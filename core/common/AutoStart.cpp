#include "AutoStart.h"

#include "AppSettings.h"
#include "Elevation.h"
#include "RegistryUtils.h"

#include <windows.h>
#include <oleauto.h>
#include <sddl.h>
#include <taskschd.h>
#include <winrt/base.h>

namespace touchrev::common::autostart
{
namespace
{
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kStartupApprovedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr wchar_t kValueName[] = L"Touch-Rev";
constexpr wchar_t kArguments[] = L"--hide";

std::wstring ExecutablePath()
{
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return std::wstring(path, path + length);
}

// 开机自启走 --hide：只驻留托盘与手势监听，不弹配置窗口。
std::wstring BuildCommand()
{
    return L"\"" + ExecutablePath() + L"\" " + kArguments;
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

DWORD Win32FromHResult(HRESULT hr)
{
    return HRESULT_FACILITY(hr) == FACILITY_WIN32 ? HRESULT_CODE(hr) : static_cast<DWORD>(hr);
}

class Bstr
{
public:
    explicit Bstr(const std::wstring& value) : value_(SysAllocStringLen(value.data(), static_cast<UINT>(value.size())))
    {
    }
    ~Bstr() { SysFreeString(value_); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    operator BSTR() const { return value_; }

private:
    BSTR value_ = nullptr;
};

std::wstring CurrentUserSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        return {};
    }

    BYTE buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]{};
    DWORD size = 0;
    std::wstring sid;
    if (GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &size))
    {
        PWSTR text = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid, &text))
        {
            sid = text;
            LocalFree(text);
        }
    }
    CloseHandle(token);
    return sid;
}

// 任务按用户区分：同一台机器上每个用户各有一份，互不覆盖。
std::wstring TaskName(const std::wstring& sid)
{
    return std::wstring(L"Touch-Rev Autostart ") + sid;
}

std::wstring XmlEscape(const std::wstring& text)
{
    std::wstring escaped;
    for (const wchar_t ch : text)
    {
        switch (ch)
        {
        case L'&': escaped += L"&amp;"; break;
        case L'<': escaped += L"&lt;"; break;
        case L'>': escaped += L"&gt;"; break;
        case L'"': escaped += L"&quot;"; break;
        default: escaped += ch; break;
        }
    }
    return escaped;
}

// 登录触发、以最高权限运行；不受电池限制、不限运行时长，优先级 4（计划任务默认 7 会降低 CPU/IO 优先级）。
std::wstring BuildTaskXml(const std::wstring& sid)
{
    return LR"(<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo><Description>Touch-Rev 开机自启（以管理员身份运行）</Description></RegistrationInfo>
  <Triggers><LogonTrigger><Enabled>true</Enabled><UserId>)" +
           sid + LR"(</UserId></LogonTrigger></Triggers>
  <Principals><Principal id="Author"><UserId>)" +
           sid + LR"(</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <AllowHardTerminate>false</AllowHardTerminate>
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
    <Priority>4</Priority>
    <IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>
  </Settings>
  <Actions Context="Author"><Exec><Command>)" +
           XmlEscape(ExecutablePath()) + LR"(</Command><Arguments>)" + kArguments +
           LR"(</Arguments></Exec></Actions>
</Task>)";
}

HRESULT ConnectRootFolder(winrt::com_ptr<ITaskFolder>& folder)
{
    winrt::com_ptr<ITaskService> service;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(service.put()));
    if (FAILED(hr))
    {
        return hr;
    }

    VARIANT empty{};
    VariantInit(&empty);
    hr = service->Connect(empty, empty, empty, empty);
    if (FAILED(hr))
    {
        return hr;
    }
    return service->GetFolder(Bstr(L"\\"), folder.put());
}

bool QueryTask(std::wstring& command)
{
    const std::wstring sid = CurrentUserSid();
    winrt::com_ptr<ITaskFolder> folder;
    if (sid.empty() || FAILED(ConnectRootFolder(folder)))
    {
        return false;
    }

    winrt::com_ptr<IRegisteredTask> task;
    if (FAILED(folder->GetTask(Bstr(TaskName(sid)), task.put())))
    {
        return false;
    }

    VARIANT_BOOL enabled = VARIANT_FALSE;
    if (FAILED(task->get_Enabled(&enabled)) || enabled == VARIANT_FALSE)
    {
        return false;
    }
    command = BuildCommand();
    return true;
}

HRESULT RegisterTask()
{
    const std::wstring sid = CurrentUserSid();
    if (sid.empty())
    {
        return HRESULT_FROM_WIN32(ERROR_NO_SUCH_USER);
    }

    winrt::com_ptr<ITaskFolder> folder;
    HRESULT hr = ConnectRootFolder(folder);
    if (FAILED(hr))
    {
        return hr;
    }

    VARIANT empty{};
    VariantInit(&empty);
    winrt::com_ptr<IRegisteredTask> task;
    return folder->RegisterTask(Bstr(TaskName(sid)),
                                Bstr(BuildTaskXml(sid)),
                                TASK_CREATE_OR_UPDATE,
                                empty,
                                empty,
                                TASK_LOGON_INTERACTIVE_TOKEN,
                                empty,
                                task.put());
}

HRESULT DeleteTask()
{
    const std::wstring sid = CurrentUserSid();
    winrt::com_ptr<ITaskFolder> folder;
    if (sid.empty())
    {
        return S_OK;
    }
    HRESULT hr = ConnectRootFolder(folder);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = folder->DeleteTask(Bstr(TaskName(sid)), 0);
    return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? S_OK : hr;
}

bool WantsElevatedTask()
{
    return touchrev::settings::Current().runAsAdmin;
}

bool WriteRunKey(DWORD& error)
{
    if (!registry::WriteString(HKEY_CURRENT_USER, kRunKey, kValueName, BuildCommand()))
    {
        error = GetLastError();
        return false;
    }

    // 删除禁用标记，让 Run 项真正恢复生效；没有该值时视为成功。
    registry::DeleteValue(HKEY_CURRENT_USER, kStartupApprovedKey, kValueName);
    return true;
}
}

State Query()
{
    State state{};
    if (QueryTask(state.command))
    {
        state.enabled = true;
        state.elevatedTask = true;
        return state;
    }

    state.command = registry::ReadString(HKEY_CURRENT_USER, kRunKey, kValueName);
    state.blockedByTaskManager = IsBlockedByTaskManager();
    state.enabled = !state.command.empty() && !state.blockedByTaskManager;
    return state;
}

bool SetEnabled(bool enable, DWORD* error)
{
    DWORD result = ERROR_SUCCESS;

    if (enable && WantsElevatedTask() && elevation::IsElevated())
    {
        // 计划任务需要已提权的进程才能以最高权限注册；成功后再删 Run 项，避免两种形式同时生效。
        const HRESULT hr = RegisterTask();
        if (FAILED(hr))
        {
            result = Win32FromHResult(hr);
        }
        else
        {
            registry::DeleteValue(HKEY_CURRENT_USER, kRunKey, kValueName);
        }
    }
    else if (enable)
    {
        if (WriteRunKey(result))
        {
            // 普通权限删不掉管理员注册的任务时保留它；下次以管理员身份启动时 Sync 会清理。
            DeleteTask();
        }
    }
    else
    {
        if (!registry::DeleteValue(HKEY_CURRENT_USER, kRunKey, kValueName))
        {
            result = GetLastError();
        }
        const HRESULT hr = DeleteTask();
        if (FAILED(hr) && result == ERROR_SUCCESS)
        {
            result = Win32FromHResult(hr);
        }
    }

    if (error)
    {
        *error = result;
    }
    return result == ERROR_SUCCESS;
}

void Sync()
{
    const State state = Query();
    const bool hasRunKey = !registry::ReadString(HKEY_CURRENT_USER, kRunKey, kValueName).empty();
    if (WantsElevatedTask())
    {
        if (elevation::IsElevated() && hasRunKey && !state.blockedByTaskManager)
        {
            SetEnabled(true);
        }
    }
    else if (state.elevatedTask)
    {
        SetEnabled(true);
    }
}
}
