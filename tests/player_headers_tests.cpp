// Exercise Player's real loadfile options against the bundled native libmpv.
// The test-only export wrapper selects headless outputs and retains the handle
// for property inspection; no header values are printed or sent over a network.
#include "player.h"
#include "extraction.h"
#include "diagnostics.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
mpv_handle* native_handle=nullptr;
decltype(&mpv_initialize) native_initialize=nullptr;
decltype(&mpv_set_option_string) native_option=nullptr;
int headless_initialize(mpv_handle* handle) {
    native_handle=handle;
    for(const auto& option:std::vector<std::pair<const char*,const char*>>{
        {"vo","null"},{"ao","null"},{"force-window","no"},{"hwdec","no"}})
        if(native_option(handle,option.first,option.second)<0)return MPV_ERROR_OPTION_ERROR;
    return native_initialize(handle);
}
FARPROC test_get_proc_address(HMODULE library,LPCSTR name) {
    auto address=GetProcAddress(library,name);
    if(std::string(name)=="mpv_initialize") {
        native_initialize=reinterpret_cast<decltype(native_initialize)>(address);
        native_option=reinterpret_cast<decltype(native_option)>(GetProcAddress(library,"mpv_set_option_string"));
        return reinterpret_cast<FARPROC>(&headless_initialize);
    }
    return address;
}
}
#define GetProcAddress test_get_proc_address
#include "../src/player.cpp"
#undef GetProcAddress
using namespace gv;
namespace {
int assertions=0;
void check(bool condition,const char* message) {++assertions;if(!condition)throw std::runtime_error(message);}
struct ImageFixture {
    std::wstring path;
    ImageFixture() {
        path=executable_dir()+L"\\player-headers-"+std::to_wstring(GetCurrentProcessId())+L".ppm";
        std::ofstream out(path.c_str(),std::ios::binary);
        const char data[]="P6\n1 1\n255\n\0\0\0";
        out.write(data,sizeof(data)-1);
        check(static_cast<bool>(out),"write local header test media");
    }
    ~ImageFixture(){DeleteFileW(path.c_str());}
};
void wait_loaded(Player& player) {
    auto deadline=GetTickCount64()+5000;
    do {
        player.drain();auto state=player.snapshot();
        check(state.error.empty(),"header test media loads without a player error");
        if(state.loaded)return;
        Sleep(10);
    }while(GetTickCount64()<deadline);
    throw std::runtime_error("header test media reaches file-loaded");
}
std::vector<std::string> header_fields(HMODULE library) {
    auto get=reinterpret_cast<decltype(&mpv_get_property)>(GetProcAddress(library,"mpv_get_property"));
    auto release=reinterpret_cast<decltype(&mpv_free_node_contents)>(GetProcAddress(library,"mpv_free_node_contents"));
    mpv_node result={};
    check(get(native_handle,"http-header-fields",MPV_FORMAT_NODE,&result)>=0,"read native header list");
    std::vector<std::string> fields;
    if(result.format==MPV_FORMAT_NODE_ARRAY && result.u.list) {
        for(int i=0;i<result.u.list->num;++i) {
            if(result.u.list->values[i].format!=MPV_FORMAT_STRING){release(&result);throw std::runtime_error("native header list contains strings");}
            fields.push_back(result.u.list->values[i].u.string);
        }
    }else {release(&result);throw std::runtime_error("native header list has array format");}
    release(&result);return fields;
}
std::string option_text(HMODULE library,const char* name) {
    auto get=reinterpret_cast<decltype(&mpv_get_property)>(GetProcAddress(library,"mpv_get_property"));
    auto release=reinterpret_cast<decltype(&mpv_free)>(GetProcAddress(library,"mpv_free"));
    char* result=nullptr;check(get(native_handle,name,MPV_FORMAT_STRING,&result)>=0,"read native string option");
    std::string text=result?result:"";release(result);return text;
}
}
int main() {
    try {
        ImageFixture image;Player player;
        player.initialize(nullptr,executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll",[]{});
        auto library=GetModuleHandleW(L"libmpv-2.dll");check(library && native_handle,"capture real initialized libmpv handle");
        Media media;media.video=utf8(image.path);
        media.headers={{"Accept","text/html,application/json;q=0.9"},
            {"Accept-Language","zh-CN,zh;q=0.9,en;q=0.8"},
            {"X-Quoted",R"(token="a,b"; path="C:\temp\clip")"},
            {"X-Trailing",R"(backslash\)"},{"X-Last","last"},
            {"Referer","https://example.invalid/path?a=1,b=2"},
            {"User-Agent",R"(HeadersTest/1.0 "quoted" C:\bin)"}};
        const std::vector<std::string> expected={"Accept: text/html,application/json;q=0.9",
            "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8",R"(X-Quoted: token="a,b"; path="C:\temp\clip")",
            R"(X-Trailing: backslash\)","X-Last: last"};
        player.load(media,0,true,"header transfer");wait_loaded(player);
        check(header_fields(library)==expected,"all canonical headers survive loadfile with commas quotes and backslashes");
        check(option_text(library,"referrer")==media.headers[5].second,"canonical Referer maps to native referrer");
        check(option_text(library,"user-agent")==media.headers[6].second,"canonical User-Agent maps to native user-agent");
        media.headers[5].first="rEfErEr";media.headers[6].first="uSeR-aGeNt";
        player.load(media,0,true,"mixed case header transfer");wait_loaded(player);
        check(header_fields(library)==expected,"case variants route dedicated headers without duplicating custom fields");
        check(option_text(library,"referrer")==media.headers[5].second,"Referer routing is case insensitive");
        check(option_text(library,"user-agent")==media.headers[6].second,"User-Agent routing is case insensitive");
        media.headers={{"Accept-Language","en-US,en;q=0.5"},{"X-Second","fresh"}};
        player.load(media,0,true,"next header transfer");wait_loaded(player);
        check(header_fields(library)==std::vector<std::string>{"Accept-Language: en-US,en;q=0.5","X-Second: fresh"},
            "next source replaces all previous per-file custom headers");
        check(option_text(library,"referrer").empty(),"next source restores an absent referrer");
        media.headers.clear();player.load(media,0,true,"headerless source");wait_loaded(player);
        check(header_fields(library).empty(),"headerless source does not inherit prior custom headers");
        player.shutdown();std::cout<<"PASS "<<assertions<<" native player header assertions\n";
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}
}
