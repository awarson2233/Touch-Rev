#pragma once

#include <windows.h>
#include <hidsdi.h>

#include <array>
#include <cstdint>
#include <vector>

class RawTouchInput
{
public:
    enum class PointState
    {
        Invalid,
        Pressed,
        Moved,
        Released,
    };

    struct TouchPoint
    {
        DWORD contactId = 0;
        bool active = false;
        PointState state = PointState::Invalid;
        LONG x = 0;
        LONG y = 0;
        LONG width = 0;
        LONG height = 0;
        std::int64_t timestamp = 0;
    };

    // 数字化仪 X/Y 的量程与物理尺寸。TouchPoint 里的坐标是 HID 逻辑单位，
    // 换算到屏幕或物理距离都必须经过这里，不能直接当像素用。
    struct DeviceGeometry
    {
        bool logicalValid = false;
        LONG logicalMinX = 0;
        LONG logicalMaxX = 0;
        LONG logicalMinY = 0;
        LONG logicalMaxY = 0;
        // 每个逻辑单位对应多少个 0.1mm；physicalValid 为 false 时无法得知物理尺寸。
        bool physicalValid = false;
        double tenthMmPerUnitX = 0.0;
        double tenthMmPerUnitY = 0.0;
    };

    struct Frame
    {
        HANDLE device = nullptr;
        int contactCount = 0;
        int receivedCount = 0;
        int remainingCount = 0;
        bool frameSync = false;
        std::int64_t timestamp = 0;
        DeviceGeometry geometry{};
        std::vector<TouchPoint> points;

        bool HasTouch() const { return !points.empty(); }

        // 同步完成的帧会列出该设备所有仍按着的触点，因此"帧内没有按下的触点"即全部松开。
        // 不能用 contactCount == 0 判断：抬手那一帧设备仍会上报 Tip=0 的触点，contactCount 不为 0。
        bool AllContactsReleased() const
        {
            if (!frameSync)
            {
                return false;
            }
            for (const TouchPoint& point : points)
            {
                if (point.active)
                {
                    return false;
                }
            }
            return true;
        }
    };

    // 触摸屏在虚拟桌面上的映射：显示区域（屏幕像素）与所属显示器。
    struct DisplayMapping
    {
        HMONITOR monitor = nullptr;
        RECT displayRect{};
        bool fromPointerDevice = false;
    };

    bool Initialize(HWND hwnd);
    void Reset();
    // 一条 WM_INPUT 可能携带多个报告、完成多帧，按到达顺序全部返回。
    std::vector<Frame> ProcessRawInput(LPARAM lParam);
    // WM_INPUT_DEVICE_CHANGE(GIDC_REMOVAL) 时调用，丢弃该设备的解析状态。
    void RemoveDevice(HANDLE device);

    // 通过指针设备 API 找到这块触摸屏映射到的显示区域；找不到时退回主显示器。
    static DisplayMapping QueryDisplayMapping(HANDLE device);

private:
    struct Finger
    {
        DWORD id = 0;
        LONG x = 0;
        LONG y = 0;
        bool pressed = false;
        bool tipSwitch = false;
        bool inRange = false;
        bool confidence = false;
        LONG width = 0;
        LONG height = 0;
        std::int64_t timestamp = 0;
    };

    struct DeviceContext
    {
        HANDLE device = nullptr;
        std::vector<BYTE> preparsedData;
        HIDP_CAPS caps{};
        DeviceGeometry geometry{};
        bool hasTipSwitch = false;
        bool hasInRange = false;
        bool hasConfidence = false;
        int remainingContacts = 0;
        int maxContactsPerPacket = 0;
        int frameExpected = 0;
        std::vector<USHORT> rootLinks;
        bool rootLinksLoaded = false;
        std::array<bool, 256> frameSeenIds{};
        std::array<USAGE, 32> digitizerUsages{};
        std::array<BYTE, 8> axisValueBuffer{};
        std::array<BYTE, 8> axisRawXBuffer{};
        std::array<BYTE, 8> axisRawYBuffer{};

        // 手指状态按设备隔离：多块触摸屏的 contactId 都从 0 起编，共用一份会互相覆盖。
        std::array<Finger, 256> activeFingers{};
        std::array<bool, 256> activeFingerIds{};
        std::array<TouchPoint, 256> previousPoints{};
        int lastExpected = 0;
        int lastReceived = 0;
        bool lastFrameSync = false;
    };

    DeviceContext* GetDeviceContext(HANDLE device);
    bool InitializeDeviceContext(HANDLE device, DeviceContext& context);
    void ParseReports(DeviceContext& context, const RAWINPUT& raw, std::int64_t timestamp, std::vector<Frame>& frames);
    void ParseContactCollections(DeviceContext& context, const BYTE* report, UINT reportLength, ULONG contactCount, std::int64_t timestamp);
    bool ParseSingleContact(DeviceContext& context, const BYTE* report, UINT reportLength, USHORT linkCollection, std::int64_t timestamp);
    bool TryGetRootLinks(DeviceContext& context);
    int ResolveContactsToParse(const DeviceContext& context, ULONG contactCount) const;
    int QueryContactCapsCount(DeviceContext& context, ULONG fallback);
    Frame BuildFrame(DeviceContext& context, std::int64_t timestamp);
    Frame BuildReleaseAllFrame(DeviceContext& context, std::int64_t timestamp);

    static void ClearActiveFingers(DeviceContext& context);
    static void LoadDeviceGeometry(DeviceContext& context);
    static bool HasDigitizerButton(DeviceContext& context, USAGE usage);
    static PHIDP_PREPARSED_DATA PreparsedData(DeviceContext& context);
    static UINT ReportLengthOr(UINT reportLength, const DeviceContext& context);
    static ULONG GetUsageValue(DeviceContext& context, const BYTE* report, UINT reportLength, USHORT usagePage, USHORT linkCollection, USHORT usage);
    static LONG GetAxisValue(DeviceContext& context, const BYTE* report, UINT reportLength, USHORT linkCollection, USHORT usage);
    static bool TryGetAxisPairArrayMode(DeviceContext& context, const BYTE* report, UINT reportLength, USHORT linkCollection, ULONG& xLow, ULONG& xHigh, ULONG& yLow, ULONG& yHigh);
    static void MapDigitizerFlags(DeviceContext& context, const BYTE* report, UINT reportLength, USHORT linkCollection, ULONG& contactFlags);
    static ULONG ExtractLowBits(const std::array<BYTE, 8>& raw, int bitSize);
    static void SplitLowHigh(const std::array<BYTE, 8>& raw, int bitSize, ULONG& low, ULONG& high);

    std::vector<DeviceContext> deviceContexts_;
};
