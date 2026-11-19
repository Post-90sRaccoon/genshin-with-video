#pragma once

#include "bili_session.h"
#include <windows.h>

namespace gv {
// When requested, returns a browser process handle owned by the caller for profile cleanup.
std::optional<BiliSession> bili_login_dialog(HWND owner, const std::wstring& profile_directory,
    HANDLE* browser_process = nullptr);
}
