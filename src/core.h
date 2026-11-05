#pragma once
#include <string>
#include <vector>
#include <array>
#include "json.hpp"
namespace gv {
using json = nlohmann::json;
constexpr std::array<double,7> PLAYBACK_SPEEDS={{0.5,0.75,1.0,1.25,1.5,1.75,2.0}};
bool valid_playback_speed(double value);
struct VideoRef {
    std::string bvid; int page=0; bool short_url=false; std::string original;
    std::string url() const;
};
struct Part { int page=0; long long cid=0; std::string title; double duration=0; };
struct Guide { std::string bvid,title; std::vector<Part> parts; };
struct Media {
    std::string video,audio; int height=0; std::string codec,note;
    unsigned long long extraction_peak_job_bytes=0;unsigned long extraction_processes=0;
    std::vector<std::pair<std::string,std::string>> headers;
};
struct Resume { size_t index=0; double seconds=0; };
VideoRef parse_video_url(const std::string& input);
Media select_media(const json& info,int max_height);
Resume choose_resume(const VideoRef& ref,const json& history,const std::vector<Part>& parts);
double clamp_seek(double position,double delta,double duration);
long long stream_deadline(const std::string& url);
long long extraction_deadline(const json& info);
}
