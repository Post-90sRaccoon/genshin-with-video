#pragma once
#include "core.h"
#include <cstddef>
#include <memory>

namespace gv {
// Stores only structured, redacted evidence. Recording never performs filesystem I/O.
class PlaybackDiagnostics {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit PlaybackDiagnostics(std::wstring directory, size_t max_bytes=2097152, size_t queue_limit=256);
    ~PlaybackDiagnostics();
    PlaybackDiagnostics(const PlaybackDiagnostics&)=delete;
    PlaybackDiagnostics& operator=(const PlaybackDiagnostics&)=delete;
    void record(const std::string& event,const json& fields=json::object()) noexcept;
    bool flush() noexcept;
    json status() const;
    std::wstring save_report(const json& context);
};
json summarize_mpv_log(const std::string& module,const std::string& level,const std::string& text);
// Bounded allowlisted evidence only; the stderr text is never retained.
json summarize_extraction_failure(unsigned long exit_code,const std::string& stderr_text);
std::string diagnostic_host(const std::string& url);
}
