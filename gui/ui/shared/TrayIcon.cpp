#include "TrayIcon.h"

#include "AppIcon.h"

#include "common/AppSettings.h"
#include "common/AutoStart.h"
#include "common/Win32Error.h"

#include <shellapi.h>
#include <strsafe.h>
#include <windowsx.h>

namespace touchrev::ui
{
namespace
{
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kTrayIconId = 1;

enum TrayCommand : UINT
{
    CommandOpenConfig = 100,
    CommandToggleHook,
    CommandToggleGesture,
    CommandToggleSwitcher,
    CommandToggleAutoStart,
    CommandClearPdbCache,
    CommandExit,
};

void AppendCheckable(HMENU menu, UINT command, PCWSTR text, bool checked, bool enabled = true)
{
    UINT flags = MF_STRING;
    flags |= checked ? MF_CHECKED : MF_UNCHECKED;
    flags |= enabled ? MF_ENABLED : MF_GRAYED;
    AppendMenuW(menu, flags, command, text);
}
}

bool TrayIcon::Initialize(HINSTANCE instance, TrayCallbacks callbacks)
{
    instance_ = instance;
    callbacks_ = std::move(callbacks);
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = TrayIcon::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = TrayIcon::WindowClassName;
    RegisterClassExW(&windowClass);

    hwnd_ = CreateWindowExW(
        0,
        TrayIcon::WindowClassName,
        L"Touch-Rev",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr,
        nullptr,
        instance_,
        this);

    if (!hwnd_)
    {
        DebugLogHResult(L"CreateWindowExW(TrayWindow)", HResultFromLastError());
        return false;
    }

    // 以管理员身份运行时，explorer（普通权限）发来的托盘回调与 TaskbarCreated 会被 UIPI 丢弃，需显式放行。
    ChangeWindowMessageFilterEx(hwnd_, kTrayCallbackMessage, MSGFLT_ALLOW, nullptr);
    if (taskbarCreatedMessage_ != 0)
    {
        ChangeWindowMessageFilterEx(hwnd_, taskbarCreatedMessage_, MSGFLT_ALLOW, nullptr);
    }

    RefreshIconImage();
    return AddNotifyIcon();
}

void TrayIcon::Shutdown()
{
    if (iconAdded_ && hwnd_)
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = hwnd_;
        data.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        iconAdded_ = false;
    }

    if (icon_)
    {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }

    if (hwnd_)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void TrayIcon::RefreshIconImage()
{
    HICON icon = CreateAppIcon(GetSystemMetrics(SM_CXSMICON));
    if (!icon)
    {
        return;
    }

    if (icon_)
    {
        DestroyIcon(icon_);
    }
    icon_ = icon;
}

bool TrayIcon::AddNotifyIcon()
{
    if (!hwnd_)
    {
        return false;
    }

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kTrayIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kTrayCallbackMessage;
    data.hIcon = icon_;
    StringCchCopyW(data.szTip, ARRAYSIZE(data.szTip), L"Touch-Rev 触控手势增强");

    if (!Shell_NotifyIconW(NIM_ADD, &data))
    {
        DebugLogHResult(L"Shell_NotifyIconW(NIM_ADD)", HResultFromLastError());
        return false;
    }

    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
    iconAdded_ = true;
    return true;
}

void TrayIcon::Notify(const std::wstring& title, const std::wstring& text, bool isError)
{
    if (!iconAdded_)
    {
        return;
    }

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kTrayIconId;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = isError ? NIIF_ERROR : NIIF_INFO;
    StringCchCopyW(data.szInfoTitle, ARRAYSIZE(data.szInfoTitle), title.c_str());
    StringCchCopyW(data.szInfo, ARRAYSIZE(data.szInfo), text.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

LRESULT CALLBACK TrayIcon::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const auto createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* tray = static_cast<TrayIcon*>(createStruct->lpCreateParams);
        tray->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tray));
    }

    auto* tray = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (tray)
    {
        return tray->HandleMessage(message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT TrayIcon::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0)
    {
        // explorer 重启后图标会被清掉，必须重新注册，否则托盘入口永久消失。
        iconAdded_ = false;
        RefreshIconImage();
        AddNotifyIcon();
        if (callbacks_.onTaskbarCreated)
        {
            callbacks_.onTaskbarCreated();
        }
        return 0;
    }

    switch (message)
    {
    case kTrayCallbackMessage:
        switch (LOWORD(lParam))
        {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_LBUTTONUP:
            if (callbacks_.onOpenConfig)
            {
                callbacks_.onOpenConfig();
            }
            return 0;

        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            ShowContextMenu();
            return 0;
        }
        return 0;

    case WM_SETTINGCHANGE:
    case WM_DPICHANGED:
    case WM_THEMECHANGED:
        if (iconAdded_)
        {
            RefreshIconImage();
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = hwnd_;
            data.uID = kTrayIconId;
            data.uFlags = NIF_ICON;
            data.hIcon = icon_;
            Shell_NotifyIconW(NIM_MODIFY, &data);
        }
        return 0;

    case WM_DESTROY:
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void TrayIcon::ShowContextMenu()
{
    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return;
    }

    const auto& current = touchrev::settings::Current();
    const auto autoStart = touchrev::common::autostart::Query();
    const bool hookEnabled = callbacks_.onQueryHookEnabled ? callbacks_.onQueryHookEnabled() : false;
    const bool hookBusy = callbacks_.onQueryHookBusy ? callbacks_.onQueryHookBusy() : false;

    AppendMenuW(menu, MF_STRING, CommandOpenConfig, L"打开控制中心(&O)");
    SetMenuDefaultItem(menu, CommandOpenConfig, FALSE);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendCheckable(menu, CommandToggleHook, hookBusy ? L"拦截系统默认手势（处理中…）" : L"拦截系统默认手势",
                    hookEnabled, callbacks_.onToggleHook != nullptr && !hookBusy);
    AppendCheckable(menu, CommandToggleGesture, L"自定义手势识别", current.gestureEnabled);
    AppendCheckable(menu, CommandToggleSwitcher, L"任务切换卡片窗口", current.switcherWindowEnabled);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendCheckable(menu, CommandToggleAutoStart, L"开机自动启动", autoStart.enabled);
    AppendMenuW(menu, MF_STRING | (hookBusy ? MF_GRAYED : MF_ENABLED), CommandClearPdbCache, L"清除 PDB 符号缓存");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CommandExit, L"退出 Touch-Rev(&X)");

    POINT cursor{};
    GetCursorPos(&cursor);

    // 菜单必须在前台窗口下弹出，否则点击别处时不会消失。
    SetForegroundWindow(hwnd_);
    const int command = TrackPopupMenuEx(
        menu,
        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        cursor.x,
        cursor.y,
        hwnd_,
        nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (command)
    {
    case CommandOpenConfig:
        if (callbacks_.onOpenConfig)
        {
            callbacks_.onOpenConfig();
        }
        break;

    case CommandToggleHook:
        if (callbacks_.onToggleHook)
        {
            callbacks_.onToggleHook(!hookEnabled);
        }
        break;

    case CommandToggleGesture:
        touchrev::settings::SetGestureEnabled(!current.gestureEnabled);
        break;

    case CommandToggleSwitcher:
        touchrev::settings::SetSwitcherWindowEnabled(!current.switcherWindowEnabled);
        break;

    case CommandToggleAutoStart:
    {
        DWORD error = ERROR_SUCCESS;
        if (!touchrev::common::autostart::SetEnabled(!autoStart.enabled, &error))
        {
            Notify(L"Touch-Rev", L"设置开机自动启动失败。", true);
        }
        break;
    }

    case CommandClearPdbCache:
        if (callbacks_.onClearPdbCache)
        {
            callbacks_.onClearPdbCache();
        }
        break;

    case CommandExit:
        if (callbacks_.onExit)
        {
            callbacks_.onExit();
        }
        break;

    default:
        break;
    }
}
}
