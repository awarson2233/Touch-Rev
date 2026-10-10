#pragma once

#include <functional>

namespace touchrev::settings {

// 用户可配置项，进程内的唯一权威副本，落盘于 HKCU\Software\Touch-Rev\Settings。
struct AppSettings {
    // 是否开启/处理触摸手势识别
    bool gestureEnabled = true;
    // 触发手势时是否弹出卡片切换器窗口
    bool switcherWindowEnabled = true;
    // 启动时自动把拦截 DLL 注入 explorer
    bool hookAutoInstall = false;
};

// 当前配置。直接改字段不会落盘也不会广播，请走下面的 Set* 接口。
const AppSettings& Current();

// 从注册表载入（缺键时保留默认值）。进程启动时调用一次。
void Load();

// 变更订阅：托盘菜单与配置窗口靠它保持勾选态一致。返回值用于 Unsubscribe。
using ChangeCallback = std::function<void(const AppSettings&)>;
int Subscribe(ChangeCallback callback);
void Unsubscribe(int token);

// 任一 Set* 都会立即写回注册表并广播变更（值未变化时静默返回）。
void SetGestureEnabled(bool value);
void SetSwitcherWindowEnabled(bool value);
void SetHookAutoInstall(bool value);

}  // namespace touchrev::settings
