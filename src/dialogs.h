#pragma once
#include "platform.h"
#include <array>
#include <optional>
namespace gv {
struct Settings {
    int quality=720,volume=35;
    std::wstring cookies;
    std::array<WORD,5> hotkeys={{VK_F6,VK_F7,VK_F8,VK_F9,VK_F10}};
};
Settings read_settings(const json& data);
json write_settings(const Settings& settings);
std::optional<std::string> open_dialog(HWND owner,const std::string& initial);
std::optional<size_t> parts_dialog(HWND owner,const Guide& guide,size_t current);
std::optional<Settings> settings_dialog(HWND owner,const Settings& settings);
UINT hotkey_modifiers(WORD key);
std::wstring hotkey_name(WORD key);
}
