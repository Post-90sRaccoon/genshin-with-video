#include "diagnostics.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <thread>

namespace gv {
namespace {
namespace fs=std::filesystem;
constexpr size_t MAX_RECORD_BYTES=4096,MAX_HISTORY=2048,MAX_LOG_BYTES=2097152;
constexpr unsigned long long HISTORY_MILLISECONDS=10*60*1000;
std::atomic<unsigned long long> unique_id{0};
long long epoch_milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string lower(std::string value) {
    for(char& c:value) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
bool contains_any(const std::string& value,std::initializer_list<const char*> words) {
    for(auto word:words) if(value.find(word)!=std::string::npos) return true;
    return false;
}
bool safe_key(const std::string& key) {
    if(key=="video-params/h" || key=="raw-input-rate") return true; // Exact libmpv telemetry keys.
    if(key.empty() || key.size()>80) return false;
    std::string normalized;
    for(unsigned char c:key) {
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.')) return false;
        if(c!='_'&&c!='-'&&c!='.') normalized.push_back(static_cast<char>(std::tolower(c)));
    }
    return !contains_any(normalized,{"cookie","authorization","auth","header","url","path","stdout","stderr","token","raw","message","secret","credential","password"});
}
bool safe_string(const std::string& value) {
    if(value.size()>512) return false;
    for(unsigned char c:value) {
        // Conservative ASCII identifiers exclude titles, paths, URLs and encoded queries.
        if(c<32 || c>=127 || c=='/' || c=='\\' || c=='?' || c=='&' || c=='=' || c=='@' || c=='%') return false;
    }
    return !contains_any(lower(value),{"http:","https:","file:","www.","cookie:","authorization","bearer ","password:","token:"});
}
bool sanitize_value(const json& input,json& output,size_t depth,size_t& budget) {
    if(depth>8 || budget==0) return false;
    --budget;
    if(input.is_object()) {
        output=json::object();
        for(auto it=input.begin();it!=input.end() && budget;++it) {
            if(!safe_key(it.key())) continue;
            json child;if(sanitize_value(it.value(),child,depth+1,budget)) output[it.key()]=std::move(child);
        }
    } else if(input.is_array()) {
        output=json::array();
        for(const auto& item:input) {
            if(!budget) break;
            json child;if(sanitize_value(item,child,depth+1,budget)) output.push_back(std::move(child));
        }
    } else if(input.is_string()) {
        const auto& value=input.get_ref<const std::string&>();if(!safe_string(value)) return false;output=value;
    } else if(input.is_number_float()) {
        double value=input.get<double>();if(!std::isfinite(value)) {output=nullptr;} else output=value;
    } else if(input.is_number()||input.is_boolean()||input.is_null()) output=input;
    else return false;
    return true;
}
json sanitize(const json& input) {
    size_t budget=256;json output=json::object();sanitize_value(input,output,0,budget);return output;
}
std::string safe_event(const std::string& event) {
    if(!safe_key(event)) return "diagnostic-event";
    return event;
}
std::string dump(const json& value) {
    return value.dump(-1,' ',false,json::error_handler_t::replace);
}
std::string session_identifier() {
    return std::to_string(GetCurrentProcessId())+"-"+std::to_string(epoch_milliseconds())+"-"+std::to_string(++unique_id);
}
std::string safe_module(const std::string& input) {
    auto value=lower(input);
    if(value.empty() || value.size()>80) return "other";
    for(unsigned char c:value) if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='/'||c=='-'||c=='_'||c=='.')) return "other";
    auto separator=value.find('/');auto first=value.substr(0,separator);
    bool known=false;for(auto prefix:{"ffmpeg","cplayer","demux","demuxer","vd","ad","vo","ao","libmpv","stream","cache","network","curl","ytdl_hook","lavf"}) if(first==prefix) known=true;
    if(!known) return "other";
    std::replace(value.begin(),value.end(),'/','.');return value;
}
std::string summary_text(const std::string& text) {
    auto value=lower(text.substr(0,8192));
    // Interpret only the surrounding diagnostic wording. Credential-bearing spans
    // must not contribute apparent error codes or failure wording.
    static const std::regex headers(R"(\b(?:cookie|set-cookie|authorization|proxy-authorization|referer|referrer|user-agent)["']?\s*[:=][^\r\n]*)");
    static const std::regex urls(R"((?:[a-z][a-z0-9+.-]*://|www\.)[^\s<>"']*)");
    static const std::regex windows_paths(R"((?:[a-z]:[\\/]|\\\\)[^\r\n]*)");
    value=std::regex_replace(value,headers," ");
    value=std::regex_replace(value,urls," ");
    return std::regex_replace(value,windows_paths," ");
}
}

struct PlaybackDiagnostics::Impl {
    struct Pending {unsigned long long sequence;std::string line;};
    fs::path directory;
    size_t max_bytes,queue_limit;
    std::string session=session_identifier();
    mutable std::mutex mutex;
    std::mutex report_mutex;
    std::condition_variable ready,finished;
    std::deque<Pending> queue;
    std::deque<json> history;
    json source_context=json::object();
    unsigned long long enqueued=0,completed=0,dropped=0,write_failures=0;
    bool stopping=false,available=false;
    std::thread worker;

    Impl(std::wstring folder,size_t bytes,size_t limit):directory(std::move(folder)),max_bytes((std::min)(bytes,MAX_LOG_BYTES)),queue_limit((std::min)(limit,size_t(256))),worker([this]{run();}) {}
    ~Impl() {
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;}ready.notify_all();
        if(worker.joinable()) worker.join();
    }
    void prune_history(unsigned long long now) {
        while(!history.empty()) {
            auto earliest=history.front()["tick_ms"].get<unsigned long long>();
            if(history.size()<=MAX_HISTORY && (earliest>now || now-earliest<=HISTORY_MILLISECONDS)) break;
            history.pop_front();
        }
    }
    json health_locked() const {
        return {{"available",available},{"log_available",available},{"dropped_records",dropped},{"write_failures",write_failures},{"queued_records",queue.size()},{"history_records",history.size()},{"session_id",session},{"max_bytes",max_bytes},{"queue_limit",queue_limit}};
    }
    bool write_line(const std::string& line) noexcept {
        try {
            if(line.size()>max_bytes) return false;
            fs::create_directories(directory);
            auto current=directory/L"playback.jsonl";
            auto first=directory/L"playback.1.jsonl",second=directory/L"playback.2.jsonl";
            if(fs::exists(current) && fs::file_size(current)+line.size()>max_bytes) {
                if(fs::exists(second)) fs::remove(second);
                if(fs::exists(first)) fs::rename(first,second);
                fs::rename(current,first);
            }
            std::ofstream out(current,std::ios::binary|std::ios::app);if(!out) return false;
            out.write(line.data(),static_cast<std::streamsize>(line.size()));out.flush();return bool(out);
        } catch(...) {return false;}
    }
    void run() noexcept {
        for(;;) {
            Pending item;
            {std::unique_lock<std::mutex> lock(mutex);ready.wait(lock,[this]{return stopping||!queue.empty();});if(queue.empty()) return;item=std::move(queue.front());queue.pop_front();}
            bool success=write_line(item.line);
            {std::lock_guard<std::mutex> lock(mutex);completed=item.sequence;available=success;if(!success) ++write_failures;}
            finished.notify_all();
        }
    }
};

PlaybackDiagnostics::PlaybackDiagnostics(std::wstring directory,size_t max_bytes,size_t queue_limit):impl_(new Impl(std::move(directory),max_bytes,queue_limit)) {}
PlaybackDiagnostics::~PlaybackDiagnostics()=default;
void PlaybackDiagnostics::record(const std::string& event,const json& fields) noexcept {
    try {
        auto safe=sanitize(fields);auto tick=GetTickCount64();
        json row={{"schema",1},{"session_id",impl_->session},{"pid",GetCurrentProcessId()},{"utc_epoch",epoch_milliseconds()/1000},{"tick_ms",tick},{"event",safe_event(event)},{"fields",safe}};
        std::string line=dump(row)+"\n";
        if(line.size()>MAX_RECORD_BYTES || line.size()>impl_->max_bytes) {row["fields"]={{"truncated",true}};line=dump(row)+"\n";}
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if(event=="source-context"||event=="source_context") impl_->source_context=std::move(safe);
            impl_->history.push_back(std::move(row));impl_->prune_history(tick);
            if(impl_->stopping||line.size()>impl_->max_bytes||impl_->queue.size()>=impl_->queue_limit) {++impl_->dropped;return;}
            auto sequence=impl_->enqueued+1;impl_->queue.push_back({sequence,std::move(line)});impl_->enqueued=sequence;
        }
        impl_->ready.notify_one();
    } catch(...) {
        try {std::lock_guard<std::mutex> lock(impl_->mutex);++impl_->dropped;} catch(...) {}
    }
}
bool PlaybackDiagnostics::flush() noexcept {
    try {
        std::unique_lock<std::mutex> lock(impl_->mutex);auto target=impl_->enqueued;
        impl_->finished.wait(lock,[this,target]{return impl_->completed>=target;});return impl_->write_failures==0;
    } catch(...) {return false;}
}
json PlaybackDiagnostics::status() const {std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->health_locked();}
std::wstring PlaybackDiagnostics::save_report(const json& context) {
    fs::path temporary;
    try {
        std::lock_guard<std::mutex> export_lock(impl_->report_mutex);
        flush();json report;
        {std::lock_guard<std::mutex> lock(impl_->mutex);impl_->prune_history(GetTickCount64());report={{"schema",1},{"session_id",impl_->session},{"pid",GetCurrentProcessId()},{"utc_epoch",epoch_milliseconds()/1000},{"context",sanitize(context)},{"source_context",impl_->source_context},{"records",impl_->history},{"health",impl_->health_locked()}};}
        auto report_directory=impl_->directory/L"reports";fs::create_directories(report_directory);
        auto filename=L"playback-report-"+std::to_wstring(epoch_milliseconds())+L"-"+std::to_wstring(++unique_id)+L".json";
        auto destination=report_directory/filename;temporary=destination;temporary+=L".tmp";
        {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);if(!out) throw std::runtime_error("write");auto text=dump(report);out.write(text.data(),static_cast<std::streamsize>(text.size()));out.flush();if(!out) throw std::runtime_error("write");}
        std::vector<fs::path> reports;
        for(const auto& entry:fs::directory_iterator(report_directory)) {
            auto name=entry.path().filename().wstring();
            if(entry.is_regular_file() && name.rfind(L"playback-report-",0)==0 && entry.path().extension()==L".json") reports.push_back(entry.path());
        }
        std::sort(reports.begin(),reports.end(),[](const fs::path& a,const fs::path& b){auto at=fs::last_write_time(a),bt=fs::last_write_time(b);return at==bt?a.filename()<b.filename():at<bt;});
        // Make room before committing: a failed retention delete cannot silently leave a sixth report.
        while(reports.size()>=5) {fs::remove(reports.front());reports.erase(reports.begin());}
        fs::rename(temporary,destination);
        return fs::absolute(destination).wstring();
    } catch(...) {
        if(!temporary.empty()) {std::error_code ignored;fs::remove(temporary,ignored);}
        try {std::lock_guard<std::mutex> lock(impl_->mutex);++impl_->write_failures;} catch(...) {}
        throw std::runtime_error("Unable to save playback diagnostic report.");
    }
}

json summarize_mpv_log(const std::string& module,const std::string& level,const std::string& text) {
    auto safe=safe_module(module),severity=lower(level),message=summary_text(text);
    if(severity=="warning") severity="warn";
    if(severity!="fatal"&&severity!="error"&&severity!="warn"&&severity!="info"&&severity!="v"&&severity!="debug"&&severity!="trace") severity="unknown";
    // Signals are literal matches from a finite vocabulary, not a diagnosis of
    // the upstream cause. In particular partial/corrupt input can precede decode errors.
    json signals=json::array();
    auto signal=[&](const char* name,std::initializer_list<const char*> patterns) {if(contains_any(message,patterns)) signals.push_back(name);};
    signal("partial_file",{"partial file"});
    signal("packet_corrupt",{"packet corrupt","corrupt packet"});
    signal("invalid_nal",{"invalid nal"});
    signal("http_read_failure",{"http read error","http read failed","http read failure","recv failure","failure when receiving data from the peer"});
    signal("timeout",{"operation timed out","connection timed out","connection timeout","read timed out"});
    signal("connection_reset",{"connection reset","connection was reset"});
    signal("transfer_failure",{"transfer failed","recv failure","failure when receiving data from the peer"});
    signal("truncated_response",{"transfer closed with","bytes remaining to read","end of response with","response truncated","truncated response","transferred a partial file"});
    signal("retrying",{"retrying (#"});
    signal("gpu_device_lost",{"device removed","device lost","dxgi_error_device_removed","dxgi_error_device_reset"});
    signal("hardware_decode_failure",{"error while decoding frame (hardware decoding)","hardware decoding failed","hardware decode failed","failed to initialize hardware decoder","failed to open hardware decoder","could not copy back hardware decoded frame"});
    signal("requested_format_unavailable",{"requested format is not available","requested format not available"});
    signal("extractor_failure",{"youtube-dl failed","yt-dlp failed"});
    auto has_signal=[&](const char* name) {return std::find(signals.begin(),signals.end(),name)!=signals.end();};
    std::string category="other";
    if(safe=="curl" || safe.rfind("curl.",0)==0 || safe=="stream.curl" || has_signal("http_read_failure") || has_signal("timeout") || has_signal("connection_reset") || has_signal("transfer_failure") || has_signal("truncated_response")) category="network";
    else if(has_signal("gpu_device_lost")) category="gpu";
    else if(has_signal("hardware_decode_failure") || has_signal("invalid_nal")) category="decoder";
    else if(has_signal("requested_format_unavailable") || has_signal("extractor_failure")) category="extractor";
    else if(contains_any(message,{"http","network","socket","tls ","connection","timed out","timeout","broken pipe","read error"}) || contains_any(safe,{".https",".http","network"})) category="network";
    else if(contains_any(message,{"d3d11","direct3d","vulkan","opengl","gpu","device removed","device lost"}) || safe.rfind("vo.",0)==0) category="gpu";
    else if(contains_any(message,{"decod","avcodec","invalid nal"}) || safe.rfind("vd",0)==0 || safe=="ffmpeg.video") category="decoder";
    json output={{"module",safe},{"severity",severity},{"category",category},{"classification","heuristic"},{"signals",std::move(signals)}};
    static const std::regex status(R"(\bhttp(?:/[0-9](?:\.[0-9])?)?\s+(?:(?:error|status(?:\s+code)?)\s*[:=]?\s*)?([1-5][0-9]{2})\b)");
    std::smatch match;if(std::regex_search(message,match,status)) output["http_status"]=std::stoi(match[1].str());
    static const std::regex labelled_error(R"((?:^|[\s(,;])(?:hresult|error(?:\s+code)?)\s*[:=]?\s*\(?\s*(0x[0-9a-f]{1,8})\b)");
    json codes=json::array();for(std::sregex_iterator i(message.begin(),message.end(),labelled_error),end;i!=end && codes.size()<4;++i) {auto code=(*i)[1].str();if(std::find(codes.begin(),codes.end(),code)==codes.end()) codes.push_back(code);}
    if(!codes.empty()) output["error_codes"]=std::move(codes);
    static const std::regex partial_offset(R"(\boffset\s+(0x[0-9a-f]{1,16})\s*:\s*partial file\b)");
    if(std::regex_search(message,match,partial_offset)) output["file_offset"]=match[1].str();
    return output;
}
json summarize_extraction_failure(unsigned long exit_code,const std::string& stderr_text) {
    auto summary=summarize_mpv_log("ytdl_hook","error",stderr_text);
    const auto& signals=summary["signals"];
    std::string reason="unclassified";
    if(std::find(signals.begin(),signals.end(),"requested_format_unavailable")!=signals.end()) reason="requested_format_unavailable";
    else if(std::find(signals.begin(),signals.end(),"extractor_failure")!=signals.end()) reason="extractor_failure";
    // Copy only the finite signal vocabulary and optional explicit HTTP status.
    // Never propagate arbitrary error text, paths, offsets or labelled hex values.
    json output={{"exit_code",exit_code},{"reason",reason},{"signals",signals}};
    if(summary.contains("http_status")) output["http_status"]=summary["http_status"];
    return output;
}
std::string diagnostic_host(const std::string& url) {
    if(url.size()>32768) return {};
    auto scheme=url.find("://");if(scheme==std::string::npos) return {};
    auto protocol=lower(url.substr(0,scheme));if(protocol!="https"&&protocol!="http") return {};
    auto start=scheme+3,end=url.find_first_of("/?#",start);auto authority=url.substr(start,end==std::string::npos?std::string::npos:end-start);
    auto at=authority.rfind('@');if(at!=std::string::npos) authority.erase(0,at+1);
    std::string host;
    if(!authority.empty()&&authority.front()=='[') {auto bracket=authority.find(']');if(bracket==std::string::npos) return {};host=authority.substr(1,bracket-1);if(bracket+1<authority.size()&&authority[bracket+1]!=':') return {};}
    else {auto colon=authority.find(':');host=authority.substr(0,colon);}
    if(host.empty() || host.size()>253) return {};
    for(unsigned char c:host) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c==':')) return {};
    return lower(host);
}
}
