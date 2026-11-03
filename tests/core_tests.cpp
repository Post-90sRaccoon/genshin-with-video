#include "core.h"
#include <iostream>
#include <stdexcept>
using namespace gv;
int count = 0;
void check(bool ok, const char* name) { ++count; if (!ok) throw std::runtime_error(name); }
template<class F> void rejects(F f, const char* name) { bool rejected = false; try { f(); } catch (const std::exception&) { rejected = true; } check(rejected,name); }
int main() {
    try {
        auto ref = parse_video_url("https://www.bilibili.com/video/BV1BMgj6MEz4/?spm_id_from=x&p=2&vd_source=abc");
        check(ref.bvid == "BV1BMgj6MEz4" && ref.page == 2,"normalize and keep explicit P");
        check(ref.url() == "https://www.bilibili.com/video/BV1BMgj6MEz4/?p=2","tracking removed");
        check(parse_video_url(" BV1BMgj6MEz4 ").page == 0,"bare BV");
        rejects([]{parse_video_url("https://www.bilibili.com.evil.test/video/BV1BMgj6MEz4/");},"reject spoofed host");
        rejects([]{parse_video_url("https://evil.test@www.bilibili.com/video/BV1BMgj6MEz4/");},"reject URL credentials");
        rejects([]{parse_video_url("https://www.bilibili.com/video/BV1BMgj6MEz4/?p=0");},"zero P");
        rejects([]{parse_video_url("https://www.bilibili.com/video/BV1BMgj6MEz4/?p=-1");},"negative P");
        rejects([]{parse_video_url("https://www.bilibili.com/video/BV1BMgj6MEz4/?p=2&p=3");},"duplicate P");
        rejects([]{parse_video_url("https://www.bilibili.com/video/BV1BMgj6MEz4/?p=2x");},"malformed P");
        check(parse_video_url("https://b23.tv/abc123").short_url,"short URL recognized");
        json formats = {{"accept_quality",{116,80,64,32,16}},{"formats",json::array({
            {{"url","https://cdn.test/480av1"},{"height",480},{"fps",30},{"vcodec","av01"},{"acodec","none"}},
            {{"url","https://cdn.test/480avc"},{"height",480},{"fps",30},{"vcodec","avc1"},{"acodec","none"}},
            {{"url","https://cdn.test/720fast"},{"height",720},{"fps",60},{"vcodec","avc1"},{"acodec","none"}},
            {{"url","https://cdn.test/audio"},{"vcodec","none"},{"acodec","mp4a"},{"abr",128}}
        })}};
        auto media=select_media(formats,720);
        check(media.height==480 && media.video=="https://cdn.test/480avc","actual formats, fps cap, codec preference");
        check(media.audio=="https://cdn.test/audio","separate audio");
        rejects([&]{select_media(formats,360);},"do not silently decode above cap");
        formats["formats"].push_back({{"url","https://cdn.test/progressive"},{"height",360},{"fps",30},{"vcodec","avc1"},{"acodec","aac"}});
        check(select_media(formats,360).audio.empty(),"progressive already contains audio");
        std::vector<Part> parts={{1,222,"first",60},{2,111,"second",120}};
        json history={{"last_cid",111},{"parts",{{"111",42.5},{"222",20.0}}}};
        auto resume=choose_resume({"BV1BMgj6MEz4",0,false,""},history,parts);
        check(resume.index==1 && resume.seconds==42.5,"CID survives reordered P");
        json allHistory={{"BV1BMgj6MEz4",history}};
        resume=choose_resume({"BV1BMgj6MEz4",0,false,""},allHistory,parts);
        check(resume.index==1 && resume.seconds==42.5,"look up resolved BV before restoring short-link history");
        resume=choose_resume({"BV1BMgj6MEz4",1,false,""},history,parts);
        check(resume.index==0 && resume.seconds==20.0,"explicit P beats latest P");
        rejects([&]{choose_resume({"BV1BMgj6MEz4",9,false,""},history,parts);},"out-of-range P");
        check(clamp_seek(2,-5,100)==0,"seek start clamp");
        check(clamp_seek(98,5,100)==100,"seek end clamp");
        check(clamp_seek(20,-5,100)==15,"relative seek");
        std::cout << "PASS " << count << " core assertions\n";
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<"\n"; return 1; }
}
