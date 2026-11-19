#include "bili_session.h"
#include <aclapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace gv;
namespace fs = std::filesystem;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const BiliCookie loginCookie{L".bilibili.com", L"/", L"SESSDATA", L"fake-session-only", true, true, 0};

const std::vector<BiliCookie> unsafeCookies{
    {L".bilibili.com.example",L"/",L"badDomain",L"discard"},
    {L"trapbilibili.com",L"/",L"badDomain",L"discard"},
    {L".sub..bilibili.com",L"/",L"badDomain",L"discard"},
    {L"https://bilibili.com",L"/",L"badDomain",L"discard"},
    {L".bilibili.com",L"/a\t",L"badPath",L"discard"},
    {L".bilibili.com",L"/",L"bad=name",L"discard"},
    {L".bilibili.com",L"/",L"badValue",L"discard;other=1"},
    {L".bilibili.com",L"/",L"badValue",L"discard\r\nInjected: 1"},
    {L".bilibili.com",L"/",L"expired",L"discard",true,false,1}
};

std::string file_contents(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    check(static_cast<bool>(input), "open generated file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void verify_rejects_inapplicable_credentials() {
    auto invalid = unsafeCookies;
    for (auto& cookie : invalid) {
        if (cookie.name != L"bad=name") cookie.name = L"SESSDATA";
    }
    invalid.push_back({L"bilibili.com",L"/",L"SESSDATA",L"host-only-root"});
    invalid.push_back({L".passport.bilibili.com",L"/",L"SESSDATA",L"sibling-only"});
    invalid.push_back({L".bilibili.com",L"/x/web-inter",L"SESSDATA",L"path-prefix-only"});
    invalid.push_back({L".bilibili.com",L"/",L"SESSDATA",L""});
    Cancellation cancel;
    check(!verify_bili_session(std::move(invalid), cancel),
          "credential inapplicable to the fixed account URL returns anonymous without a request");
}

void check_private_file(const std::wstring& path) {
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    check(GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
          nullptr, nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS, "read temporary cookie ACL");
    check(acl && acl->AceCount == 1, "temporary cookie has a single access rule");
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    check(GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED),
          "temporary cookie does not inherit permissions");
    void* ace = nullptr;
    check(GetAce(acl, 0, &ace), "read current-user access rule");
    auto allowed = static_cast<ACCESS_ALLOWED_ACE*>(ace);
    HANDLE token = nullptr;
    check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token), "open test user's token");
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> user(size);
    const auto read = GetTokenInformation(token, TokenUser, user.data(), size, &size);
    CloseHandle(token);
    const bool currentUserOnly = read && allowed->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
        EqualSid(&allowed->SidStart, reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
    LocalFree(descriptor);
    check(currentUserOnly, "temporary cookie permissions grant access to the current user");
}

void temporary_file_tests() {
    BiliSession session{L"测试昵称", unsafeCookies};
    session.cookies.push_back(loginCookie);
    session.cookies.push_back({L".API.BILIBILI.COM",L"/x",L"bili_jct",L"fake-csrf=valid"});
    session.cookies.push_back({L"api.bilibili.com",L"/",L"hostlimit",L"host-only-value"});
    fs::path path;
    struct TestException {};
    try {
        BiliCookieFile file(session);
        path = file.path();
        check_private_file(file.path());
        BiliCookieFile other(session);
        check(other.path() != file.path(), "temporary files are isolated for simultaneous uses");
        const auto contents = file_contents(path);
        check(contents == "# Netscape HTTP Cookie File\n"
              "#HttpOnly_.bilibili.com\tTRUE\t/\tTRUE\t0\tSESSDATA\tfake-session-only\n"
              ".api.bilibili.com\tTRUE\t/x\tTRUE\t0\tbili_jct\tfake-csrf=valid\n",
              "only valid, scoped, HTTPS cookies survive in the temporary Netscape file");
        throw TestException{};
    } catch (const TestException&) {}
    check(!fs::exists(path) && !fs::exists(path.parent_path()),
          "failed extraction scope removes credentials and the private directory");
}

void persistence_tests(const fs::path& directory) {
    check(!load_bili_session(directory.wstring()), "missing encrypted session is anonymous");
    BiliSession session{L"测试昵称", unsafeCookies};
    session.cookies.push_back(loginCookie);
    session.cookies.push_back({L"api.bilibili.com",L"/",L"hostlimit",L"host-only-value"});
    save_bili_session(directory.wstring(), session);
    const auto loaded = load_bili_session(directory.wstring());
    check(loaded && loaded->nickname == L"测试昵称" && loaded->cookies.size() == 2 &&
          loaded->cookies[0].value == L"fake-session-only" && loaded->cookies[0].http_only &&
          loaded->cookies[1].domain == L"api.bilibili.com",
          "DPAPI round trip preserves nickname and legitimate cookie scope");
    auto first = fs::directory_iterator(directory);
    check(first != fs::directory_iterator(), "save creates one session file");
    const auto path = first->path();
    check(++first == fs::directory_iterator(), "save leaves only the encrypted session file");
    const auto bytes = file_contents(path);
    check(bytes.find("fake-session-only") == std::string::npos && bytes.find("SESSDATA") == std::string::npos,
          "permanent session file exposes no plaintext authentication fields");
    session.nickname = L"第二个账号";
    save_bili_session(directory.wstring(), session);
    const auto updated = load_bili_session(directory.wstring());
    check(updated && updated->nickname == session.nickname, "overwrites the previous session atomically");
    {
        std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
        corrupt << "invalid data";
    }
    check(!load_bili_session(directory.wstring()), "corrupt ciphertext is anonymous");
    remove_bili_session(directory.wstring());
    remove_bili_session(directory.wstring());
    check(fs::is_empty(directory) && !load_bili_session(directory.wstring()), "logout removes the saved session");
}
}

int main() {
    const fs::path directory = fs::path(executable_dir()) /
        (L"bili-session-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try {
        verify_rejects_inapplicable_credentials();
        temporary_file_tests();
        check(fs::create_directory(directory), "create independent persistence fixture");
        persistence_tests(directory);
        fs::remove(directory);
        std::cout << "PASS Bili session scope, DPAPI round trip, logout and transient cookie cleanup\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        if (fs::exists(directory, ignored)) {
            for (const auto& entry : fs::directory_iterator(directory, ignored)) fs::remove(entry.path(), ignored);
            fs::remove(directory, ignored);
        }
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
