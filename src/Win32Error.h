#pragma once

#include <windows.h>

#include <string>
#include <string_view>

std::wstring HResultToMessage(HRESULT hr);
std::wstring HResultToHex(HRESULT hr);
HRESULT HResultFromLastError();

void DebugLog(std::wstring_view message);
void DebugLogHResult(std::wstring_view context, HRESULT hr);
