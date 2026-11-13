#include "extraction.h"
#include <iostream>
#include <thread>
#include <limits>
#include <algorithm>
#include <fstream>
using namespace gv;
namespace {
int count=0;
void check(bool ok,const char* name){++count;if(!ok)throw std::runtime_error(name);}
template<class F>void rejects(F f,const char* name){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,name);}
const VideoRef ref={"BV1PyxQ6sEbH",4,false,""};
const Part part={4,43318379864,"test",648};
json fixture(){return {{"id","BV1PyxQ6sEbH_p4"},{"extractor_key","BiliBili"},
    {"http_headers",{{"Referer","https://www.bilibili.com/"},{"User-Agent","test"}}},
    {"formats",json::array({{{"url","https://cdn.test/unselected"},{"height",1080},{"vcodec","avc1"}}})},
    {"requested_formats",json::array({
        {{"url","https://cdn.test/selected-video?token=secret"},{"vcodec","avc1.64001f"},{"acodec","none"},{"height",720},{"fps",30}},
        {{"url","https://cdn.test/selected-audio"},{"vcodec","none"},{"acodec","mp4a"}}})}};}
void format_tests(){
    const std::vector<std::pair<int,std::string>> cases={
        {360,"bestvideo[height<=360][fps<=?30.1][vcodec^=avc1]+bestaudio/bestvideo[height<=360][fps<=?30.1]+bestaudio/best[height<=360][fps<=?30.1]"},
        {480,"bestvideo[height<=480][fps<=?30.1][vcodec^=avc1]+bestaudio/bestvideo[height<=480][fps<=?30.1]+bestaudio/best[height<=480][fps<=?30.1]"},
        {720,"bestvideo[height<=720][fps<=?30.1][vcodec^=avc1]+bestaudio/bestvideo[height<=720][fps<=?30.1]+bestaudio/best[height<=720][fps<=?30.1]"}
    };
    for(const auto& item:cases)check(extraction_format(item.first)==item.second,"native and background format selection allow nominal 30 FPS while retaining height codec and audio policy");
    rejects([]{extraction_format(1080);},"format selection never increases the maximum supported height");
}
void parser_tests(){
    auto data=fixture();auto media=parse_extracted_media(data,ref,part,720);
    check(media.video=="https://cdn.test/selected-video?token=secret" && media.audio=="https://cdn.test/selected-audio" && media.height==720,"selected requested formats survive without reselection");
    for(double fps:{30.0,30.002,30.1}){
        auto selected=fixture();selected["requested_formats"][0]["fps"]=fps;
        check(parse_extracted_media(selected,ref,part,720).video==media.video,"nominal 30 FPS including rounding tolerance is accepted");
    }
    for(double fps:{30.1001,31.0,60.0}){
        auto selected=fixture();selected["requested_formats"][0]["fps"]=fps;
        rejects([&]{parse_extracted_media(selected,ref,part,720);},"FPS above nominal 30 tolerance is rejected");
    }
    data=fixture();data["requested_formats"][0].erase("fps");
    check(parse_extracted_media(data,ref,part,720).video==media.video,"missing selected video FPS remains allowed");
    data["requested_formats"][0]["fps"]=nullptr;
    check(parse_extracted_media(data,ref,part,720).video==media.video,"unknown selected video FPS remains allowed");
    data=fixture();data["requested_formats"][1]["fps"]=60;
    check(parse_extracted_media(data,ref,part,720).audio==media.audio,"video FPS cap does not reject an audio-only format");
    data=fixture();
    auto expectedHeaders=std::vector<std::pair<std::string,std::string>>{{"referer","https://www.bilibili.com/"},{"user-agent","test"}};
    auto headers=media.headers;for(auto& header:headers)std::transform(header.first.begin(),header.first.end(),header.first.begin(),[](unsigned char ch){return static_cast<char>(tolower(ch));});std::sort(headers.begin(),headers.end());
    check(headers==expectedHeaders,"top-level headers are preserved for playback");
    data["requested_formats"][0]["http_headers"]={{"referer","https://override.test/"}};
    rejects([&]{parse_extracted_media(data,ref,part,720);},"different effective audio and video headers are rejected");
    data["requested_formats"][1]["http_headers"]={{"REFERER","https://override.test/"}};
    check(parse_extracted_media(data,ref,part,720).headers.size()==2,"case-insensitive per-format overrides merge with top-level headers");
    for(const auto& value:std::vector<json>{nullptr,json::array(),"oops",json::array({12}),json::array({fixture()["requested_formats"][0]})}){
        auto bad=fixture();bad["requested_formats"]=value;rejects([&]{parse_extracted_media(bad,ref,part,720);},"malformed requested formats cannot silently fall back to formats");
    }
    for(const auto& id:{"BV1PyxQ6sEbH_p3","BV1PyxQ6sEbH","BV1BMgj6MEz4_p4","BV1PyxQ6sEbH_p4_extra"}){auto bad=fixture();bad["id"]=id;rejects([&]{parse_extracted_media(bad,ref,part,720);},"reject wrong exact BVID or part");}
    data=fixture();data.erase("id");rejects([&]{parse_extracted_media(data,ref,part,720);},"missing identity cannot authorize playback");
    data=fixture();data["cid"]=part.cid+1;rejects([&]{parse_extracted_media(data,ref,part,720);},"explicit CID must match");
    data["cid"]=part.cid;check(!parse_extracted_media(data,ref,part,720).video.empty(),"matching explicit CID is accepted");
    data=fixture();data["id"]="BV1PyxQ6sEbH_43318379864";check(!parse_extracted_media(data,ref,part,720).video.empty(),"interactive extractor CID identity is supported");
    for(const auto& dimension:std::vector<json>{nullptr,"720",true,0,-1,721,720.5,std::numeric_limits<double>::infinity()}){auto bad=fixture();bad["requested_formats"][0]["height"]=dimension;rejects([&]{parse_extracted_media(bad,ref,part,720);},"height must be finite positive integral and within cap");}
    for(const auto& fps:std::vector<json>{"30",true,-1,0,31,std::numeric_limits<double>::quiet_NaN()}){auto bad=fixture();bad["requested_formats"][0]["fps"]=fps;rejects([&]{parse_extracted_media(bad,ref,part,720);},"known FPS must be finite valid and within cap");}
    data=fixture();data["requested_formats"][0]["vcodec"]="https://private.test/?token=secret";rejects([&]{parse_extracted_media(data,ref,part,720);},"codec metadata must be a bounded identifier suitable for diagnostics");
    for(const auto& url:{"file:///secret","ytdl://https://test/","https://user:pass@cdn.test/a","https://cdn.test/a\r\nInjected: 1","https:///bad","https://cdn.test/a b"}){auto bad=fixture();bad["requested_formats"][0]["url"]=url;rejects([&]{parse_extracted_media(bad,ref,part,720);},"only valid HTTP media URLs are accepted");}
    for(const auto& key:{"Bad Header","X-Test\r\nInjected",""}){auto bad=fixture();bad["http_headers"][key]="value";rejects([&]{parse_extracted_media(bad,ref,part,720);},"header names reject invalid token characters");}
    data=fixture();data["http_headers"]["Cookie"]="secret\r\nInjected: yes";rejects([&]{parse_extracted_media(data,ref,part,720);},"header values reject line injection");
    data=fixture();data["http_headers"]["Accept-Language"]="en-us,en;q=0.5";check(parse_extracted_media(data,ref,part,720).headers.size()==3,"valid comma-bearing authentication and language headers are preserved");
    data=fixture();data.erase("requested_formats");data.update({{"url","https://cdn.test/muxed"},{"vcodec","avc1"},{"acodec","aac"},{"height",360},{"fps",nullptr}});
    check(parse_extracted_media(data,ref,part,720).audio.empty(),"progressive selected top-level stream is supported");
    data["fps"]=30.002;
    check(parse_extracted_media(data,ref,part,720).video=="https://cdn.test/muxed","progressive nominal 30 FPS uses the same rounding tolerance");
}
int fixture_main(int argc,wchar_t** argv){
    const std::wstring mode=argv[2];
    if(mode==L"argv"){json args=json::array();for(int i=3;i<argc;++i)args.push_back(utf8(argv[i]));std::cout<<args.dump();return 0;}
    if(mode==L"sleep"){Sleep(10000);return 0;}
    if(mode==L"flood"){std::string block(16384,'x');for(int i=0;i<1024;++i){std::cout<<block;std::cout.flush();}return 0;}
    if(mode==L"stderr"){std::string block(16384,'x');for(int i=0;i<128;++i)std::cerr<<block;std::cout<<"ok";return 0;}
    if(mode==L"error"){std::cerr<<"https://cdn.test/private?token=secret Cookie: private";return 3;}
    if(mode==L"formaterror"){std::cerr<<"ERROR: Requested format is not available. Use --list-formats for available formats\nhttps://cdn.test/private?token=secret\nCookie: private";return 7;}
    if(mode==L"handle"){std::cout<<(SetEvent(reinterpret_cast<HANDLE>(std::stoull(argv[3])))?"inherited":"isolated");return 0;}
    if(mode==L"tree"){
        std::wstring command=L"\""+executable_path()+L"\" --fixture sleep";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
        if(!CreateProcessW(executable_path().c_str(),&command[0],nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return 4;
        {std::ofstream pidFile(argv[3]);pidFile<<process.dwProcessId;}
        std::cout<<process.dwProcessId<<std::endl;CloseHandle(process.hThread);CloseHandle(process.hProcess);
        if(argc>4 && std::wstring(argv[4])==L"flood"){std::string block(16384,'x');for(;;){std::cout<<block;std::cout.flush();}}
        Sleep(10000);return 0;
    }
    return 5;
}
void process_tests(){
    Cancellation cancel;
    std::cerr<<"CASE argv\n";
    auto result=run_extraction_process(executable_path(),{L"--fixture",L"argv",L"",L"two words",L"literal\"quote",L"C:\\目录 名\\",L"slashes\\\\\"quote",L"&%$()"},3000,4096,cancel);
    check(json::parse(result.output)==json::array({"","two words","literal\"quote",u8"C:\\目录 名\\","slashes\\\\\"quote","&%$()"}),"Windows argv round trips empty Unicode quotes slashes and metacharacters");
    check(result.processes>=1 && result.peak_job_bytes>0,"job records extraction process count and peak private memory");
    std::cerr<<"CASE stderr\n";
    result=run_extraction_process(executable_path(),{L"--fixture",L"stderr"},10000,1024,cancel);check(result.output=="ok","large stderr is drained without blocking or retaining it");
    auto begin=GetTickCount64();rejects([&]{run_extraction_process(executable_path(),{L"--fixture",L"sleep"},100,1024,cancel);},"wall timeout stops process");check(GetTickCount64()-begin<2500,"timeout terminates promptly");
    begin=GetTickCount64();rejects([&]{run_extraction_process(executable_path(),{L"--fixture",L"flood"},3000,1024,cancel);},"stdout overflow terminates process");check(GetTickCount64()-begin<2500,"output cap terminates promptly");
    bool cancelled=false;Cancellation stop;std::thread trigger([&]{Sleep(100);stop.cancel();});begin=GetTickCount64();try{run_extraction_process(executable_path(),{L"--fixture",L"sleep"},3000,1024,stop);}catch(const Cancelled&){cancelled=true;}trigger.join();check(cancelled && GetTickCount64()-begin<2500,"Cancellation kills a running process promptly");
    std::string message;try{run_extraction_process(executable_path(),{L"--fixture",L"error"},3000,1024,cancel);}catch(const std::exception& error){message=error.what();}check(!message.empty() && message.find("secret")==std::string::npos && message.find("https://")==std::string::npos,"extractor errors never expose stderr signed URLs or cookie values");
    bool detailedFailure=false;try{run_extraction_process(executable_path(),{L"--fixture",L"formaterror"},3000,1024,cancel);}catch(const ExtractionFailure& error){auto evidence=error.details.dump();detailedFailure=error.details.value("exit_code",0)==7 && error.details.value("reason",std::string())=="requested_format_unavailable" && evidence.find("secret")==std::string::npos && evidence.find("private")==std::string::npos && evidence.find("https://")==std::string::npos;}catch(const std::exception&){}
    check(detailedFailure,"process failures retain finite exit status and requested-format classification without raw stderr");
    SECURITY_ATTRIBUTES security={sizeof(security),nullptr,TRUE};HANDLE event=CreateEventW(&security,TRUE,FALSE,nullptr);check(event!=nullptr,"create inheritable event fixture");
    result=run_extraction_process(executable_path(),{L"--fixture",L"handle",std::to_wstring(reinterpret_cast<uintptr_t>(event))},3000,1024,cancel);
    bool isolated=result.output=="isolated" && WaitForSingleObject(event,0)==WAIT_TIMEOUT;CloseHandle(event);check(isolated,"unrelated inheritable handles are not inherited");
    for(int mode=0;mode<3;++mode){
        const auto pidPath=executable_dir()+L"\\extraction-child-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(mode)+L".pid";
        DeleteFileW(pidPath.c_str());Cancellation treeCancel;std::thread treeTrigger;
        if(mode==1)treeTrigger=std::thread([&]{Sleep(300);treeCancel.cancel();});
        bool rejected=false;try{run_extraction_process(executable_path(),{L"--fixture",L"tree",pidPath,mode==2?L"flood":L"wait"},mode==0?300:3000,1024,treeCancel);}catch(const std::exception&){rejected=true;}
        if(treeTrigger.joinable())treeTrigger.join();DWORD child=0;{std::ifstream pidFile(pidPath);pidFile>>child;}DeleteFileW(pidPath.c_str());
        check(rejected && child!=0,"process tree fixture reached a timeout cancellation or overflow");
        HANDLE childProcess=OpenProcess(SYNCHRONIZE,FALSE,child);bool stopped=!childProcess || WaitForSingleObject(childProcess,1500)==WAIT_OBJECT_0;if(childProcess)CloseHandle(childProcess);check(stopped,"timeout cancellation and stdout overflow terminate descendant processes");
    }
    ExtractionOptions options;options.executable=executable_path();options.cookies=L"C:\\目录 名\\cookies.txt";
    auto media=extract_media(ref,part,options,cancel);check(media.height==720 && media.source=="background_ytdl" && media.extraction_processes>=1,"extraction invokes bounded process with canonical single-part arguments and selected format");
}
}
int wmain(int argc,wchar_t** argv){try{
    if(argc>2 && std::wstring(argv[1])==L"--fixture")return fixture_main(argc,argv);
    if(argc>1 && std::wstring(argv[1])==L"--ignore-config"){
        std::vector<std::wstring> args;for(int i=1;i<argc;++i)args.emplace_back(argv[i]);
        const std::vector<std::wstring> expected={L"--ignore-config",L"--no-playlist",L"--dump-single-json",L"--skip-download",L"--no-warnings",L"--socket-timeout",L"10",L"--retries",L"0",L"--extractor-retries",L"0",L"--sub-langs",L"none",L"--encoding",L"utf-8",L"--format",L"bestvideo[height<=720][fps<=?30.1][vcodec^=avc1]+bestaudio/bestvideo[height<=720][fps<=?30.1]+bestaudio/best[height<=720][fps<=?30.1]",L"--cookies",L"C:\\目录 名\\cookies.txt",L"--",L"https://www.bilibili.com/video/BV1PyxQ6sEbH/?p=4"};
        if(args!=expected)return 9;auto selected=fixture();selected["requested_formats"][0]["fps"]=30.002;std::cout<<selected.dump();return 0;
    }
    if(argc==5 && std::wstring(argv[1])==L"--network"){
        auto networkRef=parse_video_url(utf8(argv[2]));Part networkPart{std::stoi(argv[3]),std::stoll(argv[4]),"",0};Cancellation cancel;ExtractionOptions options;options.executable=executable_dir()+L"\\..\\third_party\\yt-dlp.exe";
        const auto start=GetTickCount64();const auto media=extract_media(networkRef,networkPart,options,cancel);std::cout<<json({{"height",media.height},{"codec",media.codec},{"elapsed_ms",GetTickCount64()-start},{"peak_job_bytes",media.extraction_peak_job_bytes},{"processes",media.extraction_processes}}).dump()<<"\n";return 0;
    }
    format_tests();parser_tests();process_tests();std::cout<<"PASS "<<count<<" extraction assertions\n";return 0;
}catch(const ExtractionFailure& error){std::cerr<<"FAIL: "<<error.what()<<" "<<error.details.dump()<<"\n";return 1;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}}
