#pragma once
#include "platform.h"
#include <cstddef>

namespace gv {
struct ExtractionFailure : std::runtime_error {
    json details;
    explicit ExtractionFailure(json evidence):std::runtime_error(
        evidence.value("reason",std::string())=="requested_format_unavailable"?
        "未找到符合当前画质和帧率限制的视频流":"视频解析失败，请检查网络或登录状态后重试"),details(std::move(evidence)){}
};
struct ExtractionOptions {
    std::wstring executable,cookies;
    int max_height=720;
    unsigned timeout_ms=30000;
    size_t output_limit=4*1024*1024;
};
struct ExtractionProcessResult {
    std::string output;
    unsigned long long peak_job_bytes=0;
    unsigned long processes=0;
};
// Uses an explicit Windows executable and argv, without a command shell.
ExtractionProcessResult run_extraction_process(const std::wstring& executable,
    const std::vector<std::wstring>& arguments,unsigned timeout_ms,size_t output_limit,Cancellation& cancel);
std::string extraction_format(int max_height);
Media parse_extracted_media(const json& info,const VideoRef& ref,const Part& part,int max_height);
Media extract_media(const VideoRef& ref,const Part& part,const ExtractionOptions& options,Cancellation& cancel);
}
