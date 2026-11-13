#include "platform.h"
#include "source.h"
#include "player.h"
#include "diagnostics.h"
#include "extraction.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <ctime>
#include <cmath>
#include <algorithm>
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
        bool preparedNetwork=argc>1 && std::string(argv[1])=="--prepared-network";
        if(argc<2 || (std::string(argv[1])!="--network" && !preparedNetwork)) return 0;
        Cancellation token;
        auto ref=parse_video_url(argc>2?argv[2]:"https://www.bilibili.com/video/BV1BMgj6MEz4/?p=2");
        double initial=argc>3?std::stod(argv[3]):10;
        double playSeconds=argc>4?std::stod(argv[4]):0.5;
        double speed=argc>5?std::stod(argv[5]):1.0;
        auto guide=fetch_guide(ref,token);
        require(!guide.parts.empty(),"actual guide has playable parts");
        if(argc<=2)require(guide.parts.size()==31,"actual guide has 31 P");
        auto yt=executable_dir()+L"\\..\\third_party\\yt-dlp.exe";
        HWND window=CreateWindowExW(0,L"STATIC",L"Media integration",WS_OVERLAPPEDWINDOW,50,50,480,300,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window!=nullptr,"test video window");
        ShowWindow(window,SW_SHOWNOACTIVATE);
        PlaybackDiagnostics diagnostics(executable_dir()+L"\\integration-data\\diagnostics");
        Player player;
        SourceOptions source;source.yt_dlp=yt;source.max_height=720;
        player.initialize(window,executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll",[]{},source,[&](const char* event,const json& fields){diagnostics.record(event,fields);});
        player.set_speed(speed);
        if(preparedNetwork){
            auto part=std::find_if(guide.parts.begin(),guide.parts.end(),[&](const Part& p){return p.page==(ref.page?ref.page:1);});require(part!=guide.parts.end(),"selected prepared page exists");
            ExtractionOptions options;options.executable=yt;auto started=GetTickCount64();auto media=extract_media(ref,*part,options,token);
            std::cout<<"Prepared: height="<<media.height<<" elapsed_ms="<<(GetTickCount64()-started)<<" peak_job_bytes="<<media.extraction_peak_job_bytes<<" processes="<<media.extraction_processes<<"\n";
            require(media.height==720 && media.extraction_peak_job_bytes>0 && media.extraction_processes>0,"real preparation preserves 720p and records transient process memory");
            player.load(media,initial,true,"prepared integration");
        }else player.load_url(ref.url(),initial,true,"integration");
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
        require(std::abs(s.speed-speed)<0.001,"requested speed applies before the first video frame");
        require(s.position>=initial-1 && s.position<=initial+1,"requested initial position applied");
        require(s.source_deadline>static_cast<long long>(std::time(nullptr)),"extract the selected stream expiry from the real ytdl hook");
        auto details=player.diagnostic_snapshot();
        require(details["values"]["video-params/h"].is_number() && details["status"]["time-pos"]==0,"online diagnostics retain supported values and statuses");
        diagnostics.record("source-context",{{"bvid",ref.bvid},{"page",ref.page},{"streams",player.diagnostic_source()}});
        diagnostics.record("snapshot",{{"snapshot",details}});
        std::cerr<<"Source deadline="<<s.source_deadline<<" hwdec="<<s.hwdec<<"\n";
        player.seek(5);
        auto seekDeadline=GetTickCount64()+5000;
        do {
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            Sleep(50);player.drain();s=player.snapshot();
        }while(GetTickCount64()<seekDeadline && (s.position<initial+4 || s.position>initial+6));
        std::cerr<<"Paused seek: pos="<<s.position<<" paused="<<s.paused<<" error="<<s.error<<"\n";
        require(s.paused && s.position>=initial+4 && s.position<=initial+6 && std::abs(s.speed-speed)<0.001,"paused seek retains pause and speed");
        player.set_pause(false); Sleep(2000);player.drain();s=player.snapshot();
        std::cerr<<"After blocked wait: pos="<<s.position<<" paused="<<s.paused<<" loaded="<<s.loaded<<" error="<<s.error<<"\n";
        auto playDeadline=GetTickCount64()+static_cast<ULONGLONG>((playSeconds+15)*1000);
        while(GetTickCount64()<playDeadline && s.position<=initial+5+playSeconds) {
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            Sleep(50);player.drain();s=player.snapshot();
        }
        std::cerr<<"After message pump: pos="<<s.position<<" paused="<<s.paused<<" error="<<s.error<<"\n";
        require(!s.paused && s.position>initial+5+playSeconds,"playback clock advances");
        for(double rate:{0.5,2.0,1.0}){
            player.set_pause(true);s=player.snapshot();auto entry=s.entry_id;double position=s.position;
            player.set_speed(rate);s=player.snapshot();
            require(s.paused && !s.seeking && s.entry_id==entry && std::abs(s.position-position)<0.3 && std::abs(s.speed-rate)<0.001,"online speed change preserves pause, entry and position");
            player.set_pause(false);auto until=GetTickCount64()+12000;
            do{MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(20);player.drain();s=player.snapshot();}while(GetTickCount64()<until && s.position<position+1.5);
            require(s.loaded && s.error.empty() && s.height>0 && !s.audio_codec.empty() && s.position>=position+1.5,"audio/video continue after live speed change");
            std::cerr<<"Online speed="<<s.speed<<" pos="<<s.position<<" height="<<s.height<<" audio="<<s.audio_codec<<"\n";
        }
        std::cout<<"PASS online decode "<<s.height<<"p, audio="<<s.audio_codec<<", pos="<<s.position<<"\n";
        auto captureBefore=player.snapshot();auto report=diagnostics.save_report({{"snapshot",player.diagnostic_snapshot()},{"streams",player.diagnostic_source()}});auto captureAfter=player.snapshot();
        require(!report.empty() && captureBefore.entry_id==captureAfter.entry_id && captureBefore.paused==captureAfter.paused,"online diagnostic export preserves active media");
        std::cout<<"PASS online diagnostic capture\n";
        player.shutdown();DestroyWindow(window);
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
