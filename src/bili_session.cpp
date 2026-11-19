#include "bili_session.h"
#include <aclapi.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <objbase.h>
#include <string_view>
#include <wincrypt.h>

namespace gv {
namespace {
constexpr std::wstring_view accountHost = L"api.bilibili.com";
constexpr std::wstring_view accountPath = L"/x/web-interface/nav";
constexpr wchar_t accountUrl[] = L"https://api.bilibili.com/x/web-interface/nav";
constexpr wchar_t sessionFilename[] = L"\\bilibili-session.dat";
constexpr size_t maxCookies = 256;
constexpr DWORD maxSavedBytes = 2 * 1024 * 1024;

[[noreturn]] void account_error() { throw std::runtime_error("无法验证 B 站登录状态，请稍后重试"); }
[[noreturn]] void save_error() { throw std::runtime_error("无法保存 B 站登录信息"); }
[[noreturn]] void jar_error() { throw std::runtime_error("无法创建 B 站临时登录文件"); }

bool ends_with(std::wstring_view text, std::wstring_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool bili_domain(std::wstring_view domain) {
    if (!domain.empty() && domain.front() == L'.') domain.remove_prefix(1);
    if (domain.size() > 253 || (domain != L"bilibili.com" && !ends_with(domain, L".bilibili.com"))) return false;
    while (!domain.empty()) {
        const auto dot = domain.find(L'.');
        const auto label = domain.substr(0, dot);
        if (label.empty() || label.size() > 63 || label.front() == L'-' || label.back() == L'-' ||
            !std::all_of(label.begin(), label.end(), [](wchar_t ch) {
                return (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'-';
            })) return false;
        if (dot == std::wstring_view::npos) break;
        domain.remove_prefix(dot + 1);
    }
    return true;
}

bool cookie_name(wchar_t ch) {
    constexpr std::wstring_view separators = L"()<>@,;:\\\"/[]?={}";
    return ch >= L'!' && ch <= L'~' && separators.find(ch) == std::wstring_view::npos;
}

bool cookie_value(wchar_t ch) {
    return ch >= L'!' && ch <= L'~' && ch != L'"' && ch != L',' && ch != L';' && ch != L'\\';
}

bool valid_cookie(BiliCookie& cookie, double now) {
    std::transform(cookie.domain.begin(), cookie.domain.end(), cookie.domain.begin(), [](wchar_t ch) {
        return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch + (L'a' - L'A')) : ch;
    });
    return bili_domain(cookie.domain) &&
        !cookie.path.empty() && cookie.path.size() <= 2048 && cookie.path.front() == L'/' &&
        std::all_of(cookie.path.begin(), cookie.path.end(), [](wchar_t ch) {
            return ch >= L'!' && ch <= L'~' && ch != L';' && ch != L'\\';
        }) &&
        !cookie.name.empty() && cookie.name.size() <= 256 && cookie.name.front() != L'$' &&
        std::all_of(cookie.name.begin(), cookie.name.end(), cookie_name) &&
        cookie.value.size() <= 4096 && std::all_of(cookie.value.begin(), cookie.value.end(), cookie_value) &&
        (cookie.expires == 0 || (std::isfinite(cookie.expires) &&
            std::floor(cookie.expires) > now && cookie.expires <= 253402300799.0));
}

void filter_cookies(std::vector<BiliCookie>& cookies) {
    const auto now = static_cast<double>(std::time(nullptr));
    cookies.erase(std::remove_if(cookies.begin(), cookies.end(), [now](BiliCookie& cookie) {
        return !valid_cookie(cookie, now);
    }), cookies.end());
    if (cookies.size() > maxCookies) cookies.resize(maxCookies);
}

bool matches_account_host(const BiliCookie& cookie) {
    const std::wstring_view domain(cookie.domain);
    if (domain.front() != L'.') return domain == accountHost;
    return domain.substr(1) == accountHost || ends_with(accountHost, domain);
}

bool matches_account_path(std::wstring_view path) {
    if (path == accountPath) return true;
    return path.size() < accountPath.size() && accountPath.substr(0, path.size()) == path &&
        (path.back() == L'/' || accountPath[path.size()] == L'/');
}

bool account_cookie(const BiliCookie& cookie) {
    return matches_account_host(cookie) && matches_account_path(cookie.path);
}

bool extraction_login_cookie(const BiliCookie& cookie) {
    // yt-dlp's default CookieJar can broaden host-only Netscape cookies to subdomains.
    // Bilibili's shared login cookie is a domain cookie available at the API root.
    return cookie.domain.front() == L'.' && matches_account_host(cookie) && cookie.path == L"/" &&
        cookie.name == L"SESSDATA" && !cookie.value.empty();
}

bool has_extraction_login(const std::vector<BiliCookie>& cookies) {
    return std::any_of(cookies.begin(), cookies.end(), extraction_login_cookie);
}

bool valid_nickname(const std::wstring& nickname) {
    return !nickname.empty() && nickname.size() <= 128 &&
        nickname.find_first_not_of(L' ') != std::wstring::npos &&
        std::none_of(nickname.begin(), nickname.end(), [](wchar_t ch) {
            return ch <= 0x1f || (ch >= 0x7f && ch <= 0x9f) || ch == 0x61c ||
                (ch >= 0x200e && ch <= 0x200f) || (ch >= 0x2028 && ch <= 0x202e) ||
                (ch >= 0x2066 && ch <= 0x2069);
        }) &&
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, nickname.data(),
            static_cast<int>(nickname.size()), nullptr, 0, nullptr, nullptr) > 0;
}

struct FileHandle {
    HANDLE value;
    ~FileHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

class PrivateAccess {
    SECURITY_DESCRIPTOR descriptor_{};
    std::vector<BYTE> acl_;
public:
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), &descriptor_, FALSE};

    bool initialize() {
        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) return false;
        FileHandle token{rawToken};
        DWORD size = 0;
        GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
        if (!size) return false;
        std::vector<BYTE> user(size);
        if (!GetTokenInformation(token.value, TokenUser, user.data(), size, &size)) return false;
        const auto sid = reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;
        const auto aclSize = static_cast<DWORD>(sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD)) +
            GetLengthSid(sid);
        acl_.resize(aclSize);
        const auto acl = reinterpret_cast<PACL>(acl_.data());
        return InitializeAcl(acl, aclSize, ACL_REVISION) &&
            AddAccessAllowedAce(acl, ACL_REVISION, FILE_ALL_ACCESS, sid) &&
            InitializeSecurityDescriptor(&descriptor_, SECURITY_DESCRIPTOR_REVISION) &&
            SetSecurityDescriptorDacl(&descriptor_, TRUE, acl, FALSE) &&
            SetSecurityDescriptorControl(&descriptor_, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    }

    bool matches(HANDLE directory) const {
        PACL dacl=nullptr;
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        const auto status=GetSecurityInfo(directory,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,
            nullptr,nullptr,&dacl,nullptr,&descriptor);
        struct FreeDescriptor { PSECURITY_DESCRIPTOR value; ~FreeDescriptor(){if(value)LocalFree(value);} } free{descriptor};
        SECURITY_DESCRIPTOR_CONTROL control{};
        DWORD revision=0;
        void* actual=nullptr;
        void* expected=nullptr;
        if(status!=ERROR_SUCCESS || !descriptor || !dacl || dacl->AceCount!=1 ||
           !GetSecurityDescriptorControl(descriptor,&control,&revision) || !(control&SE_DACL_PROTECTED) ||
           !GetAce(dacl,0,&actual) || !GetAce(reinterpret_cast<PACL>(const_cast<BYTE*>(acl_.data())),0,&expected))
            return false;
        auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(actual);
        auto* privateAce=static_cast<ACCESS_ALLOWED_ACE*>(expected);
        return ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE && ace->Mask==FILE_ALL_ACCESS &&
            EqualSid(&ace->SidStart,&privateAce->SidStart);
    }
};

unsigned long long file_time(const FILETIME& time) {
    return (static_cast<unsigned long long>(time.dwHighDateTime)<<32)|time.dwLowDateTime;
}

void cleanup_old_jars(const std::wstring& temp,const PrivateAccess& access) {
    // A terminated process may have had no chance to remove its own protected jar.
    constexpr wchar_t prefix[]=L"GenshinVideo-Bili-";
    constexpr auto day=24ULL*60*60*10000000;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    WIN32_FIND_DATAW found{};
    const HANDLE search=FindFirstFileW((temp+prefix+L"{*}").c_str(),&found);
    if(search==INVALID_HANDLE_VALUE)return;
    struct FindHandle { HANDLE value; ~FindHandle(){FindClose(value);} } guard{search};
    do {
        if(!(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (found.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
           file_time(now)<=file_time(found.ftLastWriteTime) || file_time(now)-file_time(found.ftLastWriteTime)<day)
            continue;
        const std::wstring name=found.cFileName;
        GUID guid{};
        if(name.size()!=std::size(prefix)-1+38 || name.compare(0,std::size(prefix)-1,prefix)!=0)continue;
        const auto suffix=name.substr(std::size(prefix)-1);
        if(FAILED(CLSIDFromString(suffix.c_str(),&guid)))continue;
        const auto directory=temp+name;
        {
            // Deny delete/rename of the parent until its single known file is removed.
            FileHandle handle{CreateFileW(directory.c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
                nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
            BY_HANDLE_FILE_INFORMATION info{};
            if(handle.value==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle.value,&info) ||
               (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || !access.matches(handle.value))
                continue;
            DeleteFileW((directory+L"\\cookies.txt").c_str());
        }
        RemoveDirectoryW(directory.c_str());
    } while(FindNextFileW(search,&found));
}

struct ProtectedBlob {
    DATA_BLOB data{};
    ~ProtectedBlob() {
        if (data.pbData) {
            SecureZeroMemory(data.pbData, data.cbData);
            LocalFree(data.pbData);
        }
    }
};

std::wstring unique_suffix() {
    GUID guid{};
    wchar_t text[40]{};
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, text, ARRAYSIZE(text))) return {};
    return text;
}

bool write_private_file(const std::wstring& path, SECURITY_ATTRIBUTES& access, const void* data, DWORD size) {
    bool written = false;
    {
        FileHandle file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, &access,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (file.value == INVALID_HANDLE_VALUE) return false;
        DWORD count = 0;
        written = WriteFile(file.value, data, size, &count, nullptr) && count == size &&
            FlushFileBuffers(file.value);
    }
    if (!written) DeleteFileW(path.c_str());
    return written;
}

json session_json(const BiliSession& session, const std::vector<BiliCookie>& cookies) {
    json records = json::array();
    for (const auto& cookie : cookies) {
        records.push_back({
            {"domain", utf8(cookie.domain)}, {"path", utf8(cookie.path)},
            {"name", utf8(cookie.name)}, {"value", utf8(cookie.value)},
            {"secure", cookie.secure}, {"http_only", cookie.http_only}, {"expires", cookie.expires}
        });
    }
    return {{"version", 1}, {"nickname", utf8(session.nickname)}, {"cookies", std::move(records)}};
}

BiliSession decoded_session(const json& saved) {
    if (!saved.is_object() || saved.value("version", 0) != 1) return {};
    BiliSession session{wide(saved.at("nickname").get<std::string>()), {}};
    const auto& records = saved.at("cookies");
    if (!valid_nickname(session.nickname) || !records.is_array() || records.size() > maxCookies) return {};
    for (const auto& record : records) {
        session.cookies.push_back({
            wide(record.at("domain").get<std::string>()), wide(record.at("path").get<std::string>()),
            wide(record.at("name").get<std::string>()), wide(record.at("value").get<std::string>()),
            record.at("secure").get<bool>(), record.at("http_only").get<bool>(), record.at("expires").get<double>()
        });
    }
    filter_cookies(session.cookies);
    return session;
}
}

std::optional<BiliSession> verify_bili_session(std::vector<BiliCookie> cookies, Cancellation& cancel) {
    cancel.check();
    filter_cookies(cookies);
    if (!has_extraction_login(cookies)) return std::nullopt;

    std::vector<const BiliCookie*> eligible;
    for (const auto& cookie : cookies) {
        if (account_cookie(cookie)) eligible.push_back(&cookie);
    }
    std::stable_sort(eligible.begin(), eligible.end(), [](const BiliCookie* a, const BiliCookie* b) {
        return a->path.size() > b->path.size();
    });
    std::wstring header;
    for (const auto* cookie : eligible) {
        if (!header.empty()) header += L"; ";
        header += cookie->name + L"=" + cookie->value;
        if (header.size() > 16384) account_error();
    }

    const auto response = http_get(accountUrl, cancel, header);
    if (response.status != 200) account_error();
    const auto payload = json::parse(response.body, nullptr, false);
    if (!payload.is_object()) account_error();
    const auto code = payload.find("code");
    if (code == payload.end() || !code->is_number_integer()) account_error();
    if (*code == -101) return std::nullopt;
    const auto data = payload.find("data");
    if (*code != 0 || data == payload.end() || !data->is_object()) account_error();
    const auto loggedIn = data->find("isLogin");
    if (loggedIn == data->end() || !loggedIn->is_boolean()) account_error();
    if (!loggedIn->get<bool>()) return std::nullopt;
    const auto name = data->find("uname");
    if (name == data->end() || !name->is_string() || name->get_ref<const std::string&>().size() > 512) account_error();
    auto nickname = wide(name->get_ref<const std::string&>());
    if (!valid_nickname(nickname)) account_error();
    return BiliSession{std::move(nickname), std::move(cookies)};
}

std::optional<BiliSession> load_bili_session(const std::wstring& directory) {
    const auto path = directory + sessionFilename;
    FileHandle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    BY_HANDLE_FILE_INFORMATION info{};
    if (file.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.value, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nFileSizeHigh || !info.nFileSizeLow || info.nFileSizeLow > maxSavedBytes) return std::nullopt;

    std::vector<BYTE> bytes(info.nFileSizeLow);
    DWORD read = 0;
    if (!ReadFile(file.value, bytes.data(), info.nFileSizeLow, &read, nullptr) ||
        read != info.nFileSizeLow) return std::nullopt;
    DATA_BLOB encrypted{read, bytes.data()};
    ProtectedBlob plain;
    if (!CryptUnprotectData(&encrypted, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain.data) ||
        !plain.data.cbData || plain.data.cbData > maxSavedBytes) return std::nullopt;
    try {
        auto session = decoded_session(json::parse(plain.data.pbData, plain.data.pbData + plain.data.cbData));
        if (has_extraction_login(session.cookies)) return session;
    } catch (const json::exception&) {
        // Decrypted data from an older or damaged session is unusable.
    } catch (const std::runtime_error&) {
        // Treat invalid UTF-8 as a damaged session.
    }
    return std::nullopt;
}

void save_bili_session(const std::wstring& directory, const BiliSession& session) {
    auto cookies = session.cookies;
    filter_cookies(cookies);
    if (!valid_nickname(session.nickname) || !has_extraction_login(cookies)) save_error();
    auto plain = session_json(session, cookies).dump();
    DATA_BLOB source{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(plain.data())};
    ProtectedBlob encrypted;
    const bool protectedData = CryptProtectData(&source, L"GenshinVideo Bilibili", nullptr,
        nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted.data) != FALSE;
    SecureZeroMemory(plain.data(), plain.size());
    if (!protectedData || encrypted.data.cbData > maxSavedBytes) save_error();

    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) save_error();
    PrivateAccess access;
    const auto suffix = unique_suffix();
    if (suffix.empty() || !access.initialize()) save_error();
    const auto target = directory + sessionFilename;
    const auto stage = target + suffix + L".tmp";
    if (!write_private_file(stage, access.attributes, encrypted.data.pbData, encrypted.data.cbData)) save_error();
    if (!MoveFileExW(stage.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(stage.c_str());
        save_error();
    }
}

void remove_bili_session(const std::wstring& directory) {
    if (!DeleteFileW((directory + sessionFilename).c_str())) {
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            throw std::runtime_error("无法删除 B 站登录信息");
        }
    }
}

BiliCookieFile::BiliCookieFile(const BiliSession& session) {
    auto cookies = session.cookies;
    filter_cookies(cookies);
    if (!has_extraction_login(cookies)) jar_error();

    std::wstring temp(32768, L'\0');
    const auto length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
    if (!length || length >= temp.size()) jar_error();
    temp.resize(length);
    const auto suffix = unique_suffix();
    PrivateAccess access;
    if (suffix.empty() || !access.initialize()) jar_error();
    static std::once_flag swept;
    std::call_once(swept,[&]{cleanup_old_jars(temp,access);});
    directory_ = temp + L"GenshinVideo-Bili-" + suffix;
    path_ = directory_ + L"\\cookies.txt";

    std::string contents = "# Netscape HTTP Cookie File\n";
    for (const auto& cookie : cookies) {
        if (cookie.domain.front() != L'.') continue;
        if (cookie.http_only) contents += "#HttpOnly_";
        contents += utf8(cookie.domain) + "\tTRUE\t" + utf8(cookie.path) + "\tTRUE\t" +
            std::to_string(static_cast<long long>(cookie.expires)) + '\t' + utf8(cookie.name) + '\t' +
            utf8(cookie.value) + '\n';
    }
    if (!CreateDirectoryW(directory_.c_str(), &access.attributes)) jar_error();
    const bool written = write_private_file(path_, access.attributes, contents.data(), static_cast<DWORD>(contents.size()));
    SecureZeroMemory(contents.data(), contents.size());
    if (!written) {
        RemoveDirectoryW(directory_.c_str());
        jar_error();
    }
}

BiliCookieFile::~BiliCookieFile() {
    DeleteFileW(path_.c_str());
    RemoveDirectoryW(directory_.c_str());
}
}
