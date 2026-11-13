#include "extraction.h"
#include "diagnostics.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <limits>
namespace gv {
namespace {
// None of the errors below interpolate argv, stderr, JSON, URLs or cookie paths.
[[noreturn]]void invalid_media(){throw std::runtime_error("后台解析返回的视频流无效，请手动重试");}
class Handle {
    HANDLE value_=nullptr;
public:
    Handle()=default;explicit Handle(HANDLE value):value_(value){}
    ~Handle(){if(value_ && value_!=INVALID_HANDLE_VALUE)CloseHandle(value_);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    HANDLE get()const{return value_;}
    void set(HANDLE value){if(value_ && value_!=INVALID_HANDLE_VALUE)CloseHandle(value_);value_=value;}
};
std::wstring quote_argument(const std::wstring& value){
    if(value.find(L'\0')!=std::wstring::npos)throw std::runtime_error("后台解析参数无效");
    std::wstring result=L"\"";size_t slashes=0;
    for(wchar_t ch:value){
        if(ch==L'\\'){++slashes;continue;}
        if(ch==L'\"'){result.append(slashes*2+1,L'\\');result+=ch;}
        else{result.append(slashes,L'\\');result+=ch;}
        slashes=0;
    }
    result.append(slashes*2,L'\\');result+=L'\"';return result;
}
struct ProcessTree {
    Handle job,process,thread;
    ~ProcessTree(){if(job.get()){TerminateJobObject(job.get(),1);if(process.get())WaitForSingleObject(process.get(),1000);}}
    void stop(){if(job.get())TerminateJobObject(job.get(),1);if(process.get())WaitForSingleObject(process.get(),1000);}
};
struct AttributeList {
    std::vector<unsigned char> storage;LPPROC_THREAD_ATTRIBUTE_LIST value=nullptr;
    explicit AttributeList(const std::vector<HANDLE>& handles){
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);storage.resize(bytes);
        value=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(value,1,0,&bytes)){value=nullptr;throw std::runtime_error("无法初始化后台解析进程");}
        if(!UpdateProcThreadAttribute(value,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,const_cast<HANDLE*>(handles.data()),handles.size()*sizeof(HANDLE),nullptr,nullptr)){DeleteProcThreadAttributeList(value);value=nullptr;throw std::runtime_error("无法限制后台解析进程句柄");}
    }
    ~AttributeList(){if(value)DeleteProcThreadAttributeList(value);}
};
bool drain(HANDLE pipe,std::string* output,size_t limit,bool truncate=false){
    // Work per pass is bounded so a continuous writer cannot starve cancellation.
    bool progress=false;
    for(int i=0;i<4;++i){
        DWORD available=0;
        if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr)){
            if(GetLastError()==ERROR_BROKEN_PIPE)return progress;
            throw std::runtime_error("读取后台解析结果失败");
        }
        if(!available)return progress;
        char buffer[16384];DWORD read=0;
        if(!ReadFile(pipe,buffer,std::min<DWORD>(available,sizeof(buffer)),&read,nullptr)){
            if(GetLastError()==ERROR_BROKEN_PIPE)return progress;
            throw std::runtime_error("读取后台解析结果失败");
        }
        progress=progress || read>0;if(output){if(read>limit-output->size() && !truncate)throw std::runtime_error("后台解析结果超过大小限制");output->append(buffer,std::min<size_t>(read,limit-output->size()));}
    }
    return true;
}
std::string lower(std::string value){for(char& ch:value)if(ch>='A' && ch<='Z')ch=static_cast<char>(ch-'A'+'a');return value;}
using Headers=std::map<std::string,std::string>;
void merge_headers(Headers& headers,const json& parent){
    const auto found=parent.find("http_headers");if(found==parent.end() || found->is_null())return;
    if(!found->is_object() || found->size()>128)invalid_media();
    Headers incoming;
    for(auto it=found->begin();it!=found->end();++it){
        const auto& name=it.key();if(name.empty() || name.size()>256 || !it->is_string())invalid_media();
        for(unsigned char ch:name)if(!((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') || (ch>='0' && ch<='9') || std::string("!#$%&'*+-.^_`|~").find(static_cast<char>(ch))!=std::string::npos))invalid_media();
        const auto value=it->get<std::string>();if(value.size()>16384)invalid_media();
        for(unsigned char ch:value)if(ch<32 || ch==127)invalid_media();
        const auto key=lower(name);auto duplicate=incoming.find(key);if(duplicate!=incoming.end() && duplicate->second!=value)invalid_media();incoming[key]=value;
    }
    for(const auto& header:incoming)headers[header.first]=header.second;
}
std::string media_url(const json& format){
    auto found=format.find("url");if(found==format.end() || !found->is_string())invalid_media();
    auto url=found->get<std::string>();if(url.empty() || url.size()>65536)invalid_media();
    for(unsigned char ch:url)if(ch<=32 || ch==127 || ch=='\\')invalid_media();
    auto value=wide(url);URL_COMPONENTS components={sizeof(components)};
    components.dwHostNameLength=components.dwUserNameLength=components.dwPasswordLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(value.c_str(),static_cast<DWORD>(value.size()),0,&components) ||
        (components.nScheme!=INTERNET_SCHEME_HTTP && components.nScheme!=INTERNET_SCHEME_HTTPS) ||
        !components.dwHostNameLength || components.dwUserNameLength || components.dwPasswordLength)invalid_media();
    return url;
}
std::string codec(const json& format,const char* key){
    auto it=format.find(key);if(it==format.end() || it->is_null())return "";if(!it->is_string())invalid_media();auto value=it->get<std::string>();
    if(value.size()>128)invalid_media();for(unsigned char ch:value)if(!((ch>='A' && ch<='Z') || (ch>='a' && ch<='z') || (ch>='0' && ch<='9') || ch=='.' || ch=='_' || ch=='-'))invalid_media();return value;
}
double numeric(const json& format,const char* key,bool required){
    auto it=format.find(key);if(it==format.end() || it->is_null()){if(required)invalid_media();return 0;}
    if(!it->is_number())invalid_media();double value=it->get<double>();if(!std::isfinite(value) || value<0)invalid_media();return value;
}
void identity(const json& info,const VideoRef& ref,const Part& part){
    if(ref.short_url || ref.bvid.size()!=12 || ref.bvid.substr(0,2)!="BV" || part.page<1 || part.cid<=0)invalid_media();
    auto id=info.find("id");if(id==info.end() || !id->is_string())invalid_media();
    const auto value=id->get<std::string>();
    // Pinned yt-dlp BiliBiliIE emits BV_pN, not CID. Its interactive branch emits
    // BV_CID. Captured Guide/Part identity plus exact P is therefore the normal
    // contract; verify explicit CID too when an extractor version exposes it.
    if(value!=ref.bvid+"_p"+std::to_string(part.page) &&
        !(part.page==1 && value==ref.bvid) && value!=ref.bvid+"_"+std::to_string(part.cid))invalid_media();
    auto cid=info.find("cid");if(cid!=info.end() && !cid->is_null()){
        if(cid->is_number_integer()){if(*cid!=part.cid)invalid_media();}
        else if(cid->is_string()){if(cid->get<std::string>()!=std::to_string(part.cid))invalid_media();}
        else invalid_media();
    }
}
}
ExtractionProcessResult run_extraction_process(const std::wstring& executable,const std::vector<std::wstring>& arguments,unsigned timeout_ms,size_t output_limit,Cancellation& cancel){
    cancel.check();if(executable.empty() || !timeout_ms || !output_limit || output_limit>64*1024*1024)throw std::runtime_error("后台解析设置无效");
    const auto started=GetTickCount64();std::wstring command=quote_argument(executable);
    for(const auto& arg:arguments){command+=L' ';command+=quote_argument(arg);}
    if(command.size()>=32767)throw std::runtime_error("后台解析参数过长");
    SECURITY_ATTRIBUTES security={sizeof(security),nullptr,TRUE};
    Handle outRead,outWrite,errRead,errWrite;HANDLE read=nullptr,write=nullptr;
    if(!CreatePipe(&read,&write,&security,0))throw std::runtime_error("无法创建后台解析输出");outRead.set(read);outWrite.set(write);
    if(!CreatePipe(&read,&write,&security,0))throw std::runtime_error("无法创建后台解析输出");errRead.set(read);errWrite.set(write);
    if(!SetHandleInformation(outRead.get(),HANDLE_FLAG_INHERIT,0) || !SetHandleInformation(errRead.get(),HANDLE_FLAG_INHERIT,0))throw std::runtime_error("无法隔离后台解析句柄");
    Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(input.get()==INVALID_HANDLE_VALUE)throw std::runtime_error("无法初始化后台解析输入");
    std::vector<HANDLE> inherited={outWrite.get(),errWrite.get(),input.get()};AttributeList attributes(inherited);
    ProcessTree tree;tree.job.set(CreateJobObjectW(nullptr,nullptr));if(!tree.job.get())throw std::runtime_error("无法隔离后台解析进程");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(tree.job.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("无法限制后台解析进程");
    STARTUPINFOEXW startup={};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.StartupInfo.wShowWindow=SW_HIDE;
    startup.StartupInfo.hStdInput=input.get();startup.StartupInfo.hStdOutput=outWrite.get();startup.StartupInfo.hStdError=errWrite.get();startup.lpAttributeList=attributes.value;
    PROCESS_INFORMATION process={};
    if(!CreateProcessW(executable.c_str(),&command[0],nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&process))throw std::runtime_error("无法启动后台解析，请检查 yt-dlp 依赖");
    tree.process.set(process.hProcess);tree.thread.set(process.hThread);
    if(!AssignProcessToJobObject(tree.job.get(),tree.process.get())){TerminateProcess(tree.process.get(),1);throw std::runtime_error("无法隔离后台解析进程树");}
    outWrite.set(nullptr);errWrite.set(nullptr);
    if(ResumeThread(tree.thread.get())==static_cast<DWORD>(-1))throw std::runtime_error("无法运行后台解析进程");
    tree.thread.set(nullptr);ExtractionProcessResult result;std::string errorEvidence;
    for(;;){
        cancel.check();if(GetTickCount64()-started>=timeout_ms)throw std::runtime_error("后台解析超时，请手动重试");
        const bool outputPending=drain(outRead.get(),&result.output,output_limit);const bool errorPending=drain(errRead.get(),&errorEvidence,32768,true);
        const DWORD wait=WaitForSingleObject(tree.process.get(),0);
        if(wait==WAIT_FAILED)throw std::runtime_error("等待后台解析失败");
        if(wait==WAIT_OBJECT_0 && !outputPending && !errorPending)break;
        if(wait!=WAIT_OBJECT_0 && !outputPending && !errorPending)Sleep(10);
    }
    cancel.check();DWORD code=1;if(!GetExitCodeProcess(tree.process.get(),&code) || code)throw ExtractionFailure(summarize_extraction_failure(code,errorEvidence));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION memory={};JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting={};
    if(QueryInformationJobObject(tree.job.get(),JobObjectExtendedLimitInformation,&memory,sizeof(memory),nullptr))result.peak_job_bytes=memory.PeakJobMemoryUsed;
    if(QueryInformationJobObject(tree.job.get(),JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr))result.processes=accounting.TotalProcesses;
    tree.stop();return result;
}
std::string extraction_format(int max_height){
    if(max_height!=360 && max_height!=480 && max_height!=720)throw std::runtime_error("画质上限无效");
    const auto bound="[height<="+std::to_string(max_height)+"][fps<=?"+json(MAX_VIDEO_FPS).dump()+"]";
    return "bestvideo"+bound+"[vcodec^=avc1]+bestaudio/bestvideo"+bound+"+bestaudio/best"+bound;
}
Media parse_extracted_media(const json& info,const VideoRef& ref,const Part& part,int max_height){
    extraction_format(max_height);if(!info.is_object())invalid_media();identity(info,ref,part);
    if(info.contains("entries") || (info.contains("_type") && info["_type"]!="video"))invalid_media();
    const json* video=nullptr;const json* audio=nullptr;
    const auto requested=info.find("requested_formats");
    if(requested!=info.end()){
        if(!requested->is_array() || requested->size()!=2)invalid_media();
        for(const auto& format:*requested){
            if(!format.is_object())invalid_media();
            const auto vcodec=codec(format,"vcodec"),acodec=codec(format,"acodec");
            if(!vcodec.empty() && vcodec!="none" && acodec=="none" && !video)video=&format;
            else if(vcodec=="none" && !acodec.empty() && acodec!="none" && !audio)audio=&format;
            else invalid_media();
        }
        if(!video || !audio)invalid_media();
    }else{
        if(codec(info,"vcodec").empty() || codec(info,"vcodec")=="none" || codec(info,"acodec").empty() || codec(info,"acodec")=="none")invalid_media();
        video=&info;
    }
    Media result;result.source="background_ytdl";
    const double height=numeric(*video,"height",true),fps=numeric(*video,"fps",false);
    if(height<=0 || height>max_height || std::floor(height)!=height || fps>MAX_VIDEO_FPS ||
        (video->contains("fps") && !(*video)["fps"].is_null() && fps==0))invalid_media();
    result.height=static_cast<int>(height);result.codec=codec(*video,"vcodec");result.video=media_url(*video);if(audio)result.audio=media_url(*audio);
    Headers common;merge_headers(common,info);Headers videoHeaders=common;merge_headers(videoHeaders,*video);
    if(audio){Headers audioHeaders=common;merge_headers(audioHeaders,*audio);if(videoHeaders!=audioHeaders)invalid_media();}
    size_t headerBytes=0;for(const auto& header:videoHeaders){headerBytes+=header.first.size()+header.second.size();if(headerBytes>65536)invalid_media();
        result.headers.emplace_back(header.first=="referer"?"Referer":header.first=="user-agent"?"User-Agent":header.first,header.second);
    }
    return result;
}
Media extract_media(const VideoRef& ref,const Part& part,const ExtractionOptions& options,Cancellation& cancel){
    cancel.check();const auto format=extraction_format(options.max_height);
    if(ref.short_url || part.page<1 || part.cid<=0)invalid_media();
    auto target=ref;target.page=part.page;auto canonical=parse_video_url(target.url());if(canonical.bvid!=ref.bvid || canonical.page!=part.page)invalid_media();
    std::vector<std::wstring> arguments={L"--ignore-config",L"--no-playlist",L"--dump-single-json",L"--skip-download",L"--no-warnings",L"--socket-timeout",L"10",L"--retries",L"0",L"--extractor-retries",L"0",L"--sub-langs",L"none",L"--encoding",L"utf-8",L"--format",wide(format)};
    if(!options.cookies.empty()){arguments.push_back(L"--cookies");arguments.push_back(options.cookies);}
    arguments.push_back(L"--");arguments.push_back(wide(canonical.url()));
    auto extracted=run_extraction_process(options.executable,arguments,options.timeout_ms,options.output_limit,cancel);cancel.check();
    json info;try{info=json::parse(extracted.output);}catch(const json::exception&){throw std::runtime_error("后台解析结果格式无效，请手动重试");}
    Media result;try{result=parse_extracted_media(info,ref,part,options.max_height);}catch(const json::exception&){invalid_media();}
    result.extraction_peak_job_bytes=extracted.peak_job_bytes;result.extraction_processes=extracted.processes;cancel.check();return result;
}
}
