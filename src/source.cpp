#include "source.h"
namespace gv {
static json api(const std::wstring& url,Cancellation& cancel) {
    auto response=http_get(url,cancel);
    if(response.status==412 || response.status==429)throw std::runtime_error("B 站暂时限制访问，请稍后手动重试");
    if(response.status!=200)throw std::runtime_error("B 站请求失败，HTTP "+std::to_string(response.status));
    auto j=json::parse(response.body);
    if(j.value("code",-1)!=0)throw std::runtime_error("B 站未返回可用视频信息（可能需要登录或视频已不可用）");
    return j.at("data");
}
Guide fetch_guide(VideoRef& ref,Cancellation& cancel) {
    for(int i=0;ref.short_url && i<5;++i) {
        auto response=http_get(wide(ref.url()),cancel);
        if(response.location.empty())throw std::runtime_error("短链接未跳转到可识别的视频");
        ref=parse_video_url(utf8(response.location));
    }
    if(ref.short_url)throw std::runtime_error("短链接跳转次数过多");
    auto data=api(L"https://api.bilibili.com/x/web-interface/view?bvid="+wide(ref.bvid),cancel);
    Guide g;g.bvid=ref.bvid;g.title=data.value("title","");
    for(const auto& p:data.at("pages"))g.parts.push_back({p.at("page").get<int>(),p.at("cid").get<long long>(),p.value("part",""),p.value("duration",0.0)});
    if(g.parts.empty() || g.parts.size()>10000)throw std::runtime_error("视频分 P 列表无效");
    return g;
}
Media fetch_public_media(const Guide& guide,const Part& part,int height,Cancellation& cancel) {
    auto url=L"https://api.bilibili.com/x/player/playurl?bvid="+wide(guide.bvid)+L"&cid="+std::to_wstring(part.cid)+L"&qn=64&fnval=4048&fourk=0";
    auto data=api(url,cancel);json info={{"formats",json::array()},{"http_headers",{{"Referer","https://www.bilibili.com/video/"+guide.bvid+"/"},{"User-Agent","Mozilla/5.0"}}}};
    if(data.contains("dash") && data["dash"].is_object()) {
        const auto& dash=data["dash"];
        if(dash.contains("video") && dash["video"].is_array())for(const auto& v:dash["video"]) {
            auto rate=v.value("frameRate",std::string("30"));double fps=30;try{auto slash=rate.find('/');fps=std::stod(rate);if(slash!=std::string::npos)fps/=std::stod(rate.substr(slash+1));}catch(...){}
            info["formats"].push_back({{"url",v.value("baseUrl",v.value("base_url",std::string()))},{"height",v.value("height",0)},{"fps",fps},{"vcodec",v.value("codecs","")},{"acodec","none"}});
        }
        if(dash.contains("audio") && dash["audio"].is_array())for(const auto& a:dash["audio"])info["formats"].push_back({{"url",a.value("baseUrl",a.value("base_url",std::string()))},{"vcodec","none"},{"acodec",a.value("codecs","aac")},{"abr",a.value("bandwidth",64000)/1000.0}});
    }
    auto result=select_media(info,height);result.note="已使用公开视频接口；高清画质可尝试导入登录信息";return result;
}
}
