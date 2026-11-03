#include "platform.h"
#include <memory>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <ctime>
namespace gv {
std::wstring wide(const std::string& s) {
    if(s.empty()) return L"";
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n) throw std::runtime_error("无效的 UTF-8 文本");
    std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),&r[0],n);return r;
}
std::string utf8(const std::wstring& s) {
    if(s.empty()) return "";
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),&r[0],n,nullptr,nullptr);return r;
}
std::wstring executable_path() {std::wstring s(32768,0);DWORD n=GetModuleFileNameW(nullptr,&s[0],static_cast<DWORD>(s.size()));s.resize(n);return s;}
std::wstring executable_dir() {auto s=executable_path();return s.substr(0,s.find_last_of(L"\\/"));}
void trace_event(const char* event,long long value){
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    auto directory=executable_dir()+L"\\data";
    CreateDirectoryW(directory.c_str(),nullptr);
    auto path=directory+L"\\session.jsonl";
    std::ifstream previous(path,std::ios::binary|std::ios::ate);
    bool truncate=previous && previous.tellg()>1024*1024;previous.close();
    std::ofstream out(path,std::ios::binary|(truncate?std::ios::trunc:std::ios::app));
    if(out)out<<json({{"utc_epoch",static_cast<long long>(std::time(nullptr))},{"tick_ms",GetTickCount64()},{"pid",GetCurrentProcessId()},{"event",event},{"value",value}}).dump()<<"\n";
}
void Cancellation::check() const {if(cancelled.load()) throw Cancelled();}
// Synchronous WinHTTP handles are closed only by their owning worker.
// Closing one from the UI while WinHTTP is using it is explicitly unsafe.
void Cancellation::cancel() {cancelled.store(true);}
void Cancellation::attach(HINTERNET h) {std::lock_guard<std::mutex> l(mutex_);if(cancelled){WinHttpCloseHandle(h);throw Cancelled();}request_=h;}
void Cancellation::release(HINTERNET h) {std::lock_guard<std::mutex> l(mutex_);if(request_==h){WinHttpCloseHandle(h);request_=nullptr;}}
struct Internet {HINTERNET h=nullptr;~Internet(){if(h)WinHttpCloseHandle(h);}};
HttpResponse http_get(const std::wstring& url,Cancellation& cancel,const std::wstring& cookie) {
    const auto deadline=GetTickCount64()+30000;
    auto check_budget=[&]{cancel.check();if(GetTickCount64()>=deadline)throw std::runtime_error("B 站请求超时，请手动重试");};
    cancel.check();URL_COMPONENTS u={sizeof(u)};u.dwHostNameLength=static_cast<DWORD>(-1);u.dwUrlPathLength=static_cast<DWORD>(-1);u.dwExtraInfoLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&u) || u.nScheme!=INTERNET_SCHEME_HTTPS) throw std::runtime_error("仅支持安全的 HTTPS 请求");
    std::wstring host(u.lpszHostName,u.dwHostNameLength),path(u.lpszUrlPath,u.dwUrlPathLength);if(u.dwExtraInfoLength)path.append(u.lpszExtraInfo,u.dwExtraInfoLength);
    Internet session{WinHttpOpen(L"Mozilla/5.0 GenshinVideo/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.h) throw std::runtime_error("无法初始化网络");
    WinHttpSetTimeouts(session.h,2000,2000,5000,5000);
    Internet connection{WinHttpConnect(session.h,host.c_str(),u.nPort,0)};
    if(!connection.h) throw std::runtime_error("无法连接 B 站");
    HINTERNET request=WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,L"https://www.bilibili.com/",WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if(!request) throw std::runtime_error("无法创建网络请求");
    cancel.attach(request);
    struct Guard {Cancellation& c;HINTERNET h;~Guard(){c.release(h);}} guard{cancel,request};
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect));
    std::wstring headers=L"Accept: application/json, text/html\r\n";
    if(!cookie.empty()) headers+=L"Cookie: "+cookie+L"\r\n";
    check_budget();
    if(!WinHttpSendRequest(request,headers.c_str(),static_cast<DWORD>(headers.size()),WINHTTP_NO_REQUEST_DATA,0,0,0)) {
        cancel.check();throw std::runtime_error("B 站网络请求失败，请检查连接后重试");
    }
    check_budget();
    if(!WinHttpReceiveResponse(request,nullptr)) {
        cancel.check();throw std::runtime_error("B 站网络请求失败，请检查连接后重试");
    }
    HttpResponse r;DWORD size=sizeof(r.status);WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&r.status,&size,WINHTTP_NO_HEADER_INDEX);
    if(r.status>=300 && r.status<400) {
        size=0;WinHttpQueryHeaders(request,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&size,WINHTTP_NO_HEADER_INDEX);
        if(size>2 && size<32768) {std::wstring value(size/sizeof(wchar_t),0);if(WinHttpQueryHeaders(request,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,&value[0],&size,WINHTTP_NO_HEADER_INDEX)){value.resize(wcslen(value.c_str()));r.location=value;}}
        return r;
    }
    char buffer[16384];DWORD read=0;
    do {check_budget();if(!WinHttpReadData(request,buffer,sizeof(buffer),&read)){cancel.check();throw std::runtime_error("读取 B 站响应失败");}check_budget();r.body.append(buffer,read);if(r.body.size()>4*1024*1024) throw std::runtime_error("B 站响应超过大小限制");} while(read);
    return r;
}
Store::Store(const std::wstring& directory):directory_(directory) {
    data={{"settings",json::object()},{"history",json::object()}};
    std::ifstream in(directory_+L"\\state.json",std::ios::binary);
    if(!in) return;
    in.seekg(0,std::ios::end);if(in.tellg()>2*1024*1024){warning="配置过大，已使用默认设置";return;}in.seekg(0);
    try {json read;in>>read;if(!read.is_object() || !read.value("settings",json::object()).is_object() || !read.value("history",json::object()).is_object())throw std::runtime_error("type");data.update(read);} catch(...) {warning="配置文件损坏，已使用默认设置";CopyFileW((directory_+L"\\state.json").c_str(),(directory_+L"\\state.invalid.bak").c_str(),FALSE);}
}
void Store::save() {
    if(!CreateDirectoryW(directory_.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) throw std::runtime_error("无法创建 data 目录，请把整个便携目录放到可写位置");
    auto& hist=data["history"];
    while(hist.size()>100) {
        auto oldest=hist.begin();long long least=LLONG_MAX;
        for(auto it=hist.begin();it!=hist.end();++it){long long t=it.value().is_object() && it.value().contains("touched") && it.value()["touched"].is_number_integer()?it.value()["touched"].get<long long>():0;if(t<least){least=t;oldest=it;}}
        hist.erase(oldest);
    }
    auto target=directory_+L"\\state.json",temp=directory_+L"\\state.tmp";
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("无法保存进度，请检查目录写入权限");out<<data.dump(2);out.flush();if(!out)throw std::runtime_error("保存进度失败");}
    if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("无法更新进度文件");
}
json Store::history(const std::string& bvid) const {auto it=data["history"].find(bvid);return it!=data["history"].end() && it->is_object()?*it:json::object();}
void Store::remember(const std::string& bvid,long long cid,double seconds) {
    if(bvid.empty() || cid<=0 || !std::isfinite(seconds))return;
    auto& h=data["history"][bvid];if(!h.is_object())h=json::object();
    if(!h.contains("parts") || !h["parts"].is_object())h["parts"]=json::object();
    h["last_cid"]=cid;h["parts"][std::to_string(cid)]=std::max(0.0,seconds);h["touched"]=static_cast<long long>(std::time(nullptr));
}
}
