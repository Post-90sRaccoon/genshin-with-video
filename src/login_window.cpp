#include "login_window.h"
#include "platform.h"

#include <commctrl.h>
#include <objbase.h>
#include <wrl.h>
#include <WebView2.h>

#include <algorithm>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>

namespace gv {
namespace {
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

constexpr wchar_t login_url[] = L"https://passport.bilibili.com/login";
constexpr wchar_t cookie_url[] = L"https://api.bilibili.com/";
constexpr wchar_t runtime_error[] = L"无法启动 Edge WebView2。请安装或更新 WebView2 Runtime 后重试。";
constexpr wchar_t scan_prompt[] = L"请用哔哩哔哩 App 扫码，并在手机上确认登录。";
constexpr wchar_t verify_error[] = L"暂时无法验证 B 站登录。请检查网络后重试。";
constexpr UINT_PTR login_timer = 1;
constexpr UINT start_message = WM_APP + 1;

struct CoString {
    LPWSTR value = nullptr;
    ~CoString() { CoTaskMemFree(value); }
};

bool equal_ignore_case(std::wstring_view left, std::wstring_view right) {
    return left.size() == right.size() &&
        CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
            right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

bool bilibili_host(std::wstring_view host) {
    constexpr std::wstring_view domain = L"bilibili.com";
    const auto is_dns_char = [](wchar_t ch) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'-';
    };
    if (host.size() < domain.size() || !std::all_of(host.begin(), host.end(), is_dns_char)) return false;
    const auto start = host.size() - domain.size();
    return (start == 0 || (start > 1 && host[start - 1] == L'.')) &&
        equal_ignore_case(host.substr(start), domain);
}

bool official_https(const wchar_t* uri) {
    if (!uri) return false;
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    return WinHttpCrackUrl(uri, 0, ICU_REJECT_USERPWD, &parts) &&
        parts.nScheme == INTERNET_SCHEME_HTTPS && parts.nPort == INTERNET_DEFAULT_HTTPS_PORT &&
        parts.lpszHostName && bilibili_host(std::wstring_view(parts.lpszHostName, parts.dwHostNameLength));
}

using CreateEnvironment = decltype(&CreateCoreWebView2EnvironmentWithOptions);

CreateEnvironment load_webview() {
    // Completion callbacks can still be released after the dialog closes.
    static HMODULE loader = nullptr;
    if (!loader) {
        const std::wstring dir = executable_dir();
        constexpr DWORD flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;
        loader = LoadLibraryExW((dir + L"\\WebView2Loader.dll").c_str(), nullptr, flags);
        if (!loader) {
            loader = LoadLibraryExW((dir + L"\\..\\third_party\\webview2\\x64\\WebView2Loader.dll").c_str(), nullptr, flags);
        }
    }
    return loader ? reinterpret_cast<CreateEnvironment>(
        GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions")) : nullptr;
}

std::vector<BiliCookie> read_cookies(ICoreWebView2CookieList* list) {
    UINT count = 0;
    std::vector<BiliCookie> cookies;
    if (FAILED(list->get_Count(&count))) return cookies;
    for (UINT index = 0; index < count; ++index) {
        ComPtr<ICoreWebView2Cookie> cookie;
        CoString domain, path, name, value;
        BOOL secure = FALSE, http_only = FALSE, session = FALSE;
        double expires = 0;
        if (FAILED(list->GetValueAtIndex(index, &cookie)) || !cookie ||
            FAILED(cookie->get_Domain(&domain.value)) || !domain.value ||
            FAILED(cookie->get_Path(&path.value)) || !path.value ||
            FAILED(cookie->get_Name(&name.value)) || !name.value ||
            FAILED(cookie->get_Value(&value.value)) || !value.value ||
            FAILED(cookie->get_IsSecure(&secure)) || FAILED(cookie->get_IsHttpOnly(&http_only)) ||
            FAILED(cookie->get_IsSession(&session)) || (!session && FAILED(cookie->get_Expires(&expires)))) continue;

        std::wstring_view host(domain.value);
        if (!host.empty() && host.front() == L'.') host.remove_prefix(1);
        if (bilibili_host(host)) {
            cookies.push_back({domain.value, path.value, name.value, value.value,
                secure != FALSE, http_only != FALSE, expires});
        }
    }
    return cookies;
}

struct Verification {
    Cancellation cancel;
    std::optional<BiliSession> result;
    std::atomic<bool> done{false};
};

enum class Page { starting, loading, ready, failed, unavailable, closed };

class LoginDialog : public std::enable_shared_from_this<LoginDialog> {
public:
    LoginDialog(const std::wstring& profile, HANDLE* browser_process)
        : profile_(profile), browser_process_(browser_process) {}
    ~LoginDialog() { if (font_) DeleteObject(font_); }
    static INT_PTR CALLBACK dialog_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    std::optional<BiliSession> result;

private:
    std::wstring profile_;
    HANDLE* browser_process_;
    HWND window_ = nullptr, status_ = nullptr, host_ = nullptr, primary_ = nullptr, cancel_ = nullptr;
    HFONT font_ = nullptr;
    Page page_ = Page::starting;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webview_;
    ComPtr<ICoreWebView2CookieManager> cookie_manager_;
    std::shared_ptr<Verification> verification_;
    ULONGLONG page_deadline_ = 0, cookie_deadline_ = 0, next_check_ = 0, verify_deadline_ = 0;
    UINT64 navigation_id_ = 0;
    unsigned cookie_read_id_ = 0;
    bool manual_check_ = false, auto_check_ = true;

    void init(HWND window);
    int pixels(int size) const;
    RECT work_area() const;
    void update_font();
    void layout();
    void set_unavailable(const wchar_t* message);
    void set_page_failed(const wchar_t* message);
    void initialize_webview();
    void create_controller(HRESULT error, ICoreWebView2Environment* environment);
    void configure_controller(HRESULT error, ICoreWebView2Controller* controller);
    bool configure_webview();
    void navigate();
    void navigation_completed(ICoreWebView2NavigationCompletedEventArgs* args);
    void check_cookies(bool manual);
    void cookies_read(unsigned id, HRESULT error, ICoreWebView2CookieList* cookies);
    void start_verification(std::vector<BiliCookie> cookies);
    void verification_failed();
    void tick();
    void close();
    static LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR subclass, DWORD_PTR data);
};

int LoginDialog::pixels(int size) const {
    const UINT dpi = GetDpiForWindow(window_);
    return MulDiv(size, dpi ? static_cast<int>(dpi) : 96, 96);
}

RECT LoginDialog::work_area() const {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor)) return monitor.rcWork;
    return {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}

void LoginDialog::init(HWND window) {
    window_ = window;
    SetWindowTextW(window_, L"哔哩哔哩扫码登录");
    status_ = CreateWindowExW(0, L"STATIC", L"正在启动扫码登录…", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, nullptr, nullptr);
    host_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, window_, nullptr, nullptr, nullptr);
    primary_ = CreateWindowExW(0, L"BUTTON", L"检查登录(&L)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
    cancel_ = CreateWindowExW(0, L"BUTTON", L"取消(&C)", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
    if (!status_ || !host_ || !primary_ || !cancel_) {
        EndDialog(window_, 0);
        return;
    }
    EnableWindow(host_, FALSE);
    SetWindowSubclass(host_, host_proc, 1, reinterpret_cast<DWORD_PTR>(this));
    update_font();
    const RECT area = work_area();
    const int width = std::min(pixels(900), static_cast<int>(area.right - area.left));
    const int height = std::min(pixels(670), static_cast<int>(area.bottom - area.top));
    SetWindowPos(window_, nullptr, area.left + (area.right - area.left - width) / 2,
        area.top + (area.bottom - area.top - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    layout();
    SetFocus(cancel_);
    if (!SetTimer(window_, login_timer, 250, nullptr) || !PostMessageW(window_, start_message, 0, 0)) {
        set_unavailable(runtime_error);
    }
}

void LoginDialog::update_font() {
    const auto font = CreateFontW(-pixels(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (font) {
        for (HWND control : {status_, primary_, cancel_}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (font_) DeleteObject(font_);
        font_ = font;
    }
}

void LoginDialog::layout() {
    if (!status_ || !host_ || !primary_ || !cancel_) return;
    RECT area{};
    GetClientRect(window_, &area);
    const int margin = pixels(14), status_height = pixels(40), button_height = pixels(29);
    const int primary_width = pixels(125), cancel_width = pixels(84), gap = pixels(8);
    const int button_top = std::max(static_cast<int>(area.bottom) - margin - button_height, 0);
    const int host_top = margin + status_height;
    MoveWindow(status_, margin, margin, std::max(static_cast<int>(area.right) - 2 * margin, 0), status_height, TRUE);
    MoveWindow(host_, margin, host_top, std::max(static_cast<int>(area.right) - 2 * margin, 0),
        std::max(button_top - margin - host_top, 0), TRUE);
    MoveWindow(primary_, area.right - margin - cancel_width - gap - primary_width, button_top, primary_width, button_height, TRUE);
    MoveWindow(cancel_, area.right - margin - cancel_width, button_top, cancel_width, button_height, TRUE);
    if (controller_) {
        RECT bounds{};
        GetClientRect(host_, &bounds);
        controller_->put_Bounds(bounds);
    }
}

void LoginDialog::set_unavailable(const wchar_t* message) {
    page_ = Page::unavailable;
    SetWindowTextW(status_, message);
    EnableWindow(primary_, FALSE);
}

void LoginDialog::set_page_failed(const wchar_t* message) {
    page_ = Page::failed;
    SetWindowTextW(status_, message);
    SetWindowTextW(primary_, L"重新加载(&R)");
    EnableWindow(primary_, TRUE);
}

void LoginDialog::initialize_webview() {
    page_deadline_ = GetTickCount64() + 25000;
    const auto create = load_webview();
    if (!create) {
        set_unavailable(L"缺少 WebView2Loader.dll。请重新解压完整的应用程序包。");
        return;
    }
    auto completed = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [weak = weak_from_this()](HRESULT error, ICoreWebView2Environment* environment) -> HRESULT {
            if (auto dialog = weak.lock()) dialog->create_controller(error, environment);
            return S_OK;
        });
    if (!completed || FAILED(create(nullptr, profile_.c_str(), nullptr, completed.Get()))) set_unavailable(runtime_error);
}

void LoginDialog::create_controller(HRESULT error, ICoreWebView2Environment* environment) {
    if (page_ != Page::starting) return;
    ComPtr<ICoreWebView2Environment> base(environment);
    ComPtr<ICoreWebView2Environment10> settings;
    ComPtr<ICoreWebView2ControllerOptions> options;
    CoString data_folder;
    if (FAILED(error) || !environment || FAILED(base.As(&settings)) ||
        FAILED(settings->get_UserDataFolder(&data_folder.value)) || !data_folder.value ||
        !equal_ignore_case(profile_, data_folder.value) ||
        FAILED(settings->CreateCoreWebView2ControllerOptions(&options)) ||
        FAILED(options->put_IsInPrivateModeEnabled(TRUE))) {
        set_unavailable(runtime_error);
        return;
    }
    auto completed = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [weak = weak_from_this()](HRESULT status, ICoreWebView2Controller* controller) -> HRESULT {
            if (auto dialog = weak.lock()) {
                dialog->configure_controller(status, controller);
            } else if (controller) {
                controller->Close();
            }
            return S_OK;
        });
    if (!completed || FAILED(settings->CreateCoreWebView2ControllerWithOptions(host_, options.Get(), completed.Get()))) {
        set_unavailable(runtime_error);
    }
}

void LoginDialog::configure_controller(HRESULT error, ICoreWebView2Controller* controller) {
    if (page_ != Page::starting) {
        if (controller) controller->Close();
        return;
    }
    controller_ = controller;
    if (controller_ && SUCCEEDED(controller_->get_CoreWebView2(&webview_)) && browser_process_) {
        UINT pid = 0;
        if (SUCCEEDED(webview_->get_BrowserProcessId(&pid)) && pid) {
            *browser_process_ = OpenProcess(SYNCHRONIZE, FALSE, pid);
        }
    }
    if (FAILED(error) || !webview_ || !configure_webview()) {
        set_unavailable(runtime_error);
        return;
    }
    EnableWindow(host_, TRUE);
    layout();
    navigate();
}

bool LoginDialog::configure_webview() {
    ComPtr<ICoreWebView2Settings> settings;
    ComPtr<ICoreWebView2Settings4> form_settings;
    ComPtr<ICoreWebView2_18> browser;
    ComPtr<ICoreWebView2Profile> profile;
    BOOL private_mode = FALSE;
    if (FAILED(webview_.As(&browser)) || FAILED(browser->get_Profile(&profile)) ||
        FAILED(profile->get_IsInPrivateModeEnabled(&private_mode)) || !private_mode ||
        FAILED(webview_->get_Settings(&settings)) || FAILED(settings.As(&form_settings)) ||
        FAILED(settings->put_AreDevToolsEnabled(FALSE)) || FAILED(settings->put_AreDefaultContextMenusEnabled(FALSE)) ||
        FAILED(settings->put_AreHostObjectsAllowed(FALSE)) || FAILED(settings->put_IsWebMessageEnabled(FALSE)) ||
        FAILED(form_settings->put_IsPasswordAutosaveEnabled(FALSE)) || FAILED(form_settings->put_IsGeneralAutofillEnabled(FALSE)) ||
        FAILED(browser->get_CookieManager(&cookie_manager_))) return false;

    // Closing the controller releases all of these registrations.
    EventRegistrationToken token{};
    const auto weak = weak_from_this();
    return SUCCEEDED(webview_->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
        [weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            CoString uri;
            if (FAILED(args->get_Uri(&uri.value)) || !official_https(uri.value)) return args->put_Cancel(TRUE);
            if (auto dialog = weak.lock(); dialog && dialog->page_ == Page::loading && !dialog->navigation_id_) {
                args->get_NavigationId(&dialog->navigation_id_);
            }
            return S_OK;
        }).Get(), &token)) &&
        SUCCEEDED(webview_->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
            if (auto dialog = weak.lock()) dialog->navigation_completed(args);
            return S_OK;
        }).Get(), &token)) &&
        SUCCEEDED(webview_->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
            return args->put_Handled(TRUE);
        }).Get(), &token)) &&
        SUCCEEDED(browser->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>(
        [](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
            return args->put_Cancel(TRUE);
        }).Get(), &token)) &&
        SUCCEEDED(browser->add_LaunchingExternalUriScheme(Callback<ICoreWebView2LaunchingExternalUriSchemeEventHandler>(
        [](ICoreWebView2*, ICoreWebView2LaunchingExternalUriSchemeEventArgs* args) -> HRESULT {
            return args->put_Cancel(TRUE);
        }).Get(), &token)) &&
        SUCCEEDED(webview_->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
        [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
            return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
        }).Get(), &token)) &&
        SUCCEEDED(controller_->add_MoveFocusRequested(Callback<ICoreWebView2MoveFocusRequestedEventHandler>(
        [weak](ICoreWebView2Controller*, ICoreWebView2MoveFocusRequestedEventArgs* args) -> HRESULT {
            if (auto dialog = weak.lock(); dialog && dialog->window_) {
                COREWEBVIEW2_MOVE_FOCUS_REASON reason{};
                if (SUCCEEDED(args->get_Reason(&reason))) {
                    const bool forward = reason == COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT;
                    SetFocus(forward && IsWindowEnabled(dialog->primary_) ? dialog->primary_ : dialog->cancel_);
                    args->put_Handled(TRUE);
                }
            }
            return S_OK;
        }).Get(), &token));
}

void LoginDialog::navigate() {
    page_ = Page::loading;
    navigation_id_ = 0;
    page_deadline_ = GetTickCount64() + 25000;
    EnableWindow(primary_, FALSE);
    SetWindowTextW(status_, L"正在打开哔哩哔哩官方登录页…");
    if (FAILED(webview_->Navigate(login_url))) set_page_failed(L"无法打开 B 站登录页。请检查网络后重试。");
}

void LoginDialog::navigation_completed(ICoreWebView2NavigationCompletedEventArgs* args) {
    UINT64 id = 0;
    if (page_ != Page::loading || !navigation_id_ || FAILED(args->get_NavigationId(&id)) || id != navigation_id_) return;
    BOOL success = FALSE;
    if (FAILED(args->get_IsSuccess(&success)) || !success) {
        set_page_failed(L"无法打开 B 站登录页。请检查网络后重试。");
        return;
    }
    page_ = Page::ready;
    SetWindowTextW(status_, scan_prompt);
    SetWindowTextW(primary_, L"检查登录(&L)");
    EnableWindow(primary_, TRUE);
}

void LoginDialog::check_cookies(bool manual) {
    if (page_ != Page::ready || verification_) return;
    if (manual) manual_check_ = true;
    if (cookie_deadline_) return;
    const auto now = GetTickCount64();
    cookie_deadline_ = now + 5000;
    next_check_ = now + 1500;
    const auto id = ++cookie_read_id_;
    auto handler = Callback<ICoreWebView2GetCookiesCompletedHandler>(
        [weak = weak_from_this(), id](HRESULT error, ICoreWebView2CookieList* list) -> HRESULT {
            if (auto dialog = weak.lock()) dialog->cookies_read(id, error, list);
            return S_OK;
        });
    if (!handler || FAILED(cookie_manager_->GetCookies(cookie_url, handler.Get()))) cookies_read(id, E_FAIL, nullptr);
}

void LoginDialog::cookies_read(unsigned id, HRESULT error, ICoreWebView2CookieList* list) {
    if (page_ != Page::ready || id != cookie_read_id_) return;
    cookie_deadline_ = 0;
    const bool manual = std::exchange(manual_check_, false);
    if (FAILED(error) || !list) {
        if (manual) verification_failed();
        return;
    }
    try {
        auto cookies = read_cookies(list);
        const bool logged_in = std::any_of(cookies.begin(), cookies.end(), [](const BiliCookie& cookie) {
            return cookie.name == L"SESSDATA" && !cookie.value.empty();
        });
        if (logged_in) {
            start_verification(std::move(cookies));
        } else if (manual) {
            SetWindowTextW(status_, L"尚未检测到登录。请在手机上确认登录，然后再试一次。");
        }
    } catch (...) {
        verification_failed();
    }
}

void LoginDialog::start_verification(std::vector<BiliCookie> cookies) {
    auto_check_ = false;
    auto job = std::make_shared<Verification>();
    std::thread([job, cookies = std::move(cookies)]() mutable {
        try {
            job->result = verify_bili_session(std::move(cookies), job->cancel);
        } catch (...) {
        }
        job->done.store(true, std::memory_order_release);
    }).detach();
    verification_ = std::move(job);
    verify_deadline_ = GetTickCount64() + 20000;
    SetWindowTextW(status_, L"正在验证 B 站登录…");
    EnableWindow(primary_, FALSE);
}

void LoginDialog::verification_failed() {
    auto_check_ = false;
    SetWindowTextW(status_, verify_error);
    EnableWindow(primary_, TRUE);
}

void LoginDialog::tick() {
    const auto now = GetTickCount64();
    if (page_ == Page::starting && page_deadline_ && now >= page_deadline_) {
        set_unavailable(L"WebView2 启动超时。请关闭此窗口后重试。");
    } else if (page_ == Page::loading && now >= page_deadline_) {
        set_page_failed(L"B 站登录页加载超时。请检查网络后重试。");
        webview_->Stop();
    }
    if (page_ != Page::ready) return;
    if (verification_) {
        if (verification_->done.load(std::memory_order_acquire)) {
            result = std::move(verification_->result);
            verification_.reset();
            if (result) {
                EndDialog(window_, IDOK);
            } else {
                verification_failed();
            }
        } else if (now >= verify_deadline_) {
            verification_->cancel.cancel();
            verification_.reset();
            verification_failed();
        }
        return;
    }
    if (cookie_deadline_ && now >= cookie_deadline_) {
        ++cookie_read_id_;
        cookie_deadline_ = 0;
        if (std::exchange(manual_check_, false)) verification_failed();
    }
    if (auto_check_ && now >= next_check_) check_cookies(false);
}

void LoginDialog::close() {
    page_ = Page::closed;
    if (verification_) verification_->cancel.cancel();
    if (window_) KillTimer(window_, login_timer);
    window_ = nullptr;
    if (controller_) controller_->Close();
    cookie_manager_.Reset();
    webview_.Reset();
    controller_.Reset();
}

LRESULT CALLBACK LoginDialog::host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass, DWORD_PTR data) {
    auto* dialog = reinterpret_cast<LoginDialog*>(data);
    if (message == WM_GETDLGCODE) return 0;
    if (message == WM_SETFOCUS && dialog->controller_) {
        const auto reason = GetKeyState(VK_SHIFT) < 0 ? COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS : COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT;
        dialog->controller_->MoveFocus(reason);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, host_proc, subclass);
    return DefSubclassProc(window, message, wparam, lparam);
}

INT_PTR CALLBACK LoginDialog::dialog_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_INITDIALOG) {
        auto* dialog = reinterpret_cast<LoginDialog*>(lparam);
        SetWindowLongPtrW(window, DWLP_USER, lparam);
        dialog->init(window);
        return FALSE;
    }
    auto* dialog = reinterpret_cast<LoginDialog*>(GetWindowLongPtrW(window, DWLP_USER));
    if (!dialog) return FALSE;
    switch (message) {
    case start_message:
        if (dialog->page_ == Page::starting) dialog->initialize_webview();
        return TRUE;
    case WM_TIMER:
        if (wparam != login_timer) return FALSE;
        dialog->tick();
        return TRUE;
    case WM_SIZE:
        dialog->layout();
        return TRUE;
    case WM_MOVE:
        if (dialog->controller_) dialog->controller_->NotifyParentWindowPositionChanged();
        return TRUE;
    case WM_DPICHANGED: {
        const auto& rect = *reinterpret_cast<RECT*>(lparam);
        dialog->update_font();
        SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
    }
    case WM_GETMINMAXINFO: {
        auto& limits = *reinterpret_cast<MINMAXINFO*>(lparam);
        const RECT area = dialog->work_area();
        limits.ptMinTrackSize.x = std::min<LONG>(dialog->pixels(610), area.right - area.left);
        limits.ptMinTrackSize.y = std::min<LONG>(dialog->pixels(455), area.bottom - area.top);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK) {
            if (dialog->page_ == Page::failed) dialog->navigate();
            else dialog->check_cookies(true);
            return TRUE;
        }
        if (LOWORD(wparam) != IDCANCEL) return FALSE;
        [[fallthrough]];
    case WM_CLOSE:
        EndDialog(window, IDCANCEL);
        return TRUE;
    case WM_DESTROY:
        dialog->close();
        return TRUE;
    default:
        return FALSE;
    }
}
}

std::optional<BiliSession> bili_login_dialog(HWND owner, const std::wstring& profile_directory, HANDLE* browser_process) {
    if (browser_process) *browser_process = nullptr;
    if (profile_directory.empty()) return std::nullopt;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) {
        MessageBoxW(owner, runtime_error, L"哔哩哔哩扫码登录", MB_OK | MB_ICONINFORMATION);
        return std::nullopt;
    }
    struct Uninitialize { ~Uninitialize() { CoUninitialize(); } } uninitialize;

    const auto dialog = std::make_shared<LoginDialog>(profile_directory, browser_process);
    struct alignas(DWORD) DialogTemplate {
        DLGTEMPLATE header;
        WORD menu, window_class, title;
    } dialog_template{{WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | DS_MODALFRAME | DS_CENTER,
                       0, 0, 0, 0, 450, 320}, 0, 0, 0};
    const auto outcome = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &dialog_template.header,
        owner, LoginDialog::dialog_proc, reinterpret_cast<LPARAM>(dialog.get()));
    if (outcome == -1) MessageBoxW(owner, L"无法打开登录窗口。", L"哔哩哔哩扫码登录", MB_OK | MB_ICONINFORMATION);
    return outcome == IDOK ? std::move(dialog->result) : std::nullopt;
}
}
