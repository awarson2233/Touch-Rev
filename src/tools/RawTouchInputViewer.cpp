#include "input/raw/RawTouchInput.h"
#include "input/gesture/ThreeFingerGestureRecognizer.h"

#include <windows.h>

#include <array>
#include <cwchar>
#include <string>

namespace
{
constexpr wchar_t kWindowClassName[] = L"TouchRevGUI.RawTouchInputViewer";
constexpr wchar_t kWindowTitle[] = L"RawTouchInput Viewer";
constexpr int kMaxRows = 32;
constexpr int kMargin = 16;
constexpr int kHeaderHeight = 56;
constexpr int kRowHeight = 24;
constexpr int kColumnCount = 8;
constexpr UINT_PTR kGestureTimerId = 1;
constexpr UINT kGestureTimerMs = 16;

const int kColumnWidths[kColumnCount] = {54, 96, 72, 86, 86, 86, 86, 132};
const wchar_t* kHeaders[kColumnCount] = {L"ID", L"State", L"Active", L"X", L"Y", L"Width", L"Height", L"Timestamp"};

struct RowState
{
    bool seen = false;
    bool active = false;
    RawTouchInput::PointState state = RawTouchInput::PointState::Invalid;
    LONG x = 0;
    LONG y = 0;
    LONG width = 0;
    LONG height = 0;
    std::int64_t timestamp = 0;
};

class ViewerWindow
{
public:
    bool Initialize(HINSTANCE instance)
    {
        instance_ = instance;

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = ViewerWindow::WindowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kWindowClassName;
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

        if (RegisterClassExW(&windowClass) == 0)
        {
            return false;
        }

        const int tableWidth = TotalTableWidth();
        const int width = tableWidth + kMargin * 2 + 18;
        const int height = kHeaderHeight + kRowHeight * (kMaxRows + 1) + kMargin * 2 + 48;

        hwnd_ = CreateWindowExW(
            0,
            kWindowClassName,
            kWindowTitle,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            width,
            height,
            nullptr,
            nullptr,
            instance_,
            this);

        if (!hwnd_)
        {
            return false;
        }

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        return true;
    }

    int Run()
    {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* window = static_cast<ViewerWindow*>(createStruct->lpCreateParams);
            window->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }

        auto* window = reinterpret_cast<ViewerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (window)
        {
            return window->HandleMessage(message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            rawTouchInput_.Initialize(hwnd_);
            SetTimer(hwnd_, kGestureTimerId, kGestureTimerMs, nullptr);
            return 0;

        case WM_INPUT:
            ApplyFrame(rawTouchInput_.ProcessRawInput(lParam));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;

        case WM_TIMER:
            if (wParam == kGestureTimerId)
            {
                ApplyGestureResult(gestureRecognizer_.Tick());
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            break;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                DestroyWindow(hwnd_);
                return 0;
            }
            if (wParam == 'C')
            {
                ClearRows();
                return 0;
            }
            break;

        case WM_PAINT:
            Paint();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd_, kGestureTimerId);
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    void ApplyFrame(const RawTouchInput::Frame& frame)
    {
        if (!frame.HasTouch())
        {
            return;
        }

        ApplyGestureResult(gestureRecognizer_.ProcessFrame(frame));

        lastContactCount_ = frame.contactCount;
        lastReceivedCount_ = frame.receivedCount;
        lastRemainingCount_ = frame.remainingCount;
        lastFrameSync_ = frame.frameSync;
        lastFrameTimestamp_ = frame.timestamp;
        ++frameCount_;

        for (const RawTouchInput::TouchPoint& point : frame.points)
        {
            if (point.contactId >= rows_.size())
            {
                continue;
            }

            RowState& row = rows_[point.contactId];
            row.seen = true;
            row.active = point.active;
            row.state = point.state;
            row.x = point.x;
            row.y = point.y;
            row.width = point.width;
            row.height = point.height;
            row.timestamp = point.timestamp;
        }
    }

    void ApplyGestureResult(const ThreeFingerGestureRecognizer::Result& result)
    {
        lastGestureResult_ = result;
        if (result.type != ThreeFingerGestureRecognizer::EventType::None)
        {
            lastGestureType_ = result.type;
            lastGestureDirection_ = result.direction;
        }
    }

    void ClearRows()
    {
        rows_.fill(RowState{});
        lastContactCount_ = 0;
        lastReceivedCount_ = 0;
        lastRemainingCount_ = 0;
        lastFrameSync_ = false;
        lastFrameTimestamp_ = 0;
        frameCount_ = 0;
        rawTouchInput_.Reset();
        gestureRecognizer_.Reset();
        lastGestureResult_ = {};
        lastGestureType_ = ThreeFingerGestureRecognizer::EventType::None;
        lastGestureDirection_ = ThreeFingerGestureRecognizer::Direction::None;
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void Paint()
    {
        PAINTSTRUCT paint{};
        HDC hdc = BeginPaint(hwnd_, &paint);
        if (!hdc)
        {
            return;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);
        FillRect(hdc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

        HFONT font = CreateFontW(
            -16,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(32, 32, 32));

        wchar_t title[256]{};
        std::swprintf(
            title,
            std::size(title),
            L"RawTouchInput   Frame=%llu   Expected=%d   Received=%d   Remaining=%d   Sync=%s   Timestamp=%lld   [C] Clear   [Esc] Exit",
            frameCount_,
            lastContactCount_,
            lastReceivedCount_,
            lastRemainingCount_,
            lastFrameSync_ ? L"true" : L"false",
            static_cast<long long>(lastFrameTimestamp_));
        RECT titleRect{kMargin, kMargin, client.right - kMargin, kMargin + 24};
        DrawTextW(hdc, title, -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        wchar_t gestureText[256]{};
        std::swprintf(
            gestureText,
            std::size(gestureText),
            L"Gesture=%s   Last=%s   Direction=%s   Delta=(%.0f, %.0f)   3F=%s   SameHand=%s   Center=(%.0f, %.0f)   D=(%.0f/%.0f/%.0f)   Spread=%.2f",
            GestureTypeText(lastGestureResult_.type),
            GestureTypeText(lastGestureType_),
            DirectionText(lastGestureDirection_),
            lastGestureResult_.delta.x,
            lastGestureResult_.delta.y,
            lastGestureResult_.threeFingerActive ? L"true" : L"false",
            lastGestureResult_.sameHand ? L"true" : L"false",
            lastGestureResult_.center.x,
            lastGestureResult_.center.y,
            lastGestureResult_.distances.d01,
            lastGestureResult_.distances.d02,
            lastGestureResult_.distances.d12,
            lastGestureResult_.distances.spreadRatio);
        RECT gestureRect{kMargin, kMargin + 24, client.right - kMargin, kMargin + 50};
        DrawTextW(hdc, gestureText, -1, &gestureRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        const int tableLeft = kMargin;
        const int tableTop = kHeaderHeight;
        DrawHeader(hdc, tableLeft, tableTop);

        for (int rowIndex = 0; rowIndex < kMaxRows; ++rowIndex)
        {
            DrawRow(hdc, tableLeft, tableTop + kRowHeight * (rowIndex + 1), rowIndex, rows_[rowIndex]);
        }

        SelectObject(hdc, oldFont);
        DeleteObject(font);
        EndPaint(hwnd_, &paint);
    }

    void DrawHeader(HDC hdc, int left, int top)
    {
        RECT headerRect{left, top, left + TotalTableWidth(), top + kRowHeight};
        HBRUSH headerBrush = CreateSolidBrush(RGB(240, 240, 240));
        FillRect(hdc, &headerRect, headerBrush);
        DeleteObject(headerBrush);

        int x = left;
        for (int i = 0; i < kColumnCount; ++i)
        {
            RECT cell{x, top, x + kColumnWidths[i], top + kRowHeight};
            Rectangle(hdc, cell.left, cell.top, cell.right, cell.bottom);
            DrawTextW(hdc, kHeaders[i], -1, &cell, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            x += kColumnWidths[i];
        }
    }

    void DrawRow(HDC hdc, int left, int top, int contactId, const RowState& row)
    {
        const bool highlight = row.active;
        HBRUSH rowBrush = CreateSolidBrush(highlight ? RGB(224, 244, 255) : RGB(255, 255, 255));
        RECT rowRect{left, top, left + TotalTableWidth(), top + kRowHeight};
        FillRect(hdc, &rowRect, rowBrush);
        DeleteObject(rowBrush);

        wchar_t values[kColumnCount][64]{};
        std::swprintf(values[0], std::size(values[0]), L"%d", contactId);
        std::swprintf(values[1], std::size(values[1]), L"%s", StateText(row.state));
        std::swprintf(values[2], std::size(values[2]), L"%s", row.active ? L"true" : L"false");
        std::swprintf(values[3], std::size(values[3]), L"%ld", row.x);
        std::swprintf(values[4], std::size(values[4]), L"%ld", row.y);
        std::swprintf(values[5], std::size(values[5]), L"%ld", row.width);
        std::swprintf(values[6], std::size(values[6]), L"%ld", row.height);
        std::swprintf(values[7], std::size(values[7]), L"%lld", static_cast<long long>(row.timestamp));

        int x = left;
        for (int i = 0; i < kColumnCount; ++i)
        {
            RECT cell{x, top, x + kColumnWidths[i], top + kRowHeight};
            Rectangle(hdc, cell.left, cell.top, cell.right, cell.bottom);
            RECT textCell{cell.left + 6, cell.top, cell.right - 6, cell.bottom};
            DrawTextW(hdc, row.seen ? values[i] : (i == 0 ? values[i] : L""), -1, &textCell, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            x += kColumnWidths[i];
        }
    }

    static const wchar_t* StateText(RawTouchInput::PointState state)
    {
        switch (state)
        {
        case RawTouchInput::PointState::Pressed:
            return L"Pressed";
        case RawTouchInput::PointState::Moved:
            return L"Moved";
        case RawTouchInput::PointState::Released:
            return L"Released";
        case RawTouchInput::PointState::Invalid:
        default:
            return L"Invalid";
        }
    }

    static const wchar_t* GestureTypeText(ThreeFingerGestureRecognizer::EventType type)
    {
        switch (type)
        {
        case ThreeFingerGestureRecognizer::EventType::LongPressStarted:
            return L"LongPressStarted";
        case ThreeFingerGestureRecognizer::EventType::LongPressHolding:
            return L"LongPressHolding";
        case ThreeFingerGestureRecognizer::EventType::LongPressMoved:
            return L"LongPressMoved";
        case ThreeFingerGestureRecognizer::EventType::LongPressEnded:
            return L"LongPressEnded";
        case ThreeFingerGestureRecognizer::EventType::DoubleTap:
            return L"DoubleTap";
        case ThreeFingerGestureRecognizer::EventType::None:
        default:
            return L"None";
        }
    }

    static const wchar_t* DirectionText(ThreeFingerGestureRecognizer::Direction direction)
    {
        switch (direction)
        {
        case ThreeFingerGestureRecognizer::Direction::Up:
            return L"Up";
        case ThreeFingerGestureRecognizer::Direction::Down:
            return L"Down";
        case ThreeFingerGestureRecognizer::Direction::Left:
            return L"Left";
        case ThreeFingerGestureRecognizer::Direction::Right:
            return L"Right";
        case ThreeFingerGestureRecognizer::Direction::None:
        default:
            return L"None";
        }
    }

    static int TotalTableWidth()
    {
        int width = 0;
        for (int columnWidth : kColumnWidths)
        {
            width += columnWidth;
        }
        return width;
    }

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    RawTouchInput rawTouchInput_;
    ThreeFingerGestureRecognizer gestureRecognizer_;
    ThreeFingerGestureRecognizer::Result lastGestureResult_{};
    ThreeFingerGestureRecognizer::EventType lastGestureType_ = ThreeFingerGestureRecognizer::EventType::None;
    ThreeFingerGestureRecognizer::Direction lastGestureDirection_ = ThreeFingerGestureRecognizer::Direction::None;
    std::array<RowState, kMaxRows> rows_{};
    int lastContactCount_ = 0;
    int lastReceivedCount_ = 0;
    int lastRemainingCount_ = 0;
    bool lastFrameSync_ = false;
    std::int64_t lastFrameTimestamp_ = 0;
    unsigned long long frameCount_ = 0;
};
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    ViewerWindow window;
    if (!window.Initialize(instance))
    {
        MessageBoxW(nullptr, L"RawTouchInputViewer initialization failed.", kWindowTitle, MB_ICONERROR | MB_OK);
        return 1;
    }

    return window.Run();
}
