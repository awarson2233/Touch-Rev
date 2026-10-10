#pragma once

#include <windows.h>

#include <string>

namespace touchrev::common::autostart
{
// 开机自启的当前状态。Run 键存在但被任务管理器"启动"页禁用时，
// enabled 为 false、blockedByTaskManager 为 true。
struct State
{
    bool enabled = false;
    bool blockedByTaskManager = false;
    std::wstring command;
};

State Query();

// 写入/删除 HKCU\...\Run 项；开启时一并清除任务管理器的禁用标记。
// 失败时把 Win32 错误写入 error（可为 nullptr）。
bool SetEnabled(bool enable, DWORD* error = nullptr);
}
