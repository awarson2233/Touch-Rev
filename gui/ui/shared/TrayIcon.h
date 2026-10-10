#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace touchrev::ui
{
struct TrayCallbacks
{
    std::function<void()> onOpenConfig;
    std::function<bool()> onQueryHookEnabled;
    // 拦截操作在后台进行时，菜单里的拦截相关项置灰。
    std::function<bool()> onQueryHookBusy;
    std::function<void(bool)> onToggleHook;
    std::function<void()> onClearPdbCache;
    std::function<void()> onExit;
    // explorer 重启（TaskbarCreated）：注入到旧 explorer 的拦截已随之消失，需要宿主恢复。
    std::function<void()> onTaskbarCreated;
};

// 托盘图标宿主。挂在一个专用的隐藏窗口上，与手势窗口的消息处理完全隔离，
// 并在 explorer 重启（TaskbarCreated）后自动重新注册图标。
class TrayIcon
{
public:
    static constexpr wchar_t WindowClassName[] = L"TouchRevGUI.TrayWindow";

    bool Initialize(HINSTANCE instance, TrayCallbacks callbacks);
    void Shutdown();

    HWND Hwnd() const { return hwnd_; }

    // 气泡通知，用于缓存清理一类的一次性结果反馈。
    void Notify(const std::wstring& title, const std::wstring& text, bool isError = false);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool AddNotifyIcon();
    void RefreshIconImage();
    void ShowContextMenu();

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HICON icon_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    bool iconAdded_ = false;
    TrayCallbacks callbacks_{};
};
}
