#pragma once
#include "platform.h"
#include <commctrl.h>
#include <array>
#include <optional>
namespace gv {
constexpr int HOTKEY_COUNT=8;
struct Settings {
    int quality=720,volume=35;
    double speed=1.0;
    std::wstring cookies;
    std::array<WORD,HOTKEY_COUNT> hotkeys={{VK_F6,VK_F7,VK_F8,VK_F9,VK_F10,MAKEWORD(VK_OEM_4,HOTKEYF_CONTROL|HOTKEYF_ALT),MAKEWORD(VK_OEM_6,HOTKEYF_CONTROL|HOTKEYF_ALT),MAKEWORD('0',HOTKEYF_CONTROL|HOTKEYF_ALT)}};
};
Settings read_settings(const json& data);
json write_settings(const Settings& settings);
std::optional<std::string> open_dialog(HWND owner,const std::string& initial);
std::optional<size_t> parts_dialog(HWND owner,const Guide& guide,size_t current);
std::optional<Settings> settings_dialog(HWND owner,const Settings& settings);
UINT hotkey_modifiers(WORD key);
std::wstring hotkey_name(WORD key);
}
