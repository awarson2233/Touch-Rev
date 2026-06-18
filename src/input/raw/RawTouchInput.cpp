#include "input/raw/RawTouchInput.h"

#include "common/Win32Error.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace
{
constexpr USHORT kUsagePageGenericDesktop = 0x01;
constexpr USHORT kUsageX = 0x30;
constexpr USHORT kUsageY = 0x31;

constexpr USHORT kUsagePageDigitizer = 0x0D;
constexpr USHORT kUsageTouchScreen = 0x04;
constexpr USHORT kUsageTouchPad = 0x05;
constexpr USHORT kUsageInRange = 0x32;
constexpr USHORT kUsageTipSwitch = 0x42;
constexpr USHORT kUsageConfidence = 0x47;
constexpr USHORT kUsageWidth = 0x48;
constexpr USHORT kUsageHeight = 0x49;
constexpr USHORT kUsageContactId = 0x51;
constexpr USHORT kUsageContactCount = 0x54;

constexpr USHORT kUsagePageButton = 0x09;
constexpr NTSTATUS kHidSuccess = HIDP_STATUS_SUCCESS;
constexpr size_t kMaxTrackedContacts = 256;
constexpr int kLinkNodeStrideFallback = 0x18;

std::int64_t QueryCounterValue()
{
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
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
        {kUsagePageDigitizer, kUsageTouchScreen, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, hwnd},
        {kUsagePageDigitizer, kUsageTouchPad, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, hwnd},
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
    ClearActiveFingers();
    std::fill(previousPoints_.begin(), previousPoints_.end(), TouchPoint{});
    for (DeviceContext& context : deviceContexts_)
    {
        context.remainingContacts = 0;
        context.maxContactsPerPacket = 0;
        context.frameExpected = 0;
        context.frameSeenIds.fill(false);
    }
    lastExpected_ = 0;
    lastReceived_ = 0;
    lastFrameSync_ = false;
}

RawTouchInput::Frame RawTouchInput::ProcessRawInput(LPARAM lParam)
{
    Frame frame{};
    UINT size = 0;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0)
    {
        return frame;
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
        return frame;
    }

    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
    if (raw->header.dwType != RIM_TYPEHID || raw->data.hid.dwSizeHid == 0 || raw->data.hid.dwCount == 0)
    {
        return frame;
    }

    DeviceContext* context = GetDeviceContext(raw->header.hDevice);
    if (!context)
    {
        return frame;
    }

    return ParseReports(*context, *raw, QueryCounterValue());
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
    return true;
}

RawTouchInput::Frame RawTouchInput::ParseReports(DeviceContext& context, const RAWINPUT& raw, std::int64_t timestamp)
{
    const auto* reportBase = reinterpret_cast<const BYTE*>(raw.data.hid.bRawData);
    const UINT reportSize = raw.data.hid.dwSizeHid;
    const UINT reportCount = raw.data.hid.dwCount;

    for (UINT i = 0; i < reportCount; ++i)
    {
        const BYTE* report = reportBase + (static_cast<size_t>(i) * reportSize);

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
                lastExpected_ = 0;
                lastReceived_ = 0;
                for (const TouchPoint& point : previousPoints_)
                {
                    if (point.active)
                    {
                        ++lastReceived_;
                    }
                }
                lastFrameSync_ = lastReceived_ > 0;
                return BuildReleaseAllFrame(context, timestamp);
            }
        }

        if (countStatus != kHidSuccess || contactCount > 0 || context.remainingContacts > 0)
        {
            ParseContactCollections(context, report, reportSize, contactCount, timestamp);
        }

        lastExpected_ = context.frameExpected;
        lastReceived_ = static_cast<int>(std::count(context.frameSeenIds.begin(), context.frameSeenIds.end(), true));
        lastFrameSync_ = context.remainingContacts == 0 && lastReceived_ >= context.frameExpected;

        if (lastFrameSync_)
        {
            for (size_t id = 0; id < kMaxTrackedContacts; ++id)
            {
                if (activeFingerIds_[id] && !context.frameSeenIds[id])
                {
                    activeFingerIds_[id] = false;
                }
            }
        }
    }

    if (context.frameExpected == 0)
    {
        lastExpected_ = 0;
        lastReceived_ = static_cast<int>(std::count(activeFingerIds_.begin(), activeFingerIds_.end(), true));
        lastFrameSync_ = lastReceived_ > 0;
        if (lastFrameSync_)
        {
            std::fill(context.frameSeenIds.begin(), context.frameSeenIds.end(), false);
            ClearActiveFingers();
        }
    }

    return BuildFrame(context, timestamp);
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
        if (ParseSingleContact(context, report, reportLength, 0, contactCount, timestamp))
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
        ParseSingleContact(context, report, reportLength, link, contactCount, timestamp);
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
    ULONG,
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
    const bool pressed = tipSwitch || inRange || confidence;
    if (contactId >= kMaxTrackedContacts)
    {
        return false;
    }

    Finger& finger = activeFingers_[contactId];
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
    activeFingerIds_[contactId] = true;
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
        .contactCount = lastExpected_,
        .receivedCount = lastReceived_,
        .remainingCount = context.remainingContacts,
        .frameSync = lastFrameSync_,
        .timestamp = timestamp,
    };

    std::array<bool, 256> currentActive{};
    for (size_t id = 0; id < kMaxTrackedContacts; ++id)
    {
        if (!activeFingerIds_[id])
        {
            continue;
        }

        const Finger& finger = activeFingers_[id];
        if (!finger.pressed)
        {
            TouchPoint point = previousPoints_[id].active
                ? previousPoints_[id]
                : TouchPoint{.contactId = static_cast<DWORD>(id)};
            point.active = false;
            point.state = PointState::Released;
            point.x = finger.x;
            point.y = finger.y;
            point.width = finger.width;
            point.height = finger.height;
            point.timestamp = finger.timestamp;
            frame.points.push_back(point);
            previousPoints_[id] = {};
            activeFingerIds_[id] = false;
            activeFingers_[id] = {};
            continue;
        }

        TouchPoint point{
            .contactId = static_cast<DWORD>(id),
            .active = true,
            .state = previousPoints_[id].active ? PointState::Moved : PointState::Pressed,
            .x = finger.x,
            .y = finger.y,
            .width = finger.width,
            .height = finger.height,
            .timestamp = finger.timestamp,
        };
        currentActive[id] = true;
        previousPoints_[id] = point;
        frame.points.push_back(point);
    }

    if (lastFrameSync_)
    {
        for (size_t id = 0; id < kMaxTrackedContacts; ++id)
        {
            if (!previousPoints_[id].active || currentActive[id])
            {
                continue;
            }

            TouchPoint point = previousPoints_[id];
            point.active = false;
            point.state = PointState::Released;
            point.timestamp = timestamp;
            frame.points.push_back(point);
            previousPoints_[id] = {};
        }
    }

    return frame;
}

RawTouchInput::Frame RawTouchInput::BuildReleaseAllFrame(DeviceContext& context, std::int64_t timestamp)
{
    Frame frame{
        .device = context.device,
        .contactCount = 0,
        .receivedCount = lastReceived_,
        .remainingCount = 0,
        .frameSync = lastFrameSync_,
        .timestamp = timestamp,
    };

    for (size_t id = 0; id < kMaxTrackedContacts; ++id)
    {
        if (!previousPoints_[id].active)
        {
            continue;
        }

        TouchPoint point = previousPoints_[id];
        point.active = false;
        point.state = PointState::Released;
        point.timestamp = timestamp;
        frame.points.push_back(point);
        previousPoints_[id] = {};
    }

    ClearActiveFingers();
    return frame;
}

void RawTouchInput::ClearActiveFingers()
{
    std::fill(activeFingers_.begin(), activeFingers_.end(), Finger{});
    std::fill(activeFingerIds_.begin(), activeFingerIds_.end(), false);
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
