#pragma once

#include <windows.h>

namespace touchrev::ui
{
// 按需生成应用图标（三指触控点 + 手势弧线），调用方负责 DestroyIcon。
// 图标按目标像素尺寸即时绘制，避免为每档 DPI 预置位图资源。
HICON CreateAppIcon(int sizePx);
}
