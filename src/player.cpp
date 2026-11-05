#include "player.h"
#include <stdexcept>
#include <cmath>
namespace gv {
void Player::wakeup(void* self){auto p=static_cast<Player*>(self);if(p->wake_)p->wake_();}
void Player::check(int code){if(code<0)throw std::runtime_error(std::string("播放器：")+(error_string_?error_string_(code):"调用失败"));}
void Player::map_option(const char* name,const std::vector<std::pair<std::string,std::string>>& pairs){
    std::vector<mpv_node> values;std::vector<char*> keys;
    for(const auto& pair:pairs){keys.push_back(const_cast<char*>(pair.first.c_str()));mpv_node value={};value.format=MPV_FORMAT_STRING;value.u.string=const_cast<char*>(pair.second.c_str());values.push_back(value);}
    mpv_node_list list={static_cast<int>(values.size()),values.data(),keys.data()};mpv_node root={};root.format=MPV_FORMAT_NODE_MAP;root.u.list=&list;check(option_node_(handle_,name,MPV_FORMAT_NODE,&root));
}
void Player::initialize(HWND window,const std::wstring& dll,std::function<void()> wake,const SourceOptions& source) {
    if(handle_)return;
    library_=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!library_)throw std::runtime_error("无法加载 libmpv-2.dll，请保持便携目录完整，并使用支持该依赖的 Windows x64 系统");
#define LOAD(member,name) member=reinterpret_cast<decltype(member)>(GetProcAddress(library_,name));if(!member)throw std::runtime_error("libmpv 接口不完整：" name)
    LOAD(create_,"mpv_create");LOAD(initialize_,"mpv_initialize");LOAD(option_,"mpv_set_option_string");LOAD(option_node_,"mpv_set_option");LOAD(set_,"mpv_set_property");LOAD(get_,"mpv_get_property");LOAD(string_,"mpv_get_property_string");LOAD(free_,"mpv_free");LOAD(free_node_,"mpv_free_node_contents");LOAD(command_,"mpv_command");LOAD(node_,"mpv_command_node");LOAD(event_,"mpv_wait_event");LOAD(callback_,"mpv_set_wakeup_callback");LOAD(destroy_,"mpv_terminate_destroy");LOAD(error_string_,"mpv_error_string");
#undef LOAD
    handle_=create_();if(!handle_)throw std::runtime_error("无法创建播放器");
    auto opt=[&](const char* k,const std::string& v){check(option_(handle_,k,v.c_str()));};
    opt("config","no");opt("terminal","no");opt("ytdl",source.yt_dlp.empty()?"no":"yes");opt("load-scripts","no");opt("input-default-bindings","no");opt("input-vo-keyboard","no");opt("input-cursor","no");opt("osc","no");opt("osd-level","0");
    if(!source.yt_dlp.empty()){
        if(source.max_height!=360 && source.max_height!=480 && source.max_height!=720)throw std::runtime_error("画质上限无效");
        map_option("script-opts",{{"ytdl_hook-ytdl_path",utf8(source.yt_dlp)},{"ytdl_hook-try_ytdl_first","yes"},{"ytdl_hook-all_formats","no"},{"ytdl_hook-force_all_formats","no"},{"ytdl_hook-thumbnails","none"}});
        std::vector<std::pair<std::string,std::string>> raw={{"ignore-config",""},{"socket-timeout","10"},{"retries","0"},{"extractor-retries","0"},{"sub-langs","none"}};
        if(!source.cookies.empty())raw.emplace_back("cookies",utf8(source.cookies));map_option("ytdl-raw-options",raw);
        auto bound="[height<="+std::to_string(source.max_height)+"][fps<=?30]";
        opt("ytdl-format","bestvideo"+bound+"[vcodec^=avc1]+bestaudio/bestvideo"+bound+"+bestaudio/best"+bound);
    }
    opt("wid",std::to_string(reinterpret_cast<intptr_t>(window)));opt("vo","gpu");opt("gpu-api","d3d11");opt("hwdec","auto");opt("keep-open","yes");opt("idle","yes");opt("force-window","yes");opt("pause","yes");
    opt("cache","yes");opt("demuxer-max-bytes","16MiB");opt("demuxer-max-back-bytes","8MiB");opt("demuxer-readahead-secs","15");opt("cache-pause-wait","1");opt("network-timeout","10");opt("volume","35");opt("audio-pitch-correction","yes");opt("msg-level","all=no");
    check(initialize_(handle_));wake_=std::move(wake);callback_(handle_,&Player::wakeup,this);
}
void Player::load(const Media& media,double start,bool paused,const std::string& title){load_internal(media,start,paused,title,false);}
void Player::load_url(const std::string& url,double start,bool paused,const std::string& title){auto ref=parse_video_url(url);if(ref.short_url)throw std::runtime_error("请先解析短链接");Media media;media.video=ref.url();load_internal(media,start,paused,title,true);}
void Player::load_internal(const Media& media,double start,bool paused,const std::string& title,bool parse_url) {
    if(!handle_)throw std::runtime_error("播放器尚未初始化");
    drain();loaded_=false;error_.clear();expected_entry_=-1;
    parse_url_=parse_url;source_deadline_=0;
    if(!parse_url_)source_deadline_=extraction_deadline({{"requested_formats",json::array({{{"url",media.video}},{{"url",media.audio}}})}});
    std::vector<std::string> keys={"start","pause","force-media-title","ytdl"},values={std::to_string(start),paused?"yes":"no",title,parse_url?"yes":"no"};
    for(const auto& h:media.headers){if(h.first=="Referer"){keys.push_back("referrer");values.push_back(h.second);}else if(h.first=="User-Agent"){keys.push_back("user-agent");values.push_back(h.second);}else{keys.push_back("http-header-fields-append");values.push_back(h.first+": "+h.second);}}
    if(!media.audio.empty()){keys.push_back("audio-files-append");values.push_back(media.audio);}
    std::vector<char*> names;std::vector<mpv_node> opts;
    for(size_t i=0;i<keys.size();++i){names.push_back(const_cast<char*>(keys[i].c_str()));mpv_node n={};n.format=MPV_FORMAT_STRING;n.u.string=const_cast<char*>(values[i].c_str());opts.push_back(n);}
    mpv_node_list optsList={static_cast<int>(opts.size()),opts.data(),names.data()};
    std::string command="loadfile",replace="replace";mpv_node args[5]={};
    for(int i=0;i<3;++i)args[i].format=MPV_FORMAT_STRING;
    args[0].u.string=&command[0];args[1].u.string=const_cast<char*>(media.video.c_str());args[2].u.string=&replace[0];
    args[3].format=MPV_FORMAT_INT64;args[3].u.int64=-1;args[4].format=MPV_FORMAT_NODE_MAP;args[4].u.list=&optsList;
    mpv_node_list list={5,args,nullptr};mpv_node root={};root.format=MPV_FORMAT_NODE_ARRAY;root.u.list=&list;
    mpv_node result={};int code=node_(handle_,&root,&result);
    if(code>=0 && result.format==MPV_FORMAT_NODE_MAP){auto map=result.u.list;for(int i=0;i<map->num;++i)if(std::string(map->keys[i])=="playlist_entry_id" && map->values[i].format==MPV_FORMAT_INT64)expected_entry_=map->values[i].u.int64;}
    free_node_(&result);check(code);
    if(expected_entry_<0){stop();throw std::runtime_error("播放器未返回媒体条目标识，请检查 libmpv 版本");}
    trace_event("mpv-load-entry",expected_entry_);
}
void Player::stop(){expected_entry_=-1;loaded_=false;source_deadline_=0;error_.clear();if(handle_){const char* args[]={"stop",nullptr};check(command_(handle_,args));}}
void Player::set_pause(bool paused){if(handle_){int b=paused?1:0;check(set_(handle_,"pause",MPV_FORMAT_FLAG,&b));}}
void Player::seek(double delta){if(handle_){auto s=snapshot();if(!s.loaded || !s.position_valid)return;auto target=std::to_string(clamp_seek(s.position,delta,s.duration));const char* args[]={"seek",target.c_str(),"absolute+exact",nullptr};check(command_(handle_,args));}}
void Player::volume(double value){if(handle_)check(set_(handle_,"volume",MPV_FORMAT_DOUBLE,&value));}
void Player::set_speed(double value){if(!valid_playback_speed(value))throw std::runtime_error("播放速度无效");if(handle_)check(set_(handle_,"speed",MPV_FORMAT_DOUBLE,&value));}
std::string Player::text(const char* property){char* p=string_(handle_,property);if(!p)return "";std::string result(p);free_(p);return result;}
void Player::drain(){if(!handle_)return;while(auto e=event_(handle_,0)){
    if(e->event_id==MPV_EVENT_NONE)break;
    if(e->event_id==MPV_EVENT_START_FILE){auto start=static_cast<mpv_event_start_file*>(e->data);active_entry_=start?start->playlist_entry_id:-1;}
    if(e->event_id==MPV_EVENT_FILE_LOADED && active_entry_==expected_entry_ && expected_entry_>=0){
        loaded_=true;error_.clear();
        if(parse_url_){
            auto result=json::parse(text("user-data/mpv/ytdl/json-subprocess-result"),nullptr,false);
            if(result.is_object()){
                auto output=result.find("stdout");
                if(output!=result.end() && output->is_string())source_deadline_=extraction_deadline(json::parse(output->get<std::string>(),nullptr,false));
            }
        }
        if(source_deadline_>0)trace_event("source-deadline",source_deadline_);
    }
    if(e->event_id==MPV_EVENT_END_FILE){auto end=static_cast<mpv_event_end_file*>(e->data);if(end && end->playlist_entry_id==expected_entry_ && expected_entry_>=0){loaded_=false;if(end->reason==MPV_END_FILE_REASON_ERROR)error_=std::string("媒体连接失败：")+error_string_(end->error);}}
}}
Snapshot Player::snapshot(){Snapshot s;s.loaded=loaded_;s.error=error_;s.entry_id=expected_entry_;s.source_deadline=source_deadline_;if(!handle_)return s;int value=1;get_(handle_,"pause",MPV_FORMAT_FLAG,&value);s.paused=value!=0;value=0;get_(handle_,"eof-reached",MPV_FORMAT_FLAG,&value);s.eof=value!=0;value=0;get_(handle_,"seeking",MPV_FORMAT_FLAG,&value);s.seeking=value!=0;s.position_status=get_(handle_,"time-pos",MPV_FORMAT_DOUBLE,&s.position);s.position_valid=s.loaded && s.position_status>=0 && std::isfinite(s.position) && s.position>=0;get_(handle_,"duration",MPV_FORMAT_DOUBLE,&s.duration);get_(handle_,"speed",MPV_FORMAT_DOUBLE,&s.speed);int64_t height=0;get_(handle_,"video-params/h",MPV_FORMAT_INT64,&height);s.height=static_cast<int>(height);s.audio_codec=text("audio-codec-name");s.hwdec=text("hwdec-current");return s;}
void Player::shutdown(){if(handle_){if(callback_)callback_(handle_,nullptr,nullptr);if(destroy_)destroy_(handle_);handle_=nullptr;}wake_={};if(library_){FreeLibrary(library_);library_=nullptr;}loaded_=false;source_deadline_=0;expected_entry_=active_entry_=-1;error_.clear();}
}
