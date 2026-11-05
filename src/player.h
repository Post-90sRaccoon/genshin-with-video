#pragma once
#include "platform.h"
#include <functional>
#include <mpv/client.h>
namespace gv {
struct Snapshot {bool loaded=false,paused=true,eof=false,position_valid=false,seeking=false;double position=0,duration=0,speed=1.0;int height=0,position_status=MPV_ERROR_PROPERTY_UNAVAILABLE;int64_t entry_id=-1;long long source_deadline=0;std::string audio_codec,hwdec,error;};
struct SourceOptions {std::wstring yt_dlp,cookies;int max_height=720;};
class Player {
    HMODULE library_=nullptr;mpv_handle* handle_=nullptr;std::function<void()> wake_;
    bool loaded_=false,parse_url_=false;int64_t expected_entry_=-1,active_entry_=-1;long long source_deadline_=0;std::string error_;
    decltype(&mpv_create) create_=nullptr;
    decltype(&mpv_initialize) initialize_=nullptr;
    decltype(&mpv_set_option_string) option_=nullptr;
    decltype(&mpv_set_option) option_node_=nullptr;
    decltype(&mpv_set_property) set_=nullptr;
    decltype(&mpv_get_property) get_=nullptr;
    decltype(&mpv_get_property_string) string_=nullptr;
    decltype(&mpv_free) free_=nullptr;
    decltype(&mpv_free_node_contents) free_node_=nullptr;
    decltype(&mpv_command) command_=nullptr;
    decltype(&mpv_command_node) node_=nullptr;
    decltype(&mpv_wait_event) event_=nullptr;
    decltype(&mpv_set_wakeup_callback) callback_=nullptr;
    decltype(&mpv_terminate_destroy) destroy_=nullptr;
    decltype(&mpv_error_string) error_string_=nullptr;
    static void wakeup(void* self);
    void check(int code);
    void map_option(const char* name,const std::vector<std::pair<std::string,std::string>>& values);
    void load_internal(const Media& media,double start,bool paused,const std::string& title,bool parse_url);
    std::string text(const char* property);
public:
    ~Player(){shutdown();}
    void initialize(HWND window,const std::wstring& dll,std::function<void()> wake,const SourceOptions& source=SourceOptions{});
    bool initialized() const {return handle_!=nullptr;}
    void load(const Media& media,double start,bool paused,const std::string& title);
    void load_url(const std::string& url,double start,bool paused,const std::string& title);
    void stop();void set_pause(bool paused);void seek(double delta);void volume(double value);void set_speed(double value);
    void drain();Snapshot snapshot();void shutdown();
};
}
