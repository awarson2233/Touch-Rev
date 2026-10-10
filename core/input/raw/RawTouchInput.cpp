#include "input/raw/RawTouchInput.h"

#include "common/Win32Error.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr USHORT kUsagePageGenericDesktop = 0x01;
constexpr USHORT kUsageX = 0x30;
constexpr USHORT kUsageY = 0x31;

constexpr USHORT kUsagePageDigitizer = 0x0D;
constexpr USHORT kUsageTouchScreen = 0x04;
constexpr USHORT kUsageInRange = 0x32;
constexpr USHORT kUsageTipSwitch = 0x42;
constexpr USHORT kUsageConfidence = 0x47;
constexpr USHORT kUsageWidth = 0x48;
constexpr USHORT kUsageHeight = 0x49;
constexpr USHORT kUsageContactId = 0x51;
constexpr USHORT kUsageContactCount = 0x54;

constexpr NTSTATUS kHidSuccess = HIDP_STATUS_SUCCESS;
constexpr size_t kMaxTrackedContacts = 256;

// 合理的触摸屏物理边长范围（mm），超出视为描述符数据不可信。
constexpr double kMinPlausibleExtentMm = 20.0;
constexpr double kMaxPlausibleExtentMm = 2000.0;

std::int64_t QueryCounterValue()
{
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

bool IsPlausibleExtent(double extentMm)
{
    return extentMm >= kMinPlausibleExtentMm && extentMm <= kMaxPlausibleExtentMm;
}

// 部分描述符把 LogicalMax 写成带符号的满位值（如 16 位 0xFFFF 被读成 -1），按位宽还原成无符号。
LONG UnsignedLogical(LONG value, USHORT bitSize)
{
    if (value >= 0 || bitSize == 0 || bitSize >= 32)
    {
        return value;
    }
    return static_cast<LONG>(static_cast<ULONG>(value) & ((1ul << bitSize) - 1ul));
}

bool TryGetAxisCaps(PHIDP_PREPARSED_DATA preparsed, USHORT valueCapsCount, USHORT usage, HIDP_VALUE_CAPS& result)
{
    if (valueCapsCount == 0)
    {
        return false;
    }

    std::vector<HIDP_VALUE_CAPS> caps(valueCapsCount);
    USHORT length = valueCapsCount;
    if (HidP_GetSpecificValueCaps(HidP_Input, kUsagePageGenericDesktop, 0, usage, caps.data(), &length, preparsed) != kHidSuccess ||
        length == 0)
    {
        return false;
    }

    result = caps[0];
    return true;
}

// HID 单位：低半字节为单位制（1 = SI 线性/cm，3 = 英制线性/inch），次半字节为长度量纲，须为 1。
double PhysicalExtentMm(const HIDP_VALUE_CAPS& caps, LONG logicalMin, LONG logicalMax)
{
    LONG physicalMin = caps.PhysicalMin;
    LONG physicalMax = caps.PhysicalMax;
    if (physicalMin == 0 && physicalMax == 0)
    {
        // HID 规范：物理范围缺省时等同逻辑范围。
        physicalMin = logicalMin;
        physicalMax = logicalMax;
    }
    if (physicalMax <= physicalMin)
    {
        return 0.0;
    }

    const ULONG system = caps.Units & 0xF;
    const ULONG lengthExponent = (caps.Units >> 4) & 0xF;
    if (lengthExponent != 1)
    {
        return 0.0;
    }

    double unitMm = 0.0;
    if (system == 1)
    {
        unitMm = 10.0;
    }
    else if (system == 3)
    {
        unitMm = 25.4;
    }
    else
    {
        return 0.0;
    }

    int exponent = static_cast<int>(caps.UnitsExp & 0xF);
    if (exponent >= 8)
    {
        exponent -= 16;
    }

    return static_cast<double>(physicalMax - physicalMin) * unitMm * std::pow(10.0, exponent);
}

bool FindPointerDevice(HANDLE device, POINTER_DEVICE_INFO& result)
{
    UINT32 count = 0;
    if (!GetPointerDevices(&count, nullptr) || count == 0)
    {
        return false;
    }

    std::vector<POINTER_DEVICE_INFO> devices(count);
    if (!GetPointerDevices(&count, devices.data()))
    {
        return false;
    }
    devices.resize(count);

    const POINTER_DEVICE_INFO* onlyTouch = nullptr;
    int touchCount = 0;
    for (const POINTER_DEVICE_INFO& info : devices)
    {
        if (info.device == device)
        {
            result = info;
            return true;
        }
        if (info.pointerDeviceType == POINTER_DEVICE_TYPE_TOUCH)
        {
            onlyTouch = &info;
            ++touchCount;
        }
    }

    // 句柄对不上时，只有一块触摸屏的机器仍可确定映射关系。
    if (touchCount == 1 && onlyTouch)
    {
        result = *onlyTouch;
        return true;
    }
    return false;
}
}

PHIDP_PREPARSED_DATA RawTouchInput::PreparsedData(DeviceContext& context)
{
    return reinterpret_cast<PHIDP_PREPARSED_DATA>(context.preparsedData.data());
}

UINT RawTouchInput::ReportLengthOr(UINT reportLength, const DeviceContext& context)
{
    return reportLength != 0 ? reportLength : context.caps.InputReportByteLength;
}

bool RawTouchInput::Initialize(HWND hwnd)
{
    RAWINPUTDEVICE devices[] = {
        {kUsagePageDigitizer, kUsageTouchScreen, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, hwnd}
    };

    if (!RegisterRawInputDevices(devices, static_cast<UINT>(std::size(devices)), sizeof(RAWINPUTDEVICE)))
    {
        DebugLogHResult(L"RegisterRawInputDevices(RawTouchInput)", HResultFromLastError());
        return false;
    }

    return true;
}

void RawTouchInput::Reset()
{
    for (DeviceContext& context : deviceContexts_)
    {
        ClearActiveFingers(context);
        std::fill(context.previousPoints.begin(), context.previousPoints.end(), TouchPoint{});
        context.remainingContacts = 0;
        context.maxContactsPerPacket = 0;
        context.frameExpected = 0;
        context.frameSeenIds.fill(false);
        context.lastExpected = 0;
        context.lastReceived = 0;
        context.lastFrameSync = false;
    }
}

void RawTouchInput::RemoveDevice(HANDLE device)
{
    std::erase_if(deviceContexts_, [device](const DeviceContext& context) {
        return context.device == device;
    });
}

std::vector<RawTouchInput::Frame> RawTouchInput::ProcessRawInput(LPARAM lParam)
{
    std::vector<Frame> frames;
    UINT size = 0;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0)
    {
        return frames;
    }

    std::vector<BYTE> buffer(size);
    const UINT readSize = GetRawInputData(
        reinterpret_cast<HRAWINPUT>(lParam),
        RID_INPUT,
        buffer.data(),
        &size,
        sizeof(RAWINPUTHEADER));
    if (readSize == static_cast<UINT>(-1) || readSize == 0)
    {
        DebugLogHResult(L"GetRawInputData(RawTouchInput)", HResultFromLastError());
        return frames;
    }

    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
    if (raw->header.dwType != RIM_TYPEHID || raw->data.hid.dwSizeHid == 0 || raw->data.hid.dwCount == 0)
    {
        return frames;
    }

    DeviceContext* context = GetDeviceContext(raw->header.hDevice);
    if (!context)
    {
        return frames;
    }

    ParseReports(*context, *raw, QueryCounterValue(), frames);
    return frames;
}

RawTouchInput::DisplayMapping RawTouchInput::QueryDisplayMapping(HANDLE device)
{
    DisplayMapping mapping{};

    POINTER_DEVICE_INFO info{};
    if (device && FindPointerDevice(device, info))
    {
        RECT deviceRect{};
        RECT displayRect{};
        if (GetPointerDeviceRects(info.device, &deviceRect, &displayRect) &&
            displayRect.right > displayRect.left && displayRect.bottom > displayRect.top)
        {
            mapping.displayRect = displayRect;
            mapping.monitor = info.monitor ? info.monitor : MonitorFromRect(&displayRect, MONITOR_DEFAULTTONEAREST);
            mapping.fromPointerDevice = true;
            return mapping;
        }

        if (info.monitor)
        {
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (GetMonitorInfoW(info.monitor, &monitorInfo))
            {
                mapping.monitor = info.monitor;
                mapping.displayRect = monitorInfo.rcMonitor;
                mapping.fromPointerDevice = true;
                return mapping;
            }
        }
    }

    mapping.monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (mapping.monitor && GetMonitorInfoW(mapping.monitor, &monitorInfo))
    {
        mapping.displayRect = monitorInfo.rcMonitor;
    }
    return mapping;
}

RawTouchInput::DeviceContext* RawTouchInput::GetDeviceContext(HANDLE device)
{
    const auto existing = std::find_if(deviceContexts_.begin(), deviceContexts_.end(), [device](const DeviceContext& context) {
        return context.device == device;
    });
    if (existing != deviceContexts_.end())
    {
        return &(*existing);
    }

    DeviceContext context{};
    if (!InitializeDeviceContext(device, context))
    {
        return nullptr;
    }

    deviceContexts_.push_back(std::move(context));
    return &deviceContexts_.back();
}

bool RawTouchInput::InitializeDeviceContext(HANDLE device, DeviceContext& context)
{
    UINT preparsedSize = 0;
    if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, nullptr, &preparsedSize) != 0 || preparsedSize == 0)
    {
        return false;
    }

    context.preparsedData.resize(preparsedSize);
    if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, context.preparsedData.data(), &preparsedSize) == static_cast<UINT>(-1))
    {
        DebugLogHResult(L"GetRawInputDeviceInfoW(RIDI_PREPARSEDDATA)", HResultFromLastError());
        return false;
    }

    if (HidP_GetCaps(PreparsedData(context), &context.caps) != kHidSuccess)
    {
        return false;
    }

    context.device = device;
    context.hasTipSwitch = HasDigitizerButton(context, kUsageTipSwitch);
    context.hasInRange = HasDigitizerButton(context, kUsageInRange);
    context.hasConfidence = HasDigitizerButton(context, kUsageConfidence);
    LoadDeviceGeometry(context);
    return true;
}

bool RawTouchInput::HasDigitizerButton(DeviceContext& context, USAGE usage)
{
    const USHORT capsCount = context.caps.NumberInputButtonCaps;
    if (capsCount == 0)
    {
        return false;
    }

    std::vector<HIDP_BUTTON_CAPS> caps(capsCount);
    USHORT length = capsCount;
    return HidP_GetSpecificButtonCaps(HidP_Input, kUsagePageDigitizer, 0, usage, caps.data(), &length, PreparsedData(context)) == kHidSuccess &&
           length > 0;
}

void RawTouchInput::LoadDeviceGeometry(DeviceContext& context)
{
    DeviceGeometry& geometry = context.geometry;
    geometry = {};

    HIDP_VALUE_CAPS xCaps{};
    HIDP_VALUE_CAPS yCaps{};
    const USHORT valueCapsCount = context.caps.NumberInputValueCaps;
    if (!TryGetAxisCaps(PreparsedData(context), valueCapsCount, kUsageX, xCaps) ||
        !TryGetAxisCaps(PreparsedData(context), valueCapsCount, kUsageY, yCaps))
    {
        return;
    }

    geometry.logicalMinX = UnsignedLogical(xCaps.LogicalMin, xCaps.BitSize);
    geometry.logicalMaxX = UnsignedLogical(xCaps.LogicalMax, xCaps.BitSize);
    geometry.logicalMinY = UnsignedLogical(yCaps.LogicalMin, yCaps.BitSize);
    geometry.logicalMaxY = UnsignedLogical(yCaps.LogicalMax, yCaps.BitSize);
    geometry.logicalValid = geometry.logicalMaxX > geometry.logicalMinX && geometry.logicalMaxY > geometry.logicalMinY;
    if (!geometry.logicalValid)
    {
        return;
    }

    const double spanX = static_cast<double>(geometry.logicalMaxX - geometry.logicalMinX);
    const double spanY = static_cast<double>(geometry.logicalMaxY - geometry.logicalMinY);

    double extentXMm = PhysicalExtentMm(xCaps, geometry.logicalMinX, geometry.logicalMaxX);
    double extentYMm = PhysicalExtentMm(yCaps, geometry.logicalMinY, geometry.logicalMaxY);

    // 描述符没有可用的物理单位时，退回指针栈报告的设备尺寸（HIMETRIC = 0.01mm）。
    if (!IsPlausibleExtent(extentXMm) || !IsPlausibleExtent(extentYMm))
    {
        POINTER_DEVICE_INFO info{};
        RECT deviceRect{};
        RECT displayRect{};
        if (FindPointerDevice(context.device, info) && GetPointerDeviceRects(info.device, &deviceRect, &displayRect))
        {
            extentXMm = static_cast<double>(deviceRect.right - deviceRect.left) / 100.0;
            extentYMm = static_cast<double>(deviceRect.bottom - deviceRect.top) / 100.0;
        }
    }

    if (IsPlausibleExtent(extentXMm) && IsPlausibleExtent(extentYMm))
    {
        geometry.physicalValid = true;
        geometry.tenthMmPerUnitX = extentXMm * 10.0 / spanX;
        geometry.tenthMmPerUnitY = extentYMm * 10.0 / spanY;
    }
}

void RawTouchInput::ParseReports(DeviceContext& context, const RAWINPUT& raw, std::int64_t timestamp, std::vector<Frame>& frames)
{
    const auto* reportBase = reinterpret_cast<const BYTE*>(raw.data.hid.bRawData);
    const UINT reportSize = raw.data.hid.dwSizeHid;
    const UINT reportCount = raw.data.hid.dwCount;

    bool lastReportReleasedAll = false;
    for (UINT i = 0; i < reportCount; ++i)
    {
        const BYTE* report = reportBase + (static_cast<size_t>(i) * reportSize);
        lastReportReleasedAll = false;

        ULONG contactCount = 0;
        const NTSTATUS countStatus = HidP_GetUsageValue(
            HidP_Input,
            kUsagePageDigitizer,
            0,
            kUsageContactCount,
            &contactCount,
            PreparsedData(context),
            const_cast<PCHAR>(reinterpret_cast<const char*>(report)),
            reportSize);

        if (countStatus == kHidSuccess)
        {
            if (contactCount > 0)
            {
                context.frameSeenIds.fill(false);
                context.frameExpected = static_cast<int>(contactCount);
                if (context.maxContactsPerPacket <= 0)
                {
                    context.maxContactsPerPacket = QueryContactCapsCount(context, contactCount);
                }

                const int perPacket = context.maxContactsPerPacket <= 0 ? std::numeric_limits<int>::max() : context.maxContactsPerPacket;
                const int firstPacketContacts = static_cast<int>(std::min<ULONG>(contactCount, static_cast<ULONG>(perPacket)));
                context.remainingContacts = std::max(0, static_cast<int>(contactCount) - firstPacketContacts);
            }
            else if (context.remainingContacts == 0)
            {
                context.frameSeenIds.fill(false);
                context.frameExpected = 0;
                context.lastExpected = 0;
                context.lastReceived = static_cast<int>(std::count_if(
                    context.previousPoints.begin(), context.previousPoints.end(), [](const TouchPoint& point) {
                        return point.active;
                    }));
                context.lastFrameSync = context.lastReceived > 0;
                frames.push_back(BuildReleaseAllFrame(context, timestamp));
                lastReportReleasedAll = true;
                continue;
            }
        }

        if (countStatus != kHidSuccess || contactCount > 0 || context.remainingContacts > 0)
        {
            ParseContactCollections(context, report, reportSize, contactCount, timestamp);
        }

        context.lastExpected = context.frameExpected;
        context.lastReceived = static_cast<int>(std::count(context.frameSeenIds.begin(), context.frameSeenIds.end(), true));
        context.lastFrameSync = context.remainingContacts == 0 && context.lastReceived >= context.frameExpected;

        if (context.lastFrameSync)
        {
            for (size_t id = 0; id < kMaxTrackedContacts; ++id)
            {
                if (context.activeFingerIds[id] && !context.frameSeenIds[id])
                {
                    context.activeFingerIds[id] = false;
                }
            }

            // 中途完成的帧立即产出，否则会被同一条 WM_INPUT 里后续的帧覆盖掉移动增量。
            if (i + 1 < reportCount)
            {
                frames.push_back(BuildFrame(context, timestamp));
            }
        }
    }

    if (lastReportReleasedAll)
    {
        return;
    }

    if (context.frameExpected == 0)
    {
        context.lastExpected = 0;
        context.lastReceived = static_cast<int>(std::count(context.activeFingerIds.begin(), context.activeFingerIds.end(), true));
        context.lastFrameSync = context.lastReceived > 0;
        if (context.lastFrameSync)
        {
            std::fill(context.frameSeenIds.begin(), context.frameSeenIds.end(), false);
            ClearActiveFingers(context);
        }
    }

    frames.push_back(BuildFrame(context, timestamp));
}

void RawTouchInput::ParseContactCollections(
    DeviceContext& context,
    const BYTE* report,
    UINT reportLength,
    ULONG contactCount,
    std::int64_t timestamp)
{
    if (contactCount > 0)
    {
        if (context.maxContactsPerPacket <= 0)
        {
            context.maxContactsPerPacket = QueryContactCapsCount(context, contactCount);
        }

        const int perPacket = context.maxContactsPerPacket <= 0 ? std::numeric_limits<int>::max() : context.maxContactsPerPacket;
        const int firstPacketContacts = static_cast<int>(std::min<ULONG>(contactCount, static_cast<ULONG>(perPacket)));
        context.remainingContacts = std::max(0, static_cast<int>(contactCount) - firstPacketContacts);
    }

    const int contactsToParse = ResolveContactsToParse(context, contactCount);
    if (contactsToParse <= 0)
    {
        return;
    }

    int processed = 0;
    if (!TryGetRootLinks(context) || context.rootLinks.empty())
    {
        if (ParseSingleContact(context, report, reportLength, 0, timestamp))
        {
            processed = 1;
        }
        if (contactCount == 0 && context.remainingContacts > 0)
        {
            context.remainingContacts = std::max(0, context.remainingContacts - processed);
        }
        return;
    }

    for (USHORT link : context.rootLinks)
    {
        ParseSingleContact(context, report, reportLength, link, timestamp);
        ++processed;
        if (processed >= contactsToParse)
        {
            break;
        }
    }

    if (contactCount == 0 && context.remainingContacts > 0)
    {
        context.remainingContacts = std::max(0, context.remainingContacts - processed);
    }
}

bool RawTouchInput::ParseSingleContact(
    DeviceContext& context,
    const BYTE* report,
    UINT reportLength,
    USHORT linkCollection,
    std::int64_t timestamp)
{
    const ULONG contactId = GetUsageValue(context, report, reportLength, kUsagePageDigitizer, linkCollection, kUsageContactId);

    ULONG x = 0;
    ULONG xHigh = 0;
    ULONG y = 0;
    ULONG yHigh = 0;
    if (TryGetAxisPairArrayMode(context, report, reportLength, linkCollection, x, xHigh, y, yHigh))
    {
        (void)xHigh;
        (void)yHigh;
    }
    else
    {
        x = static_cast<ULONG>(GetAxisValue(context, report, reportLength, linkCollection, kUsageX));
        y = static_cast<ULONG>(GetAxisValue(context, report, reportLength, linkCollection, kUsageY));
    }

    const ULONG width = GetUsageValue(context, report, reportLength, kUsagePageDigitizer, linkCollection, kUsageWidth);
    const ULONG height = GetUsageValue(context, report, reportLength, kUsagePageDigitizer, linkCollection, kUsageHeight);

    ULONG contactFlags = 0;
    MapDigitizerFlags(context, report, reportLength, linkCollection, contactFlags);

    if (contactId == 0 && x == 0 && y == 0)
    {
        return false;
    }

    const bool tipSwitch = (contactFlags & 0x1) != 0;
    const bool inRange = (contactFlags & 0x20) != 0;
    const bool confidence = (contactFlags & 0x400) != 0;

    // 抬起帧通常仍带 Confidence=1、Tip=0，只要设备报告 TipSwitch 就以它为准；
    // Confidence=0 表示设备判定为手掌，不计为手指。
    bool pressed = false;
    if (context.hasTipSwitch)
    {
        pressed = tipSwitch && !(context.hasConfidence && !confidence);
    }
    else if (context.hasInRange || context.hasConfidence)
    {
        pressed = inRange || confidence;
    }
    else
    {
        pressed = true;
    }

    if (contactId >= kMaxTrackedContacts)
    {
        return false;
    }

    Finger& finger = context.activeFingers[contactId];
    finger.id = contactId;
    finger.x = static_cast<LONG>(x);
    finger.y = static_cast<LONG>(y);
    finger.tipSwitch = tipSwitch;
    finger.inRange = inRange;
    finger.confidence = confidence;
    finger.width = static_cast<LONG>(width);
    finger.height = static_cast<LONG>(height);
    finger.pressed = pressed;
    finger.timestamp = timestamp;
    context.activeFingerIds[contactId] = true;
    context.frameSeenIds[contactId] = true;
    return true;
}

bool RawTouchInput::TryGetRootLinks(DeviceContext& context)
{
    if (context.rootLinksLoaded)
    {
        return true;
    }

    ULONG nodeCount = 0;
    HidP_GetLinkCollectionNodes(nullptr, &nodeCount, PreparsedData(context));
    if (nodeCount <= 1)
    {
        context.rootLinksLoaded = true;
        return true;
    }

    std::vector<HIDP_LINK_COLLECTION_NODE> nodes(nodeCount);
    const NTSTATUS status = HidP_GetLinkCollectionNodes(nodes.data(), &nodeCount, PreparsedData(context));
    if (status != kHidSuccess)
    {
        context.rootLinksLoaded = true;
        return false;
    }

    context.rootLinks.clear();
    for (ULONG index = 1; index < nodeCount; ++index)
    {
        if (nodes[index].Parent == 0)
        {
            context.rootLinks.push_back(static_cast<USHORT>(index));
        }
    }

    context.rootLinksLoaded = true;
    return true;
}

int RawTouchInput::ResolveContactsToParse(const DeviceContext& context, ULONG contactCount) const
{
    const int perPacket = context.maxContactsPerPacket <= 0 ? std::numeric_limits<int>::max() : context.maxContactsPerPacket;
    if (contactCount > 0)
    {
        return static_cast<int>(std::min<ULONG>(contactCount, static_cast<ULONG>(perPacket)));
    }

    if (context.remainingContacts > 0)
    {
        return std::min(context.remainingContacts, perPacket);
    }

    return std::numeric_limits<int>::max();
}

int RawTouchInput::QueryContactCapsCount(DeviceContext& context, ULONG fallback)
{
    if (TryGetRootLinks(context) && !context.rootLinks.empty())
    {
        return static_cast<int>(context.rootLinks.size());
    }

    USHORT valueCapsLength = 0;
    const NTSTATUS status = HidP_GetSpecificValueCaps(
        HidP_Input,
        kUsagePageDigitizer,
        0,
        kUsageContactId,
        nullptr,
        &valueCapsLength,
        PreparsedData(context));
    if ((status == kHidSuccess || status == HIDP_STATUS_BUFFER_TOO_SMALL) && valueCapsLength > 0)
    {
        return valueCapsLength;
    }

    return static_cast<int>(fallback);
}

RawTouchInput::Frame RawTouchInput::BuildFrame(DeviceContext& context, std::int64_t timestamp)
{
    Frame frame{
        .device = context.device,
        .contactCount = context.lastExpected,
        .receivedCount = context.lastReceived,
        .remainingCount = context.remainingContacts,
        .frameSync = context.lastFrameSync,
        .timestamp = timestamp,
        .geometry = context.geometry,
    };

    std::array<bool, 256> currentActive{};
    for (size_t id = 0; id < kMaxTrackedContacts; ++id)
    {
        if (!context.activeFingerIds[id])
        {
            continue;
        }

        const Finger& finger = context.activeFingers[id];
        if (!finger.pressed)
        {
            TouchPoint point = context.previousPoints[id].active
                ? context.previousPoints[id]
                : TouchPoint{.contactId = static_cast<DWORD>(id)};
            point.active = false;
            point.state = PointState::Released;
            point.x = finger.x;
            point.y = finger.y;
            point.width = finger.width;
            point.height = finger.height;
            point.timestamp = finger.timestamp;
            frame.points.push_back(point);
            context.previousPoints[id] = {};
            context.activeFingerIds[id] = false;
            context.activeFingers[id] = {};
            continue;
        }

        TouchPoint point{
            .contactId = static_cast<DWORD>(id),
            .active = true,
            .state = context.previousPoints[id].active ? PointState::Moved : PointState::Pressed,
            .x = finger.x,
            .y = finger.y,
            .width = finger.width,
            .height = finger.height,
            .timestamp = finger.timestamp,
        };
        currentActive[id] = true;
        context.previousPoints[id] = point;
        frame.points.push_back(point);
    }

    if (context.lastFrameSync)
    {
        for (size_t id = 0; id < kMaxTrackedContacts; ++id)
        {
            if (!context.previousPoints[id].active || currentActive[id])
            {
                continue;
            }

            TouchPoint point = context.previousPoints[id];
            point.active = false;
            point.state = PointState::Released;
            point.timestamp = timestamp;
            frame.points.push_back(point);
            context.previousPoints[id] = {};
        }
    }

    return frame;
}

RawTouchInput::Frame RawTouchInput::BuildReleaseAllFrame(DeviceContext& context, std::int64_t timestamp)
{
    Frame frame{
        .device = context.device,
        .contactCount = 0,
        .receivedCount = context.lastReceived,
        .remainingCount = 0,
        .frameSync = context.lastFrameSync,
        .timestamp = timestamp,
        .geometry = context.geometry,
    };

    for (size_t id = 0; id < kMaxTrackedContacts; ++id)
    {
        if (!context.previousPoints[id].active)
        {
            continue;
        }

        TouchPoint point = context.previousPoints[id];
        point.active = false;
        point.state = PointState::Released;
        point.timestamp = timestamp;
        frame.points.push_back(point);
        context.previousPoints[id] = {};
    }

    ClearActiveFingers(context);
    return frame;
}

void RawTouchInput::ClearActiveFingers(DeviceContext& context)
{
    std::fill(context.activeFingers.begin(), context.activeFingers.end(), Finger{});
    std::fill(context.activeFingerIds.begin(), context.activeFingerIds.end(), false);
}

ULONG RawTouchInput::GetUsageValue(
    DeviceContext& context,
    const BYTE* report,
    UINT reportLength,
    USHORT usagePage,
    USHORT linkCollection,
    USHORT usage)
{
    ULONG value = 0;
    const NTSTATUS status = HidP_GetUsageValue(
        HidP_Input,
        usagePage,
        linkCollection,
        usage,
        &value,
        PreparsedData(context),
        const_cast<PCHAR>(reinterpret_cast<const char*>(report)),
        ReportLengthOr(reportLength, context));
    return status == kHidSuccess ? value : 0;
}

LONG RawTouchInput::GetAxisValue(DeviceContext& context, const BYTE* report, UINT reportLength, USHORT linkCollection, USHORT usage)
{
    context.axisValueBuffer.fill(0);
    const NTSTATUS arrayStatus = HidP_GetUsageValueArray(
        HidP_Input,
        kUsagePageGenericDesktop,
        linkCollection,
        usage,
        reinterpret_cast<PCHAR>(context.axisValueBuffer.data()),
        static_cast<USHORT>(context.axisValueBuffer.size()),
        PreparsedData(context),
        const_cast<PCHAR>(reinterpret_cast<const char*>(report)),
        ReportLengthOr(reportLength, context));
    if (arrayStatus == kHidSuccess)
    {
        return static_cast<LONG>(ExtractLowBits(context.axisValueBuffer, 32));
    }

    return static_cast<LONG>(GetUsageValue(context, report, reportLength, kUsagePageGenericDesktop, linkCollection, usage));
}

bool RawTouchInput::TryGetAxisPairArrayMode(
    DeviceContext& context,
    const BYTE* report,
    UINT reportLength,
    USHORT linkCollection,
    ULONG& xLow,
    ULONG& xHigh,
    ULONG& yLow,
    ULONG& yHigh)
{
    xLow = 0;
    xHigh = 0;
    yLow = 0;
    yHigh = 0;
    context.axisRawXBuffer.fill(0);
    context.axisRawYBuffer.fill(0);

    const NTSTATUS xStatus = HidP_GetUsageValueArray(
        HidP_Input,
        kUsagePageGenericDesktop,
        linkCollection,
        kUsageX,
        reinterpret_cast<PCHAR>(context.axisRawXBuffer.data()),
        static_cast<USHORT>(context.axisRawXBuffer.size()),
        PreparsedData(context),
        const_cast<PCHAR>(reinterpret_cast<const char*>(report)),
        ReportLengthOr(reportLength, context));
    if (xStatus != kHidSuccess)
    {
        return false;
    }

    const NTSTATUS yStatus = HidP_GetUsageValueArray(
        HidP_Input,
        kUsagePageGenericDesktop,
        linkCollection,
        kUsageY,
        reinterpret_cast<PCHAR>(context.axisRawYBuffer.data()),
        static_cast<USHORT>(context.axisRawYBuffer.size()),
        PreparsedData(context),
        const_cast<PCHAR>(reinterpret_cast<const char*>(report)),
        ReportLengthOr(reportLength, context));
    if (yStatus != kHidSuccess)
    {
        return false;
    }

    constexpr int assumedBitSize = 16;
    SplitLowHigh(context.axisRawXBuffer, assumedBitSize, xLow, xHigh);
    SplitLowHigh(context.axisRawYBuffer, assumedBitSize, yLow, yHigh);
    return true;
}

void RawTouchInput::MapDigitizerFlags(DeviceContext& context, const BYTE* report, UINT reportLength, USHORT linkCollection, ULONG& contactFlags)
{
    context.digitizerUsages.fill(0);
    ULONG usageCount = static_cast<ULONG>(context.digitizerUsages.size());
    const NTSTATUS status = HidP_GetUsages(
        HidP_Input,
        kUsagePageDigitizer,
        linkCollection,
        context.digitizerUsages.data(),
        &usageCount,
        PreparsedData(context),
        const_cast<PCHAR>(reinterpret_cast<const char*>(report)),
        ReportLengthOr(reportLength, context));
    if (status != kHidSuccess)
    {
        return;
    }

    for (ULONG i = 0; i < usageCount; ++i)
    {
        switch (context.digitizerUsages[i])
        {
        case kUsageTipSwitch:
            contactFlags |= 0x1;
            break;
        case kUsageInRange:
            contactFlags |= 0x20;
            break;
        case kUsageConfidence:
            contactFlags |= 0x400;
            break;
        default:
            break;
        }
    }
}

ULONG RawTouchInput::ExtractLowBits(const std::array<BYTE, 8>& raw, int bitSize)
{
    uint64_t data = 0;
    std::memcpy(&data, raw.data(), sizeof(data));
    if (bitSize <= 0 || bitSize >= 32)
    {
        return static_cast<ULONG>(data);
    }

    const ULONG mask = (1u << bitSize) - 1;
    return static_cast<ULONG>(data) & mask;
}

void RawTouchInput::SplitLowHigh(const std::array<BYTE, 8>& raw, int bitSize, ULONG& low, ULONG& high)
{
    uint64_t data = 0;
    std::memcpy(&data, raw.data(), sizeof(data));
    if (bitSize <= 0)
    {
        low = 0;
        high = 0;
        return;
    }

    if (bitSize >= 32)
    {
        low = static_cast<ULONG>(data);
        high = static_cast<ULONG>(data >> std::min(bitSize, 63));
        return;
    }

    const ULONG mask = (1u << bitSize) - 1;
    low = static_cast<ULONG>(data) & mask;
    high = static_cast<ULONG>(data >> bitSize) & mask;
}
