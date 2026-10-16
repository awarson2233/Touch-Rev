#pragma once

#include <windows.h>

#include <string>

namespace touchrev::common::autostart
{
// 开机自启有两种形式：普通权限走 HKCU\...\Run；"以管理员身份运行"开启时走以最高权限运行的计划任务，
// 因为 Run 项无法静默提权，登录时会被系统跳过或每次弹 UAC。
// Run 键存在但被任务管理器"启动"页禁用时，enabled 为 false、blockedByTaskManager 为 true。
struct State
{
    bool enabled = false;
    bool blockedByTaskManager = false;
    // 当前生效的是计划任务形式
    bool elevatedTask = false;
    std::wstring command;
};

State Query();

// 开启时按"以管理员身份运行"设置选择形式，并清除另一种形式；关闭时两种都清除。
// 注册计划任务需要当前进程已提权，未提权时退回 Run 项，下次以管理员身份启动时由 Sync 迁移。
// 失败时把 Win32 错误写入 error（可为 nullptr）。
bool SetEnabled(bool enable, DWORD* error = nullptr);

// 已开启自启但形式与当前设置不符时（切换了"以管理员身份运行"）改写成对应形式；无法完成迁移时不动。
void Sync();
}
