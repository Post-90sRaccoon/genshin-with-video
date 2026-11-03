#include "platform.h"
#include "source.h"
#include "player.h"
#include <iostream>
#include <thread>
#include <chrono>
using namespace gv;
void require(bool b,const char* s) {if(!b) throw std::runtime_error(s);}
int main(int argc,char** argv) {
    try {
        auto path=executable_dir()+L"\\integration-data";
        Store store(path);
        store.data["history"]["BVtest"]={{"last_cid",123},{"parts",{{"123",42.5}}}};
        store.save(); Store read(path);
        require(read.data["history"]["BVtest"]["parts"]["123"]==42.5,"atomic store roundtrip");
        Cancellation requestCancel;
        HINTERNET session=WinHttpOpen(L"GenshinVideo-tests",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
        require(session!=nullptr,"WinHTTP test session");
        requestCancel.attach(session);requestCancel.cancel();
        DWORD timeout=0,size=sizeof(timeout);
        bool stillOwned=WinHttpQueryOption(session,WINHTTP_OPTION_CONNECT_TIMEOUT,&timeout,&size)!=FALSE;
        requestCancel.release(session);
        require(stillOwned,"cancel must leave synchronous WinHTTP handle owned by worker");
        std::cout<<"PASS atomic storage and synchronous request ownership\n";
        if(argc<2 || std::string(argv[1])!="--network") return 0;
        Cancellation token;
        auto ref=parse_video_url("https://www.bilibili.com/video/BV1BMgj6MEz4/?p=2");
        auto guide=fetch_guide(ref,token);
        require(guide.parts.size()==31,"actual guide has 31 P");
        auto yt=executable_dir()+L"\\..\\third_party\\yt-dlp.exe";
        HWND window=CreateWindowExW(0,L"STATIC",L"Media integration",WS_OVERLAPPEDWINDOW,50,50,480,300,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window!=nullptr,"test video window");
        ShowWindow(window,SW_SHOWNOACTIVATE);
        Player player;
        SourceOptions source;source.yt_dlp=yt;source.max_height=720;
        player.initialize(window,executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll",[]{},source);
        player.load_url(ref.url(),10,true,"integration");
        auto deadline=GetTickCount64()+45000;
        Snapshot s;
        while(GetTickCount64()<deadline) {
            MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
            player.drain();s=player.snapshot();
            if(s.loaded && s.height>0 && !s.audio_codec.empty()) break;
            if(!s.error.empty()) throw std::runtime_error(s.error);
            Sleep(50);
        }
        require(s.loaded && s.height>0 && !s.audio_codec.empty(),"video and audio decode initialized");
        std::cerr<<"Loaded: pos="<<s.position<<" paused="<<s.paused<<" height="<<s.height<<" duration="<<s.duration<<" audio="<<s.audio_codec<<"\n";
        require(s.paused,"load paused");
        require(s.position>=9 && s.position<=11,"requested initial position applied");
        player.seek(5);
        auto seekDeadline=GetTickCount64()+5000;
        do {
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            Sleep(50);player.drain();s=player.snapshot();
        }while(GetTickCount64()<seekDeadline && (s.position<14 || s.position>16));
        std::cerr<<"Paused seek: pos="<<s.position<<" paused="<<s.paused<<" error="<<s.error<<"\n";
        require(s.paused && s.position>=14 && s.position<=16,"paused seek retains pause");
        player.set_pause(false); Sleep(2000);player.drain();s=player.snapshot();
        std::cerr<<"After blocked wait: pos="<<s.position<<" paused="<<s.paused<<" loaded="<<s.loaded<<" error="<<s.error<<"\n";
        auto playDeadline=GetTickCount64()+15000;
        while(GetTickCount64()<playDeadline && s.position<=15.5) {
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            Sleep(50);player.drain();s=player.snapshot();
        }
        std::cerr<<"After message pump: pos="<<s.position<<" paused="<<s.paused<<" error="<<s.error<<"\n";
        require(!s.paused && s.position>15.5,"playback clock advances");
        std::cout<<"PASS online decode "<<s.height<<"p, audio="<<s.audio_codec<<", pos="<<s.position<<"\n";
        player.shutdown();DestroyWindow(window);
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
