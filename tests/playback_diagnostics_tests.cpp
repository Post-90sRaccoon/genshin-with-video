#include "diagnostics.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace gv;
namespace fs=std::filesystem;
namespace {
int assertions=0;
void check(bool ok,const char* name) { ++assertions; if(!ok) throw std::runtime_error(name); }
std::string contents(const fs::path& file) { std::ifstream in(file,std::ios::binary); return {std::istreambuf_iterator<char>(in),{}}; }
std::vector<json> records(const fs::path& file) {
    std::ifstream in(file);std::string line;std::vector<json> out;
    while(std::getline(in,line)) {check(line.size()+1<=4096,"every JSONL record stays within 4KiB");out.push_back(json::parse(line));}
    return out;
}
struct TempDir {
    fs::path path;
    TempDir() {static unsigned counter=0;path=fs::temp_directory_path()/(L"gv-recorder-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++counter));fs::remove_all(path);fs::create_directories(path);}
    ~TempDir() {std::error_code ec;fs::remove_all(path,ec);}
};
void summaries_keep_only_safe_classification() {
    auto network=summarize_mpv_log("ffmpeg/https","warn","HTTP error 403 Forbidden HRESULT 0x80070005 https://cdn.test/video?token=topsecret Cookie: private");
    check(network.value("category","")=="network","HTTP warning categorized as network");
    check(network.value("http_status",0)==403,"explicit HTTP status retained");
    check(network.value("severity","")=="warn","severity normalized");
    check(network.dump().find("topsecret")==std::string::npos && network.dump().find("private")==std::string::npos,"raw mpv credentials never appear in summary");
    check(network.contains("error_codes") && network["error_codes"][0]=="0x80070005","explicit HRESULT retained");
    auto decoder=summarize_mpv_log("ffmpeg/video","error","Error while decoding stream 403");
    check(decoder["category"]=="decoder" && !decoder.contains("http_status"),"ordinary numeric decoder message is not HTTP status");
    check(summarize_mpv_log("vo/gpu-next","fatal","failed to initialize d3d11")["category"]=="gpu","GPU warning categorized");
    auto unknown=summarize_mpv_log("https://user:secret@private.invalid/","weird","unclassified secret text");
    check(unknown["module"]=="other" && unknown["severity"]=="unknown" && unknown["category"]=="other","unknown unsafe module and severity normalized");
    check(diagnostic_host("https://user:password@CN-CDN.example:443/video?token=secret")=="cn-cdn.example","hostname excludes credentials port path and query");
    check(diagnostic_host("https://[2001:db8::1]:443/path?q=secret")=="2001:db8::1","IPv6 hostname preserved without port");
    check(diagnostic_host("C:\\Users\\private\\clip.mp4").empty() && diagnostic_host("not a url").empty(),"local paths are not hosts");
}
void curl_failure_signals_keep_literal_distinctions() {
    struct Case {const char* text;json signals;};
    for(const auto& item:std::vector<Case>{
        {"transfer failed",{"transfer_failure"}},
        {"Recv failure: Connection was reset",{"http_read_failure","connection_reset","transfer_failure"}},
        {"Failure when receiving data from the peer",{"http_read_failure","transfer_failure"}},
        {"Operation timed out, retrying (#2) from 84513792",{"timeout","retrying"}},
        {"transfer closed with 25513 bytes remaining to read",{"truncated_response"}},
        {"HTTP read error",{"http_read_failure"}}
    }) {
        auto summary=summarize_mpv_log("curl","error",item.text);
        check(summary["module"]=="curl" && summary["category"]=="network","curl errors retain module and network category");
        check(summary.value("signals",json::array())==item.signals,"curl summary preserves exact allowlisted failure flags");
    }
    auto child=summarize_mpv_log("stream/curl","warn","transfer failed");
    check(child["module"]=="stream.curl" && child["category"]=="network","nested curl module is recognized");
}
void ffmpeg_and_gpu_signals_do_not_overstate_fault_source() {
    auto partial=summarize_mpv_log("ffmpeg/demuxer","error","stream 0, offset 0x5099370: partial file");
    check(partial["module"]=="ffmpeg.demuxer" && partial["category"]=="other","partial file alone does not assert network or decoder root cause");
    check(partial.value("signals",json::array())==json::array({"partial_file"}),"partial file has a distinct symbolic flag");
    auto corrupt=summarize_mpv_log("ffmpeg/demuxer","warn","Packet corrupt (stream = 0, dts = 851200).");
    check(corrupt.value("signals",json::array())==json::array({"packet_corrupt"}),"corrupt packet has a distinct symbolic flag");
    auto nal=summarize_mpv_log("ffmpeg/video","error","Invalid NAL unit size (122415 > 256).");
    check(nal["category"]=="decoder" && nal.value("signals",json::array())==json::array({"invalid_nal"}),"invalid NAL remains decoder heuristic plus concrete flag");
    auto gpu=summarize_mpv_log("vo/gpu-next","error","DXGI_ERROR_DEVICE_REMOVED: HRESULT 0x887A0005");
    check(gpu["category"]=="gpu" && gpu.value("signals",json::array())==json::array({"gpu_device_lost"}),"explicit device removal gets device-loss flag");
    auto hw=summarize_mpv_log("vd","warn","Error while decoding frame (hardware decoding)!");
    check(hw["category"]=="decoder" && hw.value("signals",json::array())==json::array({"hardware_decode_failure"}),"explicit hardware frame decode failure is distinct from device loss");
    auto unknown=summarize_mpv_log("ffmpeg/demuxer","error","unclassified issue at 568 seconds");
    check(unknown.value("signals",json::array()).empty() && unknown["classification"]=="heuristic","unrecognized text gets no invented symbolic fault");
}
void extractor_signals_preserve_known_failure_wording() {
    for(const auto& text:{
        "ERROR: [BiliBili] BV1PyxQ6sEbH: Requested format is not available. Use --list-formats for a list of available formats",
        "Requested format not available",
        "ERROR: REQUESTED FORMAT IS NOT AVAILABLE"
    }) {
        auto summary=summarize_mpv_log("ytdl_hook","error",text);
        check(summary["category"]=="extractor","requested format failure identifies extraction rather than an unknown error");
        check(summary["signals"]==json::array({"requested_format_unavailable"}),"format selection failure retains a literal symbolic signal");
    }
    for(const auto& text:{"youtube-dl failed: unexpected error occurred", "yt-dlp failed: unexpected error occurred"}) {
        auto summary=summarize_mpv_log("ytdl_hook","error",text);
        check(summary["category"]=="extractor" && summary["signals"]==json::array({"extractor_failure"}),"generic hook failure stays distinct from unavailable format");
    }
    auto unknown=summarize_mpv_log("ytdl_hook","error","unclassified issue");
    check(unknown["category"]=="other" && unknown["signals"].empty(),"hook module alone cannot invent an extraction failure signal");
}
void credential_spans_cannot_seed_extractor_signals() {
    for(const auto& text:{
        "https://cdn.test/?message=requested format is not available",
        "https://cdn.test/?message=yt-dlp failed",
        "Cookie: requested format is not available; youtube-dl failed",
        "Set-Cookie: requested format not available; yt-dlp failed",
        "Authorization: Bearer requested format is not available; youtube-dl failed",
        "Proxy-Authorization: requested format is not available; yt-dlp failed",
        "Referer: requested format is not available; youtube-dl failed",
        "User-Agent: requested format is not available; yt-dlp failed",
        "{\"Cookie\": \"requested format is not available\"}",
        "{'User-Agent': 'yt-dlp failed'}",
        "C:\\private\\requested format is not available\\yt-dlp failed"
    }) {
        auto summary=summarize_mpv_log("ytdl_hook","error",text);
        check(summary["category"]=="other" && summary["signals"].empty(),"credential URL header and path spans cannot seed extractor signals");
    }
    auto safe=summarize_mpv_log("ytdl_hook","error","Cookie: requested format is not available\nyt-dlp failed: unexpected error occurred");
    check(safe["signals"]==json::array({"extractor_failure"}),"masking a header retains subsequent actual diagnostic wording");
}
void extraction_failure_summary_retains_only_allowlisted_evidence() {
    auto unavailable=summarize_extraction_failure(1,"ERROR: Requested format is not available. https://user:password@cdn.test/?token=topsecret\nCookie: private-cookie\nC:\\private\\extractor.exe");
    check(unavailable==json({{"exit_code",1},{"reason","requested_format_unavailable"},{"signals",json::array({"requested_format_unavailable"})}}),"background extraction failure keeps only exit code fixed reason and literal signal");
    auto generic=summarize_extraction_failure(2,"youtube-dl failed: unexpected error occurred HRESULT 0x80070005 offset 0x1234: partial file");
    check(generic.value("exit_code",0)==2 && generic.value("reason","")=="extractor_failure","generic extraction failure retains its actual process result");
    check(!generic.contains("error_codes") && !generic.contains("file_offset") && !generic.contains("module"),"extraction summary does not inherit unrelated log detail fields");
    auto http=summarize_extraction_failure(3,"ERROR: HTTP Error 403: Forbidden https://cdn.test/v?token=secret\nAuthorization: HTTP Error 401");
    check(http==json({{"exit_code",3},{"reason","unclassified"},{"signals",json::array()},{"http_status",403}}),"explicit HTTP status survives without retaining message text or masked status");
    auto unknown=summarize_extraction_failure(4294967295UL,"opaque-private-error C:\\private\\clip.mp4\nCookie: requested format is not available\nhttps://cdn.test/?error=secret");
    check(unknown==json({{"exit_code",4294967295UL},{"reason","unclassified"},{"signals",json::array()}}),"unknown failure retains an unsigned process exit code without echoing unknown text");
    auto large=summarize_extraction_failure(1,"ERROR: Requested format is not available\n"+std::string(4*1024*1024,'x')+"private-tail-secret");
    check(large==json({{"exit_code",1},{"reason","requested_format_unavailable"},{"signals",json::array({"requested_format_unavailable"})}}),"multi-megabyte stderr produces a bounded allowlisted summary");
    auto beyond_bound=summarize_extraction_failure(1,std::string(16384,'x')+"requested format is not available");
    check(beyond_bound["signals"].empty(),"diagnostic matching remains bounded for oversized input");
    TempDir dir;PlaybackDiagnostics recorder(dir.path.wstring());
    recorder.record("extraction-failed",unavailable);check(recorder.flush(),"safe background extraction summary persists");
    auto row=records(dir.path/L"playback.jsonl")[0];
    check(row["fields"]==unavailable,"exit code reason and signals survive the real recorder filter");
    auto exported=contents(fs::path(recorder.save_report({})));
    check(exported.find("topsecret")==std::string::npos && exported.find("private")==std::string::npos && exported.find("cdn.test")==std::string::npos && exported.find("password")==std::string::npos,"recorded extraction failure exports no raw URL credential header path or message");
}
void offsets_and_sensitive_hex_are_not_error_codes() {
    auto offset=summarize_mpv_log("ffmpeg/demuxer","error","stream 0, offset 0x5099370: partial file");
    check(!offset.contains("error_codes"),"file offset must never masquerade as HRESULT or error code");
    check(offset.value("file_offset","")=="0x5099370","partial file offset retained separately as bounded hex");
    auto large=summarize_mpv_log("ffmpeg/demuxer","error","root atom offset 0x123456789abcdef0: partial file");
    check(large.value("file_offset","")=="0x123456789abcdef0" && !large.contains("error_codes"),"64-bit file offset retained without code ambiguity");
    auto codes=summarize_mpv_log("vo/gpu-next","error","HRESULT: 0x887A0005; error code = 0x80070005; error 0x80004005; address 0x1234; error 0x123456789");
    check(codes.value("error_codes",json::array())==json::array({"0x887a0005","0x80070005","0x80004005"}),"only explicit bounded labelled codes are retained");
    auto query=summarize_mpv_log("curl","error","transfer failed for https://user:password@cdn.test/v?error=0xdeadbeef&hresult=0xcafebabe");
    check(!query.contains("error_codes") && query.dump().find("deadbeef")==std::string::npos && query.dump().find("cafebabe")==std::string::npos,"URL query hexadecimal credentials never become error codes");
    auto headers=summarize_mpv_log("curl","error","Cookie: HRESULT=0xfedcba98\nAuthorization: error 0xbaadf00d\nC:\\private\\error 0x12345678");
    check(!headers.contains("error_codes") && headers.dump().find("private")==std::string::npos,"credential headers and local paths cannot seed error codes");
}
void safe_signals_and_offsets_survive_real_recording() {
    TempDir dir;PlaybackDiagnostics recorder(dir.path.wstring());
    recorder.record("mpv-log",summarize_mpv_log("ffmpeg/demuxer","error","stream 0, offset 0x5099370: partial file https://cdn.test/video?token=topsecret Cookie: private"));
    recorder.record("mpv-log",summarize_mpv_log("curl","error","Recv failure: Connection was reset https://cdn.test/?error=0xdeadbeef"));
    check(recorder.flush(),"safe symbolic summaries persist successfully");
    auto rows=records(dir.path/L"playback.jsonl");
    check(rows.size()==2 && rows[0]["fields"]["signals"]==json::array({"partial_file"}) && rows[0]["fields"]["file_offset"]=="0x5099370","symbolic flags and offset survive recorder field filtering");
    auto report=json::parse(contents(fs::path(recorder.save_report({}))));
    auto saved=report.dump();check(saved.find("topsecret")==std::string::npos && saved.find("private")==std::string::npos && saved.find("cdn.test")==std::string::npos && saved.find("deadbeef")==std::string::npos,"new summary evidence exports no raw URL header query or credential");
}
void structured_records_redact_and_preserve_schema() {
    TempDir dir;PlaybackDiagnostics recorder(dir.path.wstring());
    json fields={{"position",13.5},{"paused",false},{"missing",nullptr},{"snapshot",{{"values",{{"video-params/h",360}}},{"status",{{"video-params/h",0}}}}},{"cookie","private-cookie"},{"Authorization","Bearer private-auth"},{"url","https://secret.test/v?token=x"},{"local_path","C:\\Users\\private\\clip.mp4"},{"nested",{{"codec","h264"},{"opaque","https://private.test/?q=hidden"},{"token","secret-token"},{"items",json::array({1,"safe","C:\\private\\file","x?token=secret"})}}}};
    recorder.record("sample",fields);check(recorder.flush(),"flush persists queued event");
    auto rows=records(dir.path/L"playback.jsonl");check(rows.size()==1,"one real JSONL row persisted");
    auto row=rows[0];check(row["schema"]==1 && row["pid"]==GetCurrentProcessId() && row["utc_epoch"].is_number_integer() && row["tick_ms"].is_number_integer() && row["session_id"].is_string(),"versioned envelope associates integer time process and session");
    check(row["event"]=="sample" && row["fields"]["position"]==13.5 && row["fields"]["paused"]==false && row["fields"]["missing"].is_null(),"safe typed values survive");
    check(row["fields"]["snapshot"]["values"].value("video-params/h",0)==360 && row["fields"]["snapshot"]["status"].contains("video-params/h"),"known mpv property slash is retained in values and status");
    auto saved=contents(dir.path/L"playback.jsonl");check(saved.find("private")==std::string::npos && saved.find("Bearer")==std::string::npos && saved.find("secret")==std::string::npos && saved.find("hidden")==std::string::npos,"recursive sensitive content absent from disk");
    check(row["fields"]["nested"]["codec"]=="h264" && row["fields"]["nested"]["items"]==json::array({1,"safe"}),"recursive filter preserves safe siblings");
    auto report=fs::path(recorder.save_report({{"build","test"},{"libmpv","2"},{"cookie","report-secret"},{"nested",{{"opaque","https://report.test/?token=hidden"}}}}));
    check(report.parent_path()==dir.path/L"reports","exports live in dedicated reports subdirectory");
    auto exported=json::parse(contents(report));check(exported["context"]["build"]=="test" && exported["records"].size()==1 && exported.contains("health"),"report includes safe context evidence and health");
    check(contents(report).find("report-secret")==std::string::npos && contents(report).find("report.test")==std::string::npos,"export context uses same recursive filter");
    check(recorder.status().value("available",false),"successful disk write sets availability");
    check(recorder.status().dump().find(dir.path.string())==std::string::npos,"health contains no full directory");
}
void rotation_history_and_reports_stay_bounded() {
    TempDir dir;PlaybackDiagnostics recorder(dir.path.wstring(),1600,256);
    recorder.record("source-context",{{"bvid","BV1BMgj6MEz4"},{"cid",222},{"generation",3}});check(recorder.flush(),"source persisted");
    for(int i=0;i<2200;++i) {recorder.record("sample",{{"sequence",i},{"codec",std::string(500,'a')}});check(recorder.flush(),"flush each sample for deterministic rotation");}
    for(const auto& name:{L"playback.jsonl",L"playback.1.jsonl",L"playback.2.jsonl"}) {auto file=dir.path/name;check(fs::exists(file) && fs::file_size(file)<=1600,"each rotated log respects configured bound");records(file);}
    check(!fs::exists(dir.path/L"playback.3.jsonl"),"only three log generations exist");
    auto report=fs::path(recorder.save_report({{"build","test"}}));auto exported=json::parse(contents(report));
    check(exported["records"].size()<=2048,"report history has hard record count bound");
    check(exported["source_context"]["bvid"]=="BV1BMgj6MEz4" && exported["source_context"]["generation"]==3,"last source context survives history eviction");
    for(int i=0;i<7;++i) recorder.save_report({{"number",i}});
    size_t reports=0;for(const auto& file:fs::directory_iterator(dir.path/L"reports")) if(file.path().extension()==L".json") ++reports;
    check(reports==5,"only five exported reports retained");
}
void known_rate_property_and_report_directory_are_preserved() {
    TempDir dir;PlaybackDiagnostics recorder(dir.path.wstring());
    recorder.record("sample",{{"snapshot",{{"values",{{"demuxer-cache-state",{{"raw-input-rate",42000},{"cache-duration",7}}}}}}}});
    check(recorder.flush(),"known read-rate snapshot persisted");
    auto row=records(dir.path/L"playback.jsonl")[0];
    check(row["fields"]["snapshot"]["values"]["demuxer-cache-state"].value("raw-input-rate",0)==42000,"known raw-input-rate numeric property survives generic secret filtering");
    auto report=fs::path(recorder.save_report({}));check(report.parent_path()==dir.path/L"reports","report directory is separate from rotating logs");
}
void oversize_invalid_and_failed_writes_are_safe() {
    TempDir dir;
    {PlaybackDiagnostics recorder(dir.path.wstring());recorder.record("sample",{{"large",std::string(10000,'a')},{"bad",std::string("\xff",1)}});check(recorder.flush(),"invalid UTF8 or oversize fields cannot break worker");auto rows=records(dir.path/L"playback.jsonl");check(!rows.empty(),"oversized event still leaves bounded evidence");}
    auto blocker=dir.path/L"blocker";{std::ofstream out(blocker);out<<"file blocks directory";}
    PlaybackDiagnostics failing(blocker.wstring());failing.record("sample",{{"position",1}});
    check(!failing.flush(),"flush reports actual filesystem failure");check(failing.status().value("write_failures",0)>0 && !failing.status().value("available",true),"failure status is explicit");
    bool rejected=false;try {failing.save_report({});} catch(const std::exception& e) {rejected=std::string(e.what()).find(dir.path.string())==std::string::npos;}
    check(rejected,"export failure throws generic error with no sensitive path");
    PlaybackDiagnostics dropping((dir.path/L"drop").wstring(),2097152,0);for(int i=0;i<8;++i) dropping.record("sample",{{"sequence",i}});
    check(dropping.status().value("dropped_records",0)==8,"bounded disabled queue accounts for every dropped event");
}
}
int main() {
    unsigned failures=0;
    for(auto test:{summaries_keep_only_safe_classification,curl_failure_signals_keep_literal_distinctions,ffmpeg_and_gpu_signals_do_not_overstate_fault_source,extractor_signals_preserve_known_failure_wording,credential_spans_cannot_seed_extractor_signals,extraction_failure_summary_retains_only_allowlisted_evidence,offsets_and_sensitive_hex_are_not_error_codes,safe_signals_and_offsets_survive_real_recording,structured_records_redact_and_preserve_schema,rotation_history_and_reports_stay_bounded,known_rate_property_and_report_directory_are_preserved,oversize_invalid_and_failed_writes_are_safe}) {
        try {test();}catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";++failures;}
    }
    if(failures) return 1;
    std::cout<<"PASS "<<assertions<<" playback diagnostics assertions\n";
}
