#include "AutoStart.h"

#include "PathUtils.h"
#include "RegistryUtils.h"

#include <windows.h>

namespace touchrev::common::autostart
{
namespace
{
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kStartupApprovedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr wchar_t kValueName[] = L"Touch-Rev";

std::wstring ExecutablePath()
{
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return std::wstring(path, path + length);
}

// 开机自启走 --hide：只驻留托盘与手势监听，不弹配置窗口。
std::wstring BuildCommand()
{
    return L"\"" + ExecutablePath() + L"\" --hide";
}

// 任务管理器"启动"页把禁用状态写在 StartupApproved 下的 12 字节 BLOB 里，
// 首字节最低位置 1 表示已禁用。Run 键本身不会被删掉，只看 Run 键会读出错误状态。
bool IsBlockedByTaskManager()
{
    BYTE blob[32]{};
    DWORD size = sizeof(blob);
    if (RegGetValueW(HKEY_CURRENT_USER, kStartupApprovedKey, kValueName, RRF_RT_REG_BINARY,
                     nullptr, blob, &size) != ERROR_SUCCESS ||
        size == 0)
    {
        return false;
    }
    return (blob[0] & 0x01) != 0;
}
}

State Query()
{
    State state{};
    state.command = registry::ReadString(HKEY_CURRENT_USER, kRunKey, kValueName);
    state.blockedByTaskManager = IsBlockedByTaskManager();
    state.enabled = !state.command.empty() && !state.blockedByTaskManager;
    return state;
}

bool SetEnabled(bool enable, DWORD* error)
{
    if (error)
    {
        *error = ERROR_SUCCESS;
    }

    if (!enable)
    {
        if (!registry::DeleteValue(HKEY_CURRENT_USER, kRunKey, kValueName))
        {
            if (error)
            {
                *error = GetLastError();
            }
            return false;
        }
        return true;
    }

    if (!registry::WriteString(HKEY_CURRENT_USER, kRunKey, kValueName, BuildCommand()))
    {
        if (error)
        {
            *error = GetLastError();
        }
        return false;
    }

    // 删除禁用标记，让 Run 项真正恢复生效；没有该值时视为成功。
    registry::DeleteValue(HKEY_CURRENT_USER, kStartupApprovedKey, kValueName);
    return true;
}
}
