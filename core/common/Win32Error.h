#pragma once

#include <windows.h>

#include <sstream>
#include <string>
#include <string_view>

std::wstring HResultToMessage(HRESULT hr);
std::wstring HResultToHex(HRESULT hr);
HRESULT HResultFromLastError();

// 错误日志：经 OutputDebugString 输出，所有构建都保留。同一内容 10 秒内只发送一次，
// 之后再发送时附带期间被抑制的次数，避免在每帧或每个指针事件里反复出错时刷屏。
void DebugLog(std::wstring_view message);
void DebugLogHResult(std::wstring_view context, HRESULT hr);

// 追踪日志：只在定义了 TOUCHREV_TRACE 的构建（Debug）中输出，不限频。
void DebugTrace(std::wstring_view message);

// 参数是流表达式，如 TR_TRACE(L"count=" << count)。未定义 TOUCHREV_TRACE 时整条语句编译掉，表达式不求值。
#if defined(TOUCHREV_TRACE)
#define TR_TRACE(streamExpression)                       \
    do                                                   \
    {                                                    \
        std::wostringstream touchrevTraceStream;         \
        touchrevTraceStream << streamExpression;         \
        DebugTrace(touchrevTraceStream.str());           \
    } while (0)
#else
#define TR_TRACE(streamExpression) \
    do                             \
    {                              \
    } while (0)
#endif
