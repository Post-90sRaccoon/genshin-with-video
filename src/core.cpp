#include "core.h"
#include <regex>
#include <algorithm>
#include <cmath>
#include <tuple>
namespace gv {
std::string VideoRef::url() const {
    if(short_url) return original;
    return "https://www.bilibili.com/video/"+bvid+"/"+(page ? "?p="+std::to_string(page):"");
}
VideoRef parse_video_url(const std::string& input) {
    auto a=input.find_first_not_of(" \t\r\n"), b=input.find_last_not_of(" \t\r\n");
    if(a==std::string::npos) throw std::runtime_error("请输入 B 站视频链接或 BV 号");
    std::string s=input.substr(a,b-a+1);
    if(s.size()>4096 || s.find_first_of("\r\n\"\\")!=std::string::npos) throw std::runtime_error("链接包含无效字符");
    std::smatch m;
    if(std::regex_match(s,m,std::regex("[bB][vV]([A-Za-z0-9]{10})"))) return {"BV"+m[1].str(),0,false,""};
    if(std::regex_match(s,std::regex("https?://b23\\.tv/[A-Za-z0-9]{3,32}/?(?:[?#][^ ]*)?",std::regex::icase)))
        return {"",0,true,s};
    if(!std::regex_match(s,m,std::regex("https?://(?:www\\.|m\\.)?bilibili\\.com/video/[bB][vV]([A-Za-z0-9]{10})/?(?:\\?([^# ]*))?(?:#[^ ]*)?",std::regex::icase)))
        throw std::runtime_error("请使用 bilibili.com/video/BV… 或 b23.tv 视频链接");
    VideoRef ref={"BV"+m[1].str(),0,false,""};
    std::string query=m[2].str(); bool seen=false;
    size_t offset=0;
    while(offset<query.size()) {
        size_t end=query.find('&',offset); if(end==std::string::npos) end=query.size();
        auto field=query.substr(offset,end-offset);
        if(field=="p" || field.compare(0,2,"p=")==0) {
            auto value=field.size()>2?field.substr(2):"";
            if(seen || !std::regex_match(value,std::regex("[1-9][0-9]{0,3}"))) throw std::runtime_error("分 P 参数无效或重复");
            ref.page=std::stoi(value); seen=true;
        }
        offset=end+1;
    }
    return ref;
}
static double number(const json& j,const char* key,double fallback=0) {
    auto it=j.find(key); return it!=j.end() && it->is_number()?it->get<double>():fallback;
}
static std::string string(const json& j,const char* key) {
    auto it=j.find(key); return it!=j.end() && it->is_string()?it->get<std::string>():"";
}
Media select_media(const json& info,int max_height) {
    if(!info.contains("formats") || !info["formats"].is_array()) throw std::runtime_error("解析结果没有视频流");
    const json* video=nullptr; const json* audio=nullptr;
    std::tuple<int,int,double> best(-1,-1,-1);
    double best_audio=-1;
    for(const auto& f:info["formats"]) {
        auto url=string(f,"url");
        if(url.compare(0,8,"https://") && url.compare(0,7,"http://")) continue;
        auto vc=string(f,"vcodec"), ac=string(f,"acodec");
        int height=static_cast<int>(number(f,"height"));
        if(vc!="none" && height>0 && height<=max_height && number(f,"fps",30)<=30.1) {
            int codec=(vc.find("avc")==0 || vc.find("h264")==0)?3:(vc.find("hev")==0 || vc.find("hvc")==0)?2:1;
            auto score=std::make_tuple(height,codec,number(f,"fps",30));
            if(score>best) {best=score;video=&f;}
        } else if(vc=="none" && ac!="none" && !ac.empty()) {
            double abr=number(f,"abr",64),score=abr<=130?abr:130-(abr-130);
            if(score>best_audio) {best_audio=score;audio=&f;}
        }
    }
    if(!video) throw std::runtime_error("没有符合画质上限的 30 帧视频流，请调整画质或登录信息");
    Media result; result.video=string(*video,"url"); result.height=static_cast<int>(number(*video,"height")); result.codec=string(*video,"vcodec");
    if(string(*video,"acodec")=="none") {
        if(!audio) throw std::runtime_error("未找到配套音频流");
        result.audio=string(*audio,"url");
    }
    json headers=info.value("http_headers",json::object());
    if(video->contains("http_headers") && (*video)["http_headers"].is_object()) headers.update((*video)["http_headers"]);
    if(headers.is_object()) for(auto it=headers.begin();it!=headers.end();++it) {
        if(!it.value().is_string()) continue;
        auto value=it.value().get<std::string>();
        if(it.key().find_first_of("\r\n:")!=std::string::npos || value.find_first_of("\r\n")!=std::string::npos) throw std::runtime_error("媒体请求头无效");
        result.headers.emplace_back(it.key(),value);
    }
    return result;
}
Resume choose_resume(const VideoRef& ref,const json& history,const std::vector<Part>& parts) {
    if(parts.empty()) throw std::runtime_error("视频没有可播放分 P");
    const auto& h=history.is_object() && history.contains(ref.bvid) && history[ref.bvid].is_object()?history[ref.bvid]:history;
    Resume r;
    if(ref.page) {
        auto it=std::find_if(parts.begin(),parts.end(),[&](const Part& p){return p.page==ref.page;});
        if(it==parts.end()) throw std::runtime_error("指定的分 P 不存在");
        r.index=static_cast<size_t>(it-parts.begin());
    } else if(h.is_object() && h.contains("last_cid") && h["last_cid"].is_number_integer()) {
        auto cid=h["last_cid"].get<long long>();
        for(size_t i=0;i<parts.size();++i) if(parts[i].cid==cid) {r.index=i;break;}
    }
    auto key=std::to_string(parts[r.index].cid);
    if(h.is_object() && h.contains("parts") && h["parts"].is_object() && h["parts"].contains(key) && h["parts"][key].is_number())
        r.seconds=h["parts"][key].get<double>();
    if(!std::isfinite(r.seconds) || r.seconds<0) r.seconds=0;
    if(parts[r.index].duration>0) r.seconds=std::min(r.seconds,parts[r.index].duration);
    return r;
}
double clamp_seek(double position,double delta,double duration) {
    if(!std::isfinite(position)) position=0;
    double target=std::max(0.0,position+delta);
    return duration>0?std::min(target,duration):target;
}
long long stream_deadline(const std::string& url) {
    auto query=url.find('?'),fragment=url.find('#');
    if(query==std::string::npos || (fragment!=std::string::npos && fragment<query))return 0;
    auto end=fragment==std::string::npos?url.size():fragment;
    long long deadline=0;
    for(auto offset=query+1;offset<end;){
        auto next=url.find('&',offset);if(next==std::string::npos || next>end)next=end;
        auto field=url.substr(offset,next-offset);
        if(field.compare(0,9,"deadline=")==0){
            auto value=field.substr(9);
            if(!value.empty() && value.find_first_not_of("0123456789")==std::string::npos){
                try{auto parsed=std::stoll(value);if(parsed>0 && (!deadline || parsed<deadline))deadline=parsed;}catch(const std::exception&){}
            }
        }
        offset=next+1;
    }
    return deadline;
}
long long extraction_deadline(const json& info) {
    if(!info.is_object())return 0;
    long long deadline=0;
    auto include=[&](const json& format){
        if(!format.is_object())return;
        auto value=stream_deadline(string(format,"url"));
        if(value>0 && (!deadline || value<deadline))deadline=value;
    };
    auto formats=info.find("requested_formats");
    if(formats!=info.end() && formats->is_array())for(const auto& format:*formats)include(format);
    else include(info);
    return deadline;
}
}
