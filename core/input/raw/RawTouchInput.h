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

    struct Frame
    {
        HANDLE device = nullptr;
        int contactCount = 0;
        int receivedCount = 0;
        int remainingCount = 0;
        bool frameSync = false;
        std::int64_t timestamp = 0;
        std::vector<TouchPoint> points;

        bool HasTouch() const { return !points.empty(); }
    };

    bool Initialize(HWND hwnd);
    void Reset();
    Frame ProcessRawInput(LPARAM lParam);

private:
    struct DeviceContext
    {
        HANDLE device = nullptr;
        std::vector<BYTE> preparsedData;
        HIDP_CAPS caps{};
        int remainingContacts = 0;
        int maxContactsPerPacket = 0;
        int frameExpected = 0;
        std::vector<USHORT> rootLinks;
        bool rootLinksLoaded = false;
        std::array<bool, 256> frameSeenIds{};
        std::array<USHORT, 32> buttonUsages{};
        std::array<USAGE, 32> digitizerUsages{};
        std::array<BYTE, 8> axisValueBuffer{};
        std::array<BYTE, 8> axisRawXBuffer{};
        std::array<BYTE, 8> axisRawYBuffer{};
    };

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

    DeviceContext* GetDeviceContext(HANDLE device);
    bool InitializeDeviceContext(HANDLE device, DeviceContext& context);
    Frame ParseReports(DeviceContext& context, const RAWINPUT& raw, std::int64_t timestamp);
    void ParseContactCollections(DeviceContext& context, const BYTE* report, UINT reportLength, ULONG contactCount, std::int64_t timestamp);
    bool ParseSingleContact(DeviceContext& context, const BYTE* report, UINT reportLength, USHORT linkCollection, ULONG contactCount, std::int64_t timestamp);
    bool TryGetRootLinks(DeviceContext& context);
    int ResolveContactsToParse(const DeviceContext& context, ULONG contactCount) const;
    int QueryContactCapsCount(DeviceContext& context, ULONG fallback);
    Frame BuildFrame(DeviceContext& context, std::int64_t timestamp);
    Frame BuildReleaseAllFrame(DeviceContext& context, std::int64_t timestamp);
    void ClearActiveFingers();

    static PHIDP_PREPARSED_DATA PreparsedData(DeviceContext& context);
    static UINT ReportLengthOr(UINT reportLength, const DeviceContext& context);
    static ULONG GetUsageValue(DeviceContext& context, const BYTE* report, UINT reportLength, USHORT usagePage, USHORT linkCollection, USHORT usage);
    static LONG GetAxisValue(DeviceContext& context, const BYTE* report, UINT reportLength, USHORT linkCollection, USHORT usage);
    static bool TryGetAxisPairArrayMode(DeviceContext& context, const BYTE* report, UINT reportLength, USHORT linkCollection, ULONG& xLow, ULONG& xHigh, ULONG& yLow, ULONG& yHigh);
    static void MapDigitizerFlags(DeviceContext& context, const BYTE* report, UINT reportLength, USHORT linkCollection, ULONG& contactFlags);
    static ULONG ExtractLowBits(const std::array<BYTE, 8>& raw, int bitSize);
    static void SplitLowHigh(const std::array<BYTE, 8>& raw, int bitSize, ULONG& low, ULONG& high);

    std::vector<DeviceContext> deviceContexts_;
    std::array<Finger, 256> activeFingers_{};
    std::array<bool, 256> activeFingerIds_{};
    std::array<TouchPoint, 256> previousPoints_{};
    int lastExpected_ = 0;
    int lastReceived_ = 0;
    bool lastFrameSync_ = false;
};
