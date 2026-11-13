#include "core.h"
#include <iostream>
#include <stdexcept>
#include <limits>
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
        json fpsFormats={{"formats",json::array({
            {{"url","https://cdn.test/nominal30"},{"height",720},{"fps",30.002},{"vcodec","avc1"},{"acodec","none"}},
            {{"url","https://cdn.test/audio"},{"vcodec","none"},{"acodec","mp4a"},{"abr",128},{"fps",60}}
        })}};
        for(double fps:{30.0,30.002,30.1}){
            fpsFormats["formats"][0]["fps"]=fps;
            auto selected=select_media(fpsFormats,720);
            check(selected.video=="https://cdn.test/nominal30" && selected.audio=="https://cdn.test/audio","select nominal 30 FPS within rounding tolerance without filtering audio FPS");
        }
        for(double fps:{30.1001,31.0,60.0}){
            fpsFormats["formats"][0]["fps"]=fps;
            rejects([&]{select_media(fpsFormats,720);},"selection rejects video above nominal 30 FPS tolerance");
        }
        fpsFormats["formats"][0].erase("fps");
        check(select_media(fpsFormats,720).height==720,"selection allows missing video FPS");
        fpsFormats["formats"][0]["fps"]=nullptr;
        check(select_media(fpsFormats,720).height==720,"selection allows unknown video FPS");
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
        check(stream_deadline("https://cdn.test/v?x=1&deadline=1793856353&token=hidden")==1793856353,"read the signed stream expiry");
        check(stream_deadline("https://cdn.test/v?xdeadline=1793856353")==0,"expiry must be an exact query key");
        check(stream_deadline("https://cdn.test/v#?deadline=1793856353")==0,"a fragment is not an expiry query");
        check(stream_deadline("https://cdn.test/v?deadline=-1")==0,"ignore negative expiry");
        check(stream_deadline("https://cdn.test/v?deadline=1793856353x")==0,"ignore malformed expiry");
        check(stream_deadline("https://cdn.test/v?deadline=9999999999999999999999999")==0,"ignore overflowing expiry");
        json extracted={{"formats",json::array({{{"url","https://cdn.test/unused?deadline=1"}}})},{"requested_formats",json::array({{{"url","https://cdn.test/video?deadline=1793856353"}},{{"url","https://cdn.test/audio?deadline=1793856253"}}})}};
        check(extraction_deadline(extracted)==1793856253,"renew before the earliest selected audio or video expires");
        check(extraction_deadline({{"url","https://cdn.test/muxed?deadline=1793856353"}})==1793856353,"progressive stream expiry");
        check(extraction_deadline({{"requested_formats",nullptr}})==0,"missing expiry leaves local and unsigned media alone");
        // The P4 report had a video EOF at 568s while the 648s audio kept advancing.
        // Cache EOF alone is healthy when the complete video is already buffered.
        const json earlyVideo={{"type","video"},{"cache-end",568.033312},{"cache-duration",0.0}};
        const json earlyCache={{"eof",true},{"ts-per-stream",json::array({earlyVideo,
            {{"type","audio"},{"cache-end",648.106667},{"cache-duration",76.416}}})}};
        auto input=read_video_input(earlyCache,571.2612452081161);
        check(input.valid && input.reader_eof && input.video_end==568.033312 && input.video_remaining==0.0,
            "read the video row rather than the longer audio cache");
        check(premature_video_eof(input,571.254610258116,648.1333125),
            "detect drained early video EOF from the real P4 report");
        auto stillBuffered=earlyCache;
        stillBuffered["ts-per-stream"][0]["cache-duration"]=55.433312;
        check(!premature_video_eof(read_video_input(stillBuffered,512.6),512.6,648.1333125),
            "reader EOF with 55 seconds of video remaining does not recover early");
        auto finalFrame=earlyCache;
        finalFrame["ts-per-stream"][0]["cache-duration"]=0.1;
        check(!premature_video_eof(read_video_input(finalFrame,567.76),567.75,648.1333125),
            "audio must advance beyond the final video packets before recovery");
        auto fullCache=earlyCache;
        fullCache["ts-per-stream"][0]["cache-end"]=648.0;
        check(!premature_video_eof(read_video_input(fullCache,648.1),648.1,648.1333125),
            "EOF after a healthy complete video cache is not a fault");
        auto noEof=earlyCache;
        noEof["eof"]=false;
        check(!premature_video_eof(read_video_input(noEof,571.26),571.25,648.1333125),
            "a drained cache without reader EOF is not this failure");
        const json externalAudioCache={{"eof",true},{"ts-per-stream",json::array({earlyVideo})}};
        check(premature_video_eof(read_video_input(externalAudioCache,571.26),571.25,648.1333125),
            "external public API audio need not appear in the video demuxer cache");
        auto shortOutro=read_video_input(externalAudioCache,100.0);
        shortOutro.video_end=95.0;
        check(!premature_video_eof(shortOutro,100.0,100.0),"normal five-second outro gap is tolerated");
        shortOutro.video_end=1980.0;shortOutro.audio_position=2000.0;
        check(!premature_video_eof(shortOutro,2000.0,2000.0),"normal one-percent outro gap is tolerated");
        shortOutro.video_end=1979.9;
        check(premature_video_eof(shortOutro,2000.0,2000.0),"early EOF beyond the one-percent margin is detected");
        auto notDrained=input;notDrained.video_remaining=0.251;
        check(!premature_video_eof(notDrained,571.25,648.1333125),"video cache must drain to a quarter second");
        notDrained.video_remaining=0.25;
        check(premature_video_eof(notDrained,571.25,648.1333125),"quarter-second residual cache is allowed");
        auto noAudioLead=input;noAudioLead.audio_position=568.533312;
        check(!premature_video_eof(noAudioLead,571.25,648.1333125),"audio lead must exceed half a second");
        check(!premature_video_eof(input,567.5,648.1333125),"playback must reach the video cache end");
        check(premature_video_eof(input,567.533312,648.1333125),"half-second playback tolerance includes the boundary");
        for(const json& invalidCache:std::vector<json>{nullptr,json::array(),json::object(),
            {{"eof",true}},{{"eof",true},{"ts-per-stream",nullptr}},
            {{"eof",true},{"ts-per-stream",json::array()}},
            {{"eof",true},{"ts-per-stream",json::array({{{"type","audio"},{"cache-end",648.1},{"cache-duration",76.4}}})}}})
            check(!read_video_input(invalidCache,571.26).valid,"missing cache data cannot establish video EOF");
        for(const json& wrongEof:std::vector<json>{nullptr,0,1,"true",json::array(),json::object()}) {
            auto bad=externalAudioCache;bad["eof"]=wrongEof;
            check(!read_video_input(bad,571.26).valid,"EOF must be an actual boolean");
        }
        for(const char* key:{"cache-end","cache-duration"}) {
            auto missing=externalAudioCache;missing["ts-per-stream"][0].erase(key);
            check(!read_video_input(missing,571.26).valid,"video cache metrics must be present");
            for(const json& badMetric:std::vector<json>{nullptr,"0",true,-1.0,
                std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
                auto bad=externalAudioCache;bad["ts-per-stream"][0][key]=badMetric;
                check(!read_video_input(bad,571.26).valid,"video cache metrics must be finite nonnegative numbers");
            }
        }
        auto duplicateVideo=externalAudioCache;duplicateVideo["ts-per-stream"].push_back(earlyVideo);
        check(!read_video_input(duplicateVideo,571.26).valid,"duplicate video rows make the cache ambiguous");
        for(const json& badStream:std::vector<json>{nullptr,12,json::object(),{{"type",true}}}) {
            auto bad=externalAudioCache;bad["ts-per-stream"].push_back(badStream);
            check(!read_video_input(bad,571.26).valid,"untyped stream rows cannot establish a unique video");
        }
        for(double invalidTime:{-1.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
            check(!read_video_input(externalAudioCache,invalidTime).valid,"invalid or unavailable audio clock invalidates observation");
            check(!premature_video_eof(input,invalidTime,648.1333125),"invalid playback clock cannot trigger recovery");
            check(!premature_video_eof(input,571.25,invalidTime),"invalid duration cannot trigger recovery");
            auto invalidInput=input;invalidInput.video_end=invalidTime;
            check(!premature_video_eof(invalidInput,571.25,648.1333125),"predicate rejects invalid video end even when marked valid");
            invalidInput=input;invalidInput.video_remaining=invalidTime;
            check(!premature_video_eof(invalidInput,571.25,648.1333125),"predicate rejects invalid residual cache even when marked valid");
            invalidInput=input;invalidInput.audio_position=invalidTime;
            check(!premature_video_eof(invalidInput,571.25,648.1333125),"predicate rejects invalid audio clock even when marked valid");
        }
        check(!premature_video_eof(input,0,648.1333125),"zero playback clock is not an active failure");
        check(!premature_video_eof(input,571.25,0),"unknown zero duration cannot trigger recovery");
        auto zeroEnd=input;zeroEnd.video_end=0;
        check(!premature_video_eof(zeroEnd,571.25,648.1333125),"zero video end is not evidence of truncated media");
        auto unavailable=input;unavailable.valid=false;
        check(!premature_video_eof(unavailable,571.25,648.1333125),"invalid observation cannot trigger recovery");
        check(!premature_video_eof(input,649.2,648.1333125),"playback more than one second past duration is implausible");
        auto pastDuration=input;pastDuration.audio_position=649.2;
        check(!premature_video_eof(pastDuration,571.25,648.1333125),"audio more than one second past duration is implausible");
        pastDuration.audio_position=649.1333125;
        check(premature_video_eof(pastDuration,649.1333125,648.1333125),"one-second end-clock tolerance includes the boundary");

        // The P9 HTTP 403 report stopped the reader with 18 seconds still buffered.
        // Renewal must be possible while both playback clocks still precede EOF.
        const json damagedCache={{"eof",true},{"ts-per-stream",json::array({
            {{"type","video"},{"cache-end",690.766},{"cache-duration",18.16}}})}};
        auto damaged=read_video_input(damagedCache,672.6);
        check(damaged_video_input(damaged,672.6,1112.2),
            "detect truncated video input before the actual P9 buffered tail drains");
        check(!premature_video_eof(damaged,672.6,1112.2),
            "early input damage does not change the drained EOF contract");
        auto audioBehind=damaged;audioBehind.audio_position=0;
        check(damaged_video_input(audioBehind,672.6,1112.2),
            "early damage does not require audio to pass the final video packets");
        check(damaged_video_input(input,571.25,648.1333125),
            "drained truncated input is also damaged");
        check(damaged_video_input(read_video_input(stillBuffered,512.6),512.6,648.1333125),
            "reader EOF identifies truncated input while a longer buffered tail remains");
        auto healthyCached=damaged;healthyCached.video_end=1112.2;healthyCached.video_remaining=439.6;
        check(!damaged_video_input(healthyCached,672.6,1112.2),
            "whole-file cached EOF is healthy even with substantial buffered video");
        healthyCached.video_end=1113.0;
        check(!damaged_video_input(healthyCached,672.6,1112.2),
            "video packets extending beyond the nominal duration are healthy");
        auto noReaderEof=damaged;noReaderEof.reader_eof=false;
        check(!damaged_video_input(noReaderEof,672.6,1112.2),
            "ordinary partially buffered input without reader EOF is healthy");
        auto invalidObservation=damaged;invalidObservation.valid=false;
        check(!damaged_video_input(invalidObservation,672.6,1112.2),
            "unavailable input observation cannot establish early damage");

        VideoInputState outro={true,true,95.0,18.0,77.0};
        check(!damaged_video_input(outro,77.0,100.0),
            "five-second video outro is not damaged input");
        outro.video_end=94.9;
        check(damaged_video_input(outro,77.0,100.0),
            "gap beyond the five-second outro allowance establishes damage");
        outro={true,true,1980.0,80.0,1900.0};
        check(!damaged_video_input(outro,1900.0,2000.0),
            "one-percent video outro is not damaged input");
        outro.video_end=1979.9;
        check(damaged_video_input(outro,1900.0,2000.0),
            "gap beyond the one-percent outro allowance establishes damage");

        VideoInputState readerAhead={true,true,700.0,60.0,635.0};
        check(damaged_video_input(readerAhead,635.0,1112.2),
            "packet and decode clocks may lead the playback clock by five seconds");
        check(!damaged_video_input(readerAhead,634.9,1112.2),
            "playback far behind the inferred cache reader position rejects stale post-seek input");
        check(!damaged_video_input(damaged,100.0,1112.2),
            "old EOF observation after a backward seek cannot trigger early renewal");
        auto impossibleRemaining=damaged;impossibleRemaining.video_remaining=691.267;
        check(!damaged_video_input(impossibleRemaining,1.0,1112.2),
            "remaining video cannot exceed its end timestamp by more than half a second");
        impossibleRemaining.video_remaining=691.266;
        check(damaged_video_input(impossibleRemaining,1.0,1112.2),
            "half-second packet timestamp allowance accepts a plausible initial cache");

        for(const json& malformedCache:std::vector<json>{nullptr,json::array(),json::object(),
            {{"eof",true}},{{"eof",true},{"ts-per-stream",json::array()}},
            {{"eof",true},{"ts-per-stream",json::array({{{"type","video"},{"cache-end",690.766}}})}}})
            check(!damaged_video_input(read_video_input(malformedCache,672.6),672.6,1112.2),
                "malformed cache cannot establish early input damage");
        for(double invalidTime:{-1.0,std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}) {
            check(!damaged_video_input(damaged,invalidTime,1112.2),
                "invalid playback clock rejects early damage");
            check(!damaged_video_input(damaged,672.6,invalidTime),
                "invalid duration rejects early damage");
            auto invalidInput=damaged;invalidInput.video_end=invalidTime;
            check(!damaged_video_input(invalidInput,672.6,1112.2),
                "invalid video end rejects early damage even if marked valid");
            invalidInput=damaged;invalidInput.video_remaining=invalidTime;
            check(!damaged_video_input(invalidInput,672.6,1112.2),
                "invalid remaining cache rejects early damage even if marked valid");
            invalidInput=damaged;invalidInput.audio_position=invalidTime;
            check(!damaged_video_input(invalidInput,672.6,1112.2),
                "invalid audio clock rejects early damage even if marked valid");
        }
        check(!damaged_video_input(damaged,0,1112.2),"zero playback clock rejects early damage");
        check(!damaged_video_input(damaged,672.6,0),"unknown zero duration rejects early damage");
        auto missingEnd=damaged;missingEnd.video_end=0;
        check(!damaged_video_input(missingEnd,672.6,1112.2),"zero video end rejects early damage");
        check(!damaged_video_input(damaged,1113.3,1112.2),
            "playback more than one second past duration rejects early damage");
        auto invalidAudio=damaged;invalidAudio.audio_position=1113.3;
        check(!damaged_video_input(invalidAudio,672.6,1112.2),
            "audio more than one second past duration rejects early damage");
        invalidAudio.audio_position=1113.2;
        check(damaged_video_input(invalidAudio,1113.2,1112.2),
            "one-second playback and audio end-clock tolerance includes the boundary");
        std::cout << "PASS " << count << " core assertions\n";
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<"\n"; return 1; }
}
