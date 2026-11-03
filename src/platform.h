#pragma once
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "core.h"
namespace gv {
std::wstring wide(const std::string& s);
std::string utf8(const std::wstring& s);
std::wstring executable_path();
std::wstring executable_dir();
void trace_event(const char* event,long long value=0);
struct Cancelled : std::runtime_error { Cancelled():std::runtime_error("操作已取消"){} };
class Cancellation {
    std::mutex mutex_; HINTERNET request_=nullptr;
public:
    std::atomic<bool> cancelled{false};
    void cancel(); void check() const;
    void attach(HINTERNET request); void release(HINTERNET request);
};
struct HttpResponse { unsigned status=0; std::string body; std::wstring location; };
HttpResponse http_get(const std::wstring& url,Cancellation& cancel,const std::wstring& cookie=L"");
class Store {
    std::wstring directory_;
public:
    json data; std::string warning;
    explicit Store(const std::wstring& directory);
    void save();
    json history(const std::string& bvid) const;
    void remember(const std::string& bvid,long long cid,double seconds);
};
}
