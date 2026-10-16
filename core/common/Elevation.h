#pragma once

#include <windows.h>

#include <string>

namespace touchrev::common::elevation
{
// 当前进程令牌是否已提权（UAC 下以管理员身份运行）。
bool IsElevated();

// 以管理员身份重新启动本程序（弹 UAC）。用户取消时返回 false，error 为 ERROR_CANCELLED。
bool RelaunchElevated(const std::wstring& arguments, DWORD* error = nullptr);
}
