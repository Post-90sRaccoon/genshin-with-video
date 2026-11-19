#pragma once
#include "platform.h"
#include <optional>

namespace gv {
struct BiliCookie {
    std::wstring domain, path, name, value;
    bool secure = false, http_only = false;
    double expires = 0;
};

struct BiliSession {
    std::wstring nickname;
    std::vector<BiliCookie> cookies;
};

std::optional<BiliSession> verify_bili_session(std::vector<BiliCookie> cookies, Cancellation& cancel);
std::optional<BiliSession> load_bili_session(const std::wstring& directory);
void save_bili_session(const std::wstring& directory, const BiliSession& session);
void remove_bili_session(const std::wstring& directory);

class BiliCookieFile {
    std::wstring directory_;
    std::wstring path_;
public:
    explicit BiliCookieFile(const BiliSession& session);
    ~BiliCookieFile();
    BiliCookieFile(const BiliCookieFile&) = delete;
    BiliCookieFile& operator=(const BiliCookieFile&) = delete;
    const std::wstring& path() const { return path_; }
};
}
