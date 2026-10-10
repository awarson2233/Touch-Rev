#include "AppSettings.h"

#include "RegistryUtils.h"

#include <vector>

namespace touchrev::settings {
namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\Touch-Rev\\Settings";
constexpr wchar_t kGestureEnabledValue[] = L"GestureEnabled";
constexpr wchar_t kSwitcherWindowEnabledValue[] = L"SwitcherWindowEnabled";
constexpr wchar_t kHookAutoInstallValue[] = L"HookAutoInstall";

AppSettings g_settings;

struct Subscriber {
    int token = 0;
    ChangeCallback callback;
};

std::vector<Subscriber>& Subscribers() {
    static std::vector<Subscriber> subscribers;
    return subscribers;
}

int NextToken() {
    static int token = 0;
    return ++token;
}

void Broadcast() {
    // 回调里可能再次订阅/退订，先拷一份快照再分发。
    const std::vector<Subscriber> snapshot = Subscribers();
    for (const Subscriber& subscriber : snapshot) {
        if (subscriber.callback) {
            subscriber.callback(g_settings);
        }
    }
}

void Persist(PCWSTR name, bool value) {
    touchrev::common::registry::WriteDword(HKEY_CURRENT_USER, kSettingsKey, name,
                                           value ? 1u : 0u);
}

void Assign(bool& field, PCWSTR name, bool value) {
    if (field == value) {
        return;
    }
    field = value;
    Persist(name, value);
    Broadcast();
}

}  // namespace

const AppSettings& Current() {
    return g_settings;
}

void Load() {
    namespace registry = touchrev::common::registry;
    g_settings.gestureEnabled =
        registry::ReadDword(HKEY_CURRENT_USER, kSettingsKey, kGestureEnabledValue,
                            g_settings.gestureEnabled ? 1u : 0u) != 0;
    g_settings.switcherWindowEnabled =
        registry::ReadDword(HKEY_CURRENT_USER, kSettingsKey, kSwitcherWindowEnabledValue,
                            g_settings.switcherWindowEnabled ? 1u : 0u) != 0;
    g_settings.hookAutoInstall =
        registry::ReadDword(HKEY_CURRENT_USER, kSettingsKey, kHookAutoInstallValue,
                            g_settings.hookAutoInstall ? 1u : 0u) != 0;
    Broadcast();
}

int Subscribe(ChangeCallback callback) {
    const int token = NextToken();
    Subscribers().push_back({token, std::move(callback)});
    return token;
}

void Unsubscribe(int token) {
    auto& subscribers = Subscribers();
    for (auto it = subscribers.begin(); it != subscribers.end(); ++it) {
        if (it->token == token) {
            subscribers.erase(it);
            return;
        }
    }
}

void SetGestureEnabled(bool value) {
    Assign(g_settings.gestureEnabled, kGestureEnabledValue, value);
}

void SetSwitcherWindowEnabled(bool value) {
    Assign(g_settings.switcherWindowEnabled, kSwitcherWindowEnabledValue, value);
}

void SetHookAutoInstall(bool value) {
    Assign(g_settings.hookAutoInstall, kHookAutoInstallValue, value);
}

}  // namespace touchrev::settings
