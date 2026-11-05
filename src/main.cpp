#include "dialogs.h"
#include "source.h"
#include "player.h"
#include <shellapi.h>
#include <commctrl.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <thread>
#include <memory>
#include <deque>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>
#include <cmath>
using namespace gv;
namespace {
constexpr UINT JOB_DONE=WM_APP+1,MEDIA_EVENT=WM_APP+2,TRAY_EVENT=WM_APP+3,SHOW_EXISTING=WM_APP+4,FRAME_READY=WM_APP+5;
const UINT TASKBAR_CREATED=RegisterWindowMessageW(L"TaskbarCreated");
enum Command { OPEN=100,PARTS,SETTINGS,HIDE,PAUSE,PREVIOUS,NEXT,RETRY,FROM_START,PASS_THROUGH,QUIT,SPEED,MENU_BAR };
constexpr int SPEED_FIRST=200;
enum class RetrySource { Extractor,PublicApi };
struct Result {unsigned generation=0;VideoRef ref;std::shared_ptr<Guide> guide;Resume resume;Media media;bool public_streams=false;std::string error;};
struct Job {std::shared_ptr<Cancellation> cancel=std::make_shared<Cancellation>();std::atomic<bool> done{false};std::thread thread;};
std::wstring clock_text(double seconds){int t=static_cast<int>(std::max(0.0,seconds));wchar_t s[48];swprintf_s(s,L"%02d:%02d",t/60,t%60);return s;}
std::wstring speed_text(double speed){std::wostringstream text;text<<speed<<L"×";return text.str();}
class App {
public:
    HWND window=nullptr,video=nullptr,statusLabel=nullptr;HFONT font=nullptr;Store store;Settings settings;Player player;
    std::shared_ptr<Guide> guide;size_t index=0;VideoRef requested;
    std::vector<std::unique_ptr<Job>> jobs;std::mutex resultsMutex;std::deque<std::unique_ptr<Result>> results;
    std::atomic<bool> closing{false};unsigned generation=0;bool busy=false,desiredPaused=false,hidden=false,pass=false,modal=false,refreshUsed=false,applyIntentOnLoad=false,hadPlayback=false;
    std::array<bool,HOTKEY_COUNT> registered{};std::string message,lastError,storageWarning,hotkeyWarning;std::wstring status;Snapshot state;double lastPosition=0;int lastSave=0;
    UINT dpi=96;int top=34,bottom=25;std::array<HWND,6> buttons{};NOTIFYICONDATAW tray={};bool trayAdded=false;ULONGLONG loadStarted=0;
    long long renewedDeadline=0;
    std::optional<RECT> resizeScreen,dpiScreen;bool resizing=false;
    RECT frameInsets={};UINT frameDpi=96;bool frameMeasured=false;
    explicit App():store(executable_dir()+L"\\data"),settings(read_settings(store.data["settings"])) {storageWarning=store.warning;}
    ~App(){closing=true;for(auto& j:jobs)j->cancel->cancel();for(auto& j:jobs)if(j->thread.joinable())j->thread.join();if(font)DeleteObject(font);}
    int scale(int v) const{return MulDiv(v,dpi,96);}
    void set_font(){if(font)DeleteObject(font);font=CreateFontW(-scale(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");for(auto b:buttons)if(b)SendMessageW(b,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(statusLabel)SendMessageW(statusLabel,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}
    void init(){
        dpi=GetDpiForWindow(window);top=settings.menu_bar_visible?scale(34):0;bottom=scale(25);set_font();
        video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,top,480,270,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        const wchar_t* labels[]={L"打开",L"选集",L"设置",L"隐藏",L"播放",L"1×"};int ids[]={OPEN,PARTS,SETTINGS,HIDE,PAUSE,SPEED};
        for(size_t i=0;i<buttons.size();++i)buttons[i]=CreateWindowExW(0,L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[i])),GetModuleHandleW(nullptr),nullptr);
        statusLabel=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_ENDELLIPSIS|SS_CENTERIMAGE,0,0,0,0,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        set_font();register_keys();
        add_tray();
        if(!trayAdded && !registered[1])message="显示热键不可用，且无法创建托盘图标；请先在设置中更换热键";
        layout();update_status();
    }
    void add_tray(){tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=TRAY_EVENT;tray.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wcscpy_s(tray.szTip,L"原神攻略小窗 · 右键控制 / 显示窗口");trayAdded=Shell_NotifyIconW(NIM_ADD,&tray)!=FALSE;if(hidden && !trayAdded && !registered[1])show_existing();}
    void show_existing(){if(hidden)toggle_hidden();if(pass)set_pass(false);keep_on_screen();ShowWindow(window,SW_SHOWNOACTIVATE);SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);PostMessageW(window,FRAME_READY,0,0);}
    RECT screen_area(const RECT* rect=nullptr) const{
        MONITORINFO info={sizeof(info)};
        HMONITOR monitor=rect?MonitorFromRect(rect,MONITOR_DEFAULTTONEAREST):MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
        if(GetMonitorInfoW(monitor,&info))return info.rcMonitor;
        return {0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
    }
    RECT active_screen_area() const{if(dpiScreen)return *dpiScreen;return resizing && resizeScreen?*resizeScreen:screen_area();}
    RECT invisible_frame() const{
        if(frameMeasured)return {MulDiv(frameInsets.left,dpi,frameDpi),MulDiv(frameInsets.top,dpi,frameDpi),MulDiv(frameInsets.right,dpi,frameDpi),MulDiv(frameInsets.bottom,dpi,frameDpi)};
        BOOL composition=FALSE;DWORD style=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE));
        if(FAILED(DwmIsCompositionEnabled(&composition)) || !composition || !(style&WS_THICKFRAME))return {};
        RECT frame={};AdjustWindowRectExForDpi(&frame,style,GetMenu(window)!=nullptr,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi);
        // Before the first composed frame, leave the visible border inside the screen.
        return {std::max<LONG>(0,-frame.left-scale(1)),0,std::max<LONG>(0,frame.right-scale(1)),std::max<LONG>(0,frame.bottom-scale(1))};
    }
    RECT outer_bounds(const RECT& screen) const{RECT margin=invisible_frame();return {screen.left-margin.left,screen.top-margin.top,screen.right+margin.right,screen.bottom+margin.bottom};}
    RECT visible_rect(const RECT& outer) const{RECT margin=invisible_frame();return {outer.left+margin.left,outer.top+margin.top,outer.right-margin.right,outer.bottom-margin.bottom};}
    void refresh_frame_insets(bool alignDocked=true){
        if(!IsWindowVisible(window))return;
        // Cache only a synchronized frame; querying DWM on every drag message can lag the live HWND.
        BOOL composition=FALSE;if(FAILED(DwmIsCompositionEnabled(&composition)))return;
        if(composition)DwmFlush();RECT outer,visible,frame={};if(!GetWindowRect(window,&outer))return;
        if(composition){if(FAILED(DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&visible,sizeof(visible))))return;}else visible=outer;
        UINT actualDpi=GetDpiForWindow(window);if(!actualDpi)return;
        AdjustWindowRectExForDpi(&frame,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),GetMenu(window)!=nullptr,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)),actualDpi);
        RECT margin={visible.left-outer.left,visible.top-outer.top,outer.right-visible.right,outer.bottom-visible.bottom};
        if(visible.right<=visible.left || visible.bottom<=visible.top || margin.left<0 || margin.top<0 || margin.right<0 || margin.bottom<0 || margin.left>-frame.left || margin.top>-frame.top || margin.right>frame.right || margin.bottom>frame.bottom)return;
        RECT screen=active_screen_area(),before=outer_bounds(screen);frameInsets=margin;frameDpi=actualDpi;frameMeasured=true;
        if(alignDocked){
            RECT after=outer_bounds(screen),adjusted=outer;LONG dx=0,dy=0;
            if(std::abs(outer.right-before.right)<=1)dx=after.right-outer.right;else if(std::abs(outer.left-before.left)<=1)dx=after.left-outer.left;
            if(std::abs(outer.bottom-before.bottom)<=1)dy=after.bottom-outer.bottom;else if(std::abs(outer.top-before.top)<=1)dy=after.top-outer.top;
            OffsetRect(&adjusted,dx,dy);fit_rect(adjusted,screen);
            if(!EqualRect(&adjusted,&outer))SetWindowPos(window,nullptr,adjusted.left,adjusted.top,adjusted.right-adjusted.left,adjusted.bottom-adjusted.top,SWP_NOZORDER|SWP_NOACTIVATE);
        }
    }
    void fit_rect(RECT& rect,const RECT& screen){
        RECT work=outer_bounds(screen);
        if(work.right<=work.left || work.bottom<=work.top)return;
        LONG width=rect.right-rect.left,height=rect.bottom-rect.top;
        if(width<1 || height<1 || width>work.right-work.left || height>work.bottom-work.top){
            RECT fit={work.left,work.top,work.left+std::max<LONG>(1,width),work.top+std::max<LONG>(1,height)};
            constrain_size(WMSZ_BOTTOMRIGHT,fit,&screen);width=fit.right-fit.left;height=fit.bottom-fit.top;
        }
        LONG x=std::clamp(rect.left,work.left,work.right-width),y=std::clamp(rect.top,work.top,work.bottom-height);
        rect={x,y,x+width,y+height};
    }
    void keep_on_screen(){
        RECT before;if(!GetWindowRect(window,&before))return;RECT rect=before;fit_rect(rect,active_screen_area());
        if(!EqualRect(&rect,&before))SetWindowPos(window,HWND_TOPMOST,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOACTIVATE);
    }
    void layout(){
        top=settings.menu_bar_visible?scale(34):0;
        RECT r;GetClientRect(window,&r);int x=scale(18),gap=scale(4),width=std::min(scale(55),std::max(1,(static_cast<int>(r.right)-scale(26)-gap*5)/6));
        for(auto b:buttons){if(b){MoveWindow(b,x,scale(4),width,scale(26),TRUE);ShowWindow(b,settings.menu_bar_visible?SW_SHOWNOACTIVATE:SW_HIDE);}x+=width+gap;}
        if(video)MoveWindow(video,0,top,r.right,std::max<LONG>(1,r.bottom-top-bottom),TRUE);
        if(statusLabel)MoveWindow(statusLabel,scale(8),r.bottom-bottom,std::max<LONG>(1,r.right-scale(16)),bottom,TRUE);
        InvalidateRect(window,nullptr,FALSE);
    }
    void constrain_size(UINT edge,RECT& rect,const RECT* screen=nullptr){
        RECT allowed={};const RECT* work=nullptr;if(screen){allowed=outer_bounds(*screen);work=&allowed;}
        RECT frame={};
        AdjustWindowRectExForDpi(&frame,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),GetMenu(window)!=nullptr,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi);
        int frameWidth=static_cast<int>(frame.right-frame.left),overhead=static_cast<int>(frame.bottom-frame.top)+top+bottom;
        int minVideoHeight=std::max(1,scale(230)-overhead);
        int minVideoWidth=std::max(scale(330)-frameWidth,MulDiv(minVideoHeight,16,9));
        // Top/bottom drags supply height; side and corner drags supply width.
        bool vertical=edge==WMSZ_TOP || edge==WMSZ_BOTTOM;
        int proposed=vertical?MulDiv(rect.bottom-rect.top-overhead,16,9):static_cast<int>(rect.right-rect.left)-frameWidth;
        int videoWidth=std::max(minVideoWidth,proposed);
        bool left=edge==WMSZ_LEFT || edge==WMSZ_TOPLEFT || edge==WMSZ_BOTTOMLEFT;
        bool upper=edge==WMSZ_TOP || edge==WMSZ_TOPLEFT || edge==WMSZ_TOPRIGHT;
        LONG anchorX=left?rect.right:rect.left,anchorY=upper?rect.bottom:rect.top;
        if(work){
            int availableWidth=static_cast<int>(work->right-work->left)-frameWidth,availableHeight=static_cast<int>(work->bottom-work->top)-overhead;
            if(availableWidth<1 || availableHeight<1){rect=*work;return;}
            // MulDiv rounds height to nearest: this inverse also accepts its last fitting pixel.
            auto max_width=[](int width,int height){return std::min(width,(height*16+7)/9);};
            int fullMaximum=max_width(availableWidth,availableHeight);
            minVideoWidth=std::min(minVideoWidth,fullMaximum);
            int minWidth=minVideoWidth+frameWidth,minHeight=MulDiv(minVideoWidth,9,16)+overhead;
            anchorX=left?std::clamp<LONG>(anchorX,work->left+minWidth,work->right):std::clamp<LONG>(anchorX,work->left,work->right-minWidth);
            anchorY=upper?std::clamp<LONG>(anchorY,work->top+minHeight,work->bottom):std::clamp<LONG>(anchorY,work->top,work->bottom-minHeight);
            availableWidth=static_cast<int>(left?anchorX-work->left:work->right-anchorX)-frameWidth;
            availableHeight=static_cast<int>(upper?anchorY-work->top:work->bottom-anchorY)-overhead;
            int maximum=max_width(availableWidth,availableHeight);
            videoWidth=std::clamp(proposed,minVideoWidth,maximum);
        }
        int width=videoWidth+frameWidth,height=MulDiv(videoWidth,9,16)+overhead;
        rect.left=left?anchorX-width:anchorX;rect.right=rect.left+width;
        rect.top=upper?anchorY-height:anchorY;rect.bottom=rect.top+height;
    }
    void unregister_keys(){for(int i=0;i<HOTKEY_COUNT;++i){UnregisterHotKey(window,500+i);registered[i]=false;}}
    void register_keys(){unregister_keys();std::wstring failed;for(int i=0;i<HOTKEY_COUNT;++i){registered[i]=RegisterHotKey(window,500+i,hotkey_modifiers(settings.hotkeys[i]),LOBYTE(settings.hotkeys[i]))!=FALSE;if(!registered[i]){if(!failed.empty())failed+=L"、";failed+=hotkey_name(settings.hotkeys[i]);}}if(!registered[0] && pass)set_pass(false);hotkeyWarning=failed.empty()?"":"快捷键被占用："+utf8(failed)+"；请在设置中更换";}
    void geometry(){if(!IsWindow(window))return;RECT r;GetWindowRect(window,&r);fit_rect(r,active_screen_area());store.data["window"]={{"x",r.left},{"y",r.top},{"width",r.right-r.left},{"height",r.bottom-r.top}};}
    void save(){if(guide && state.loaded && state.position_valid && !busy){lastPosition=state.position;store.remember(guide->bvid,guide->parts.at(index).cid,state.position);}geometry();store.data["settings"]=write_settings(settings);store.save();}
    void save_safely(){try{save();storageWarning.clear();}catch(const std::exception& e){storageWarning=e.what();}update_status();}
    void cancel_jobs(){for(auto& j:jobs)if(!j->done)j->cancel->cancel();}
    void cleanup_jobs(){jobs.erase(std::remove_if(jobs.begin(),jobs.end(),[](std::unique_ptr<Job>& j){if(!j->done)return false;if(j->thread.joinable())j->thread.join();return true;}),jobs.end());}
    void open(const VideoRef& ref,bool keep_pause=false,bool refresh=false,std::optional<double> start={},RetrySource retry=RetrySource::PublicApi){
        if(modal || closing)return;
        if(player.initialized()){state=player.snapshot();if(state.loaded && state.position_valid)lastPosition=state.position;}
        save_safely();cancel_jobs();cleanup_jobs();
        if(start && guide && guide->bvid==ref.bvid){auto p=std::find_if(guide->parts.begin(),guide->parts.end(),[&](const Part& item){return item.page==ref.page;});if(p!=guide->parts.end()){store.remember(guide->bvid,p->cid,*start);try{store.save();}catch(const std::exception& e){storageWarning=e.what();}}}
        loadStarted=0;applyIntentOnLoad=false;
        if(!refresh){refreshUsed=false;hadPlayback=false;renewedDeadline=0;}
        if(!keep_pause)desiredPaused=false;
        if(hidden)desiredPaused=true;
        requested=ref;busy=true;lastError.clear();message="正在获取攻略与当前分 P…";state=Snapshot{};
        trace_event("metadata-request",ref.page);
        if(player.initialized())player.stop();ShowWindow(video,SW_HIDE);KillTimer(window,1);update_status();
        auto gen=++generation;auto history=store.data["history"];auto cached=guide && guide->bvid==ref.bvid?guide:std::shared_ptr<Guide>();
        auto quality=settings.quality;
        auto j=std::make_unique<Job>();Job* job=j.get();auto token=j->cancel;
        double refreshPosition=lastPosition;
        j->thread=std::thread([this,job,token,ref,gen,history,cached,quality,refresh,refreshPosition,start,retry]{
            auto r=std::make_unique<Result>();r->generation=gen;r->ref=ref;
            try{
                r->guide=cached?cached:std::make_shared<Guide>(fetch_guide(r->ref,*token));
                r->resume=choose_resume(r->ref,history,r->guide->parts);if(refresh)r->resume.seconds=refreshPosition;if(start)r->resume.seconds=clamp_seek(*start,0,r->guide->parts[r->resume.index].duration);
                if(refresh && retry==RetrySource::PublicApi){r->public_streams=true;r->media=fetch_public_media(*r->guide,r->guide->parts[r->resume.index],quality,*token);}
            }catch(const Cancelled&){r->error="操作已取消";}catch(const std::exception& e){r->error=e.what();}
            {std::lock_guard<std::mutex> l(resultsMutex);results.push_back(std::move(r));}
            job->done=true;if(!closing)PostMessageW(window,JOB_DONE,0,0);
        });
        jobs.push_back(std::move(j));
    }
    void accept_results(){
        if(modal || closing)return;
        std::deque<std::unique_ptr<Result>> ready;{std::lock_guard<std::mutex> l(resultsMutex);ready.swap(results);}
        for(auto& r:ready){if(r->generation!=generation || closing)continue;busy=false;
            if(!r->error.empty()){fail(r->error);continue;}
            guide=r->guide;index=r->resume.index;requested=r->ref;requested.bvid=guide->bvid;requested.short_url=false;requested.page=guide->parts[index].page;lastPosition=r->resume.seconds;
            store.data["last_video"]=VideoRef{guide->bvid,0,false,""}.url();message.clear();
            store.data.erase("last_extraction");store.data["last_source"]=r->public_streams?"public_api":"mpv_ytdl";
            try{store.save();}catch(const std::exception& e){storageWarning=e.what();}
            if(r->public_streams && r->media.height<settings.quality)message="当前可用画质 "+std::to_string(r->media.height)+"p";
            auto dll=executable_dir()+L"\\libmpv-2.dll";if(GetFileAttributesW(dll.c_str())==INVALID_FILE_ATTRIBUTES)dll=executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll";
            SourceOptions source;source.max_height=settings.quality;source.cookies=settings.cookies;source.yt_dlp=executable_dir()+L"\\yt-dlp.exe";
            if(GetFileAttributesW(source.yt_dlp.c_str())==INVALID_FILE_ATTRIBUTES)source.yt_dlp=executable_dir()+L"\\..\\third_party\\yt-dlp.exe";
            player.initialize(video,dll,[this]{if(!closing)PostMessageW(window,MEDIA_EVENT,0,0);},source);player.volume(settings.volume);player.set_speed(settings.speed);
            ShowWindow(video,SW_SHOWNOACTIVATE);applyIntentOnLoad=true;loadStarted=GetTickCount64();trace_event(r->public_streams?"public-stream-load":"mpv-url-load",requested.page);
            if(r->public_streams)player.load(r->media,r->resume.seconds,desiredPaused||hidden,guide->parts[index].title);else player.load_url(requested.url(),r->resume.seconds,desiredPaused||hidden,guide->parts[index].title);
            SetTimer(window,1,1000,nullptr);lastSave=0;
        }
        cleanup_jobs();update_status();
    }
    void fail(const std::string& error){busy=false;loadStarted=0;applyIntentOnLoad=false;lastError=error;message=error;ShowWindow(video,SW_HIDE);update_status();InvalidateRect(window,nullptr,TRUE);}
    bool renew_source(long long now){
        if(busy || modal || hidden || desiredPaused || !lastError.empty() || !state.error.empty() || !state.loaded || !state.position_valid || state.paused || state.seeking || state.eof || requested.bvid.empty())return false;
        if(state.source_deadline<=0 || state.source_deadline<=renewedDeadline || state.source_deadline-now>60)return false;
        renewedDeadline=state.source_deadline;trace_event("source-renewal",renewedDeadline);
        open(requested,true,true,{},RetrySource::Extractor);return true;
    }
    void sync(){if(!player.initialized())return;player.drain();state=player.snapshot();if(state.loaded && state.position_valid && !busy){loadStarted=0;hadPlayback=true;lastPosition=state.position;if(applyIntentOnLoad){trace_event("file-loaded",state.height);player.set_pause(desiredPaused||hidden);applyIntentOnLoad=false;state=player.snapshot();}if(state.eof)desiredPaused=true;if(hidden && !state.paused){desiredPaused=true;player.set_pause(true);state=player.snapshot();}}if(loadStarted && (!state.loaded || !state.position_valid) && !busy && !modal && GetTickCount64()-loadStarted>45000){trace_event("load-timeout");loadStarted=0;if(!refreshUsed){refreshUsed=true;open(requested,true,true);}else{player.stop();fail("视频解析或连接超时，请右键手动重试");}return;}if(!state.error.empty() && !busy && !modal && !requested.bvid.empty()){
            if(!refreshUsed){refreshUsed=true;open(requested,true,true,{},hadPlayback?RetrySource::Extractor:RetrySource::PublicApi);return;}fail(state.error+"；请右键选择“重试当前分 P”");KillTimer(window,1);
        }if(renew_source(static_cast<long long>(std::time(nullptr))))return;update_status();}
    void timer(){sync();if(++lastSave>=10){lastSave=0;if(state.loaded && !state.paused)save_safely();}}
    void update_status(){
        std::wstring value;
        if(busy)value=L"正在加载…  再次打开可切换链接";
        else if(!lastError.empty())value=wide(lastError);
        else if(guide && !state.loaded && applyIntentOnLoad)value=L"P"+std::to_wstring(guide->parts[index].page)+L" · 正在解析和加载视频…";
        else if(guide){const auto& p=guide->parts.at(index);value=L"P"+std::to_wstring(p.page)+L"/"+std::to_wstring(guide->parts.size())+L"  ·  "+std::to_wstring(state.height)+L"p  ·  "+speed_text(settings.speed)+L"  ·  "+clock_text(state.loaded && state.position_valid?state.position:lastPosition)+L" / "+clock_text(state.duration>0?state.duration:p.duration)+L"  ·  "+(state.paused?L"已暂停":L"播放中");if(pass)value+=L"  ·  鼠标穿透";}
        else value=message.empty()?(settings.menu_bar_visible?L"打开 B 站链接开始  ·  可拖动上方空白处移动":L"右键打开 B 站链接  ·  可拖动底部状态栏移动"):wide(message);
        if(guide && !busy && lastError.empty() && !message.empty())value+=L"  ·  "+wide(message);
        if(!hotkeyWarning.empty())value=wide(hotkeyWarning);
        if(!storageWarning.empty())value=wide(storageWarning);
        if(status!=value){status=value;if(statusLabel)SetWindowTextW(statusLabel,status.c_str());RECT r;GetClientRect(window,&r);r.top=r.bottom-bottom;InvalidateRect(window,&r,FALSE);}
        if(buttons[4])SetWindowTextW(buttons[4],desiredPaused || !guide?L"播放":L"暂停");
        if(buttons[5])SetWindowTextW(buttons[5],speed_text(settings.speed).c_str());
        std::wstring title=L"原神攻略小窗";if(guide)title+=L" · P"+std::to_wstring(guide->parts[index].page)+L" · "+wide(guide->parts[index].title);SetWindowTextW(window,title.c_str());
    }
    void set_pass(bool enabled){if(enabled && !registered[0]){MessageBoxW(window,L"鼠标穿透解除热键被其他程序占用。请先在设置里更换该热键。",L"需要可用的解除热键",MB_OK|MB_ICONINFORMATION);return;}pass=enabled;LONG_PTR style=GetWindowLongPtrW(window,GWL_EXSTYLE);if(enabled){style|=WS_EX_TRANSPARENT|WS_EX_LAYERED;SetWindowLongPtrW(window,GWL_EXSTYLE,style);SetLayeredWindowAttributes(window,0,255,LWA_ALPHA);}else{style&=~(WS_EX_TRANSPARENT|WS_EX_LAYERED);SetWindowLongPtrW(window,GWL_EXSTYLE,style);}SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED);PostMessageW(window,FRAME_READY,0,0);update_status();}
    void toggle_hidden(){if(!hidden){if(!registered[1] && !trayAdded){MessageBoxW(window,L"显示热键被占用，且托盘图标不可用。请先更换显示热键。",L"无法安全隐藏",MB_OK);return;}desiredPaused=true;player.set_pause(true);sync();save_safely();hidden=true;ShowWindow(window,SW_HIDE);}else{hidden=false;desiredPaused=true;player.set_pause(true);ShowWindow(window,SW_SHOWNOACTIVATE);SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);PostMessageW(window,FRAME_READY,0,0);sync();}update_status();}
    void toggle_menu_bar(){settings.menu_bar_visible=!settings.menu_bar_visible;layout();save_safely();}
    void pause(){if(hidden)return;desiredPaused=!desiredPaused;trace_event("pause-request",desiredPaused?1:0);if(!busy && player.initialized()){player.set_pause(desiredPaused);sync();save_safely();}update_status();}
    void set_speed(double value){player.set_speed(value);settings.speed=value;if(player.initialized())state=player.snapshot();save_safely();}
    void step_speed(int delta){
        auto current=std::find(PLAYBACK_SPEEDS.begin(),PLAYBACK_SPEEDS.end(),settings.speed);
        if(current==PLAYBACK_SPEEDS.end())return;
        int speedIndex=static_cast<int>(current-PLAYBACK_SPEEDS.begin());
        int next=std::clamp(speedIndex+delta,0,static_cast<int>(PLAYBACK_SPEEDS.size())-1);
        if(next!=speedIndex)set_speed(PLAYBACK_SPEEDS[next]);
    }
    HMENU speed_menu(){
        HMENU menu=CreatePopupMenu();UINT selected=SPEED_FIRST+2;
        for(size_t i=0;i<PLAYBACK_SPEEDS.size();++i){
            auto label=speed_text(PLAYBACK_SPEEDS[i]);int id=SPEED_FIRST+static_cast<int>(i);
            if(PLAYBACK_SPEEDS[i]==1.0)label+=L"\t"+hotkey_name(settings.hotkeys[7]);
            AppendMenuW(menu,MF_STRING,id,label.c_str());
            if(PLAYBACK_SPEEDS[i]==settings.speed)selected=id;
        }
        CheckMenuRadioItem(menu,SPEED_FIRST,SPEED_FIRST+static_cast<UINT>(PLAYBACK_SPEEDS.size())-1,selected,MF_BYCOMMAND);
        return menu;
    }
    void show_speed_menu(){
        RECT button;if(!GetWindowRect(buttons[5],&button))return;
        HWND previous=GetForegroundWindow();HMENU menu=speed_menu();SetForegroundWindow(window);
        int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,button.left,button.bottom,0,window,nullptr);
        DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);
        if(previous && previous!=window && IsWindow(previous))SetForegroundWindow(previous);
        if(id)command(id);
    }
    void next(int delta){if(!guide || busy)return;auto target=static_cast<long long>(index)+delta;if(target<0 || target>=static_cast<long long>(guide->parts.size()))return;VideoRef ref{guide->bvid,guide->parts[static_cast<size_t>(target)].page,false,""};open(ref,true);}
    void focus_dialog(bool begin){LONG_PTR style=GetWindowLongPtrW(window,GWL_EXSTYLE);SetWindowLongPtrW(window,GWL_EXSTYLE,begin?style&~WS_EX_NOACTIVATE:style|WS_EX_NOACTIVATE);}
    void command(int id){
        if(modal || closing)return;
        if(id>=SPEED_FIRST && id<SPEED_FIRST+static_cast<int>(PLAYBACK_SPEEDS.size())){set_speed(PLAYBACK_SPEEDS[id-SPEED_FIRST]);return;}
        if(id==SPEED){show_speed_menu();return;}
        if(id==MENU_BAR){toggle_menu_bar();return;}
        if(id==PAUSE){pause();return;}if(id==HIDE){toggle_hidden();return;}if(id==PASS_THROUGH){set_pass(!pass);return;}if(id==PREVIOUS){next(-1);return;}if(id==NEXT){next(1);return;}if(id==QUIT){PostMessageW(window,WM_CLOSE,0,0);return;}
        if(id==RETRY){if(!requested.bvid.empty() || requested.short_url)open(requested,true);return;}
        if(id==FROM_START){if(guide && !busy)open(requested,true,false,0.0);return;}
        HWND previous=GetForegroundWindow();bool wasPass=pass;if(pass)set_pass(false);focus_dialog(true);modal=true;
        std::optional<std::string> url;std::optional<size_t> part;std::optional<Settings> changed;
        if(id==OPEN){std::string initial="https://www.bilibili.com/video/BV1BMgj6MEz4/";if(store.data.contains("last_video") && store.data["last_video"].is_string())initial=store.data["last_video"].get<std::string>();url=open_dialog(window,initial);}
        if(id==PARTS && guide)part=parts_dialog(window,*guide,index);
        if(id==SETTINGS){unregister_keys();changed=settings_dialog(window,settings);if(changed)settings=*changed;register_keys();}
        modal=false;focus_dialog(false);if(wasPass && registered[0])set_pass(true);if(previous && previous!=window && IsWindow(previous))SetForegroundWindow(previous);
        if(changed){save_safely();player.shutdown();state.loaded=false;if(guide)open(requested,true);}
        if(url)open(parse_video_url(*url));
        if(part && guide && *part<guide->parts.size()){VideoRef ref{guide->bvid,guide->parts[*part].page,false,""};open(ref,true);}
        accept_results();sync();
    }
    HMENU context_menu(){
        HMENU menu=CreatePopupMenu();auto add=[&](int id,const wchar_t* label,bool enabled=true,bool checked=false){AppendMenuW(menu,MF_STRING|(enabled?0:MF_GRAYED)|(checked?MF_CHECKED:0),id,label);};
        add(OPEN,L"打开 B 站链接…");add(PARTS,L"选择分 P…",guide!=nullptr);AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        add(PAUSE,desiredPaused?L"继续播放":L"暂停",guide!=nullptr || busy);AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(speed_menu()),L"播放速度");
        add(PREVIOUS,L"上一 P",guide && index>0 && !busy);add(NEXT,L"下一 P",guide && index+1<guide->parts.size() && !busy);add(FROM_START,L"当前 P 从头播放",guide!=nullptr && !busy);add(RETRY,L"重试当前分 P",!requested.bvid.empty() || requested.short_url);AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        auto menuLabel=L"显示菜单栏\t"+hotkey_name(settings.hotkeys[8]);add(MENU_BAR,menuLabel.c_str(),true,settings.menu_bar_visible);
        add(PASS_THROUGH,L"鼠标穿透（快捷键可解除）",true,pass);add(HIDE,hidden?L"显示小窗（保持暂停）":L"隐藏并暂停");add(SETTINGS,L"设置 / 快捷键…");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);add(QUIT,L"退出");return menu;
    }
    void menu(POINT point){HWND previous=GetForegroundWindow();HMENU menu=context_menu();SetForegroundWindow(window);int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);if(previous && previous!=window && IsWindow(previous))SetForegroundWindow(previous);if(id)command(id);}
    void paint(){PAINTSTRUCT p;HDC dc=BeginPaint(window,&p);RECT r;GetClientRect(window,&r);HBRUSH bg=CreateSolidBrush(RGB(21,25,31));FillRect(dc,&r,bg);DeleteObject(bg);SetBkMode(dc,TRANSPARENT);SelectObject(dc,font);SetTextColor(dc,RGB(183,194,204));if(top){RECT grip={scale(4),0,scale(18),top};DrawTextW(dc,L"⠿",-1,&grip,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}RECT s=r;s.top=r.bottom-bottom;s.left=scale(10);s.right-=scale(8);DrawTextW(dc,status.c_str(),-1,&s,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);if(!player.initialized() || busy || !lastError.empty()){RECT body={scale(24),top+scale(18),r.right-scale(24),r.bottom-bottom-scale(10)};std::wstring text=busy?L"正在打开攻略…\n\n可以继续操作游戏，加载完成后开始播放。":!lastError.empty()?wide(lastError)+L"\n\n右键 → 重试当前分 P，或打开其他链接。":std::wstring(L"攻略就在游戏旁边\n\n打开 B 站链接，选择你的路线\n暂停、倒退、继续都可以用快捷键\n\n")+(settings.menu_bar_visible?L"右键打开菜单 · 拖动上方空白处移动":L"右键显示菜单栏 · 拖动底部状态栏移动");DrawTextW(dc,text.c_str(),-1,&body,DT_CENTER|DT_WORDBREAK);}EndPaint(window,&p);}
    void draw_button(DRAWITEMSTRUCT* item){HBRUSH b=CreateSolidBrush((item->itemState&ODS_SELECTED)?RGB(49,80,80):RGB(35,43,52));FillRect(item->hDC,&item->rcItem,b);DeleteObject(b);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,RGB(224,233,239));SelectObject(item->hDC,font);wchar_t label[64];GetWindowTextW(item->hwndItem,label,64);DrawTextW(item->hDC,label,-1,&item->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
    void close(){if(closing)return;trace_event("window-close");if(player.initialized())state=player.snapshot();save_safely();closing=true;cancel_jobs();unregister_keys();KillTimer(window,1);player.shutdown();if(trayAdded)Shell_NotifyIconW(NIM_DELETE,&tray);DestroyWindow(window);}
};
LRESULT CALLBACK window_proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){App* app=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(msg==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));app->window=hwnd;}if(!app)return DefWindowProcW(hwnd,msg,w,l);try{
    if(msg==TASKBAR_CREATED){app->add_tray();return 0;}
    switch(msg){
    case WM_CREATE:app->init();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:app->paint();return 0;
    case WM_DRAWITEM:app->draw_button(reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;
    case WM_CTLCOLORSTATIC:{HDC dc=reinterpret_cast<HDC>(w);SetTextColor(dc,RGB(183,194,204));SetBkColor(dc,RGB(21,25,31));SetDCBrushColor(dc,RGB(21,25,31));return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));}
    case WM_SIZE:app->layout();return 0;
    case WM_NCHITTEST:{
        auto hit=DefWindowProcW(hwnd,msg,w,l);
        if(hit==HTCLIENT && !app->settings.menu_bar_visible){RECT client;GetClientRect(hwnd,&client);POINT p={GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(hwnd,&p);if(p.y>=client.bottom-app->bottom && p.y<client.bottom)return HTCAPTION;}
        return hit;
    }
    case WM_GETMINMAXINFO:{
        auto p=reinterpret_cast<MINMAXINFO*>(l);RECT work=app->active_screen_area(),maximum=app->outer_bounds(work);app->constrain_size(WMSZ_BOTTOMRIGHT,maximum,&work);
        p->ptMaxTrackSize={maximum.right-maximum.left,maximum.bottom-maximum.top};p->ptMinTrackSize={std::min<LONG>(app->scale(330),p->ptMaxTrackSize.x),std::min<LONG>(app->scale(230),p->ptMaxTrackSize.y)};return 0;
    }
    case WM_ENTERSIZEMOVE:app->resizing=false;app->resizeScreen.reset();app->refresh_frame_insets();app->keep_on_screen();app->resizeScreen=app->screen_area();return 0;
    case WM_SIZING:{if(!app->resizeScreen)app->resizeScreen=app->screen_area();app->resizing=true;app->constrain_size(static_cast<UINT>(w),*reinterpret_cast<RECT*>(l),&*app->resizeScreen);return TRUE;}
    case WM_EXITSIZEMOVE:app->refresh_frame_insets();app->keep_on_screen();app->resizing=false;app->resizeScreen.reset();app->save_safely();return 0;
    case WM_DPICHANGED:{
        app->dpi=HIWORD(w);app->top=app->settings.menu_bar_visible?app->scale(34):0;app->bottom=app->scale(25);app->set_font();RECT r=*reinterpret_cast<RECT*>(l);
        RECT destination=app->resizing && app->resizeScreen?*app->resizeScreen:app->screen_area(&r);
        if(app->resizing && app->resizeScreen)app->constrain_size(WMSZ_BOTTOMRIGHT,r,&*app->resizeScreen);
        else if(!app->resizeScreen)app->fit_rect(r,destination);
        // SetWindowPos can synchronously query min/max before the new monitor is reflected by the HWND.
        auto previous=app->dpiScreen;app->dpiScreen=destination;
        SetWindowPos(hwnd,HWND_TOPMOST,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE);app->dpiScreen=previous;app->layout();PostMessageW(hwnd,FRAME_READY,0,0);return 0;
    }
    case WM_DISPLAYCHANGE:app->resizing=false;app->resizeScreen.reset();app->keep_on_screen();PostMessageW(hwnd,FRAME_READY,0,0);return 0;
    case WM_SETTINGCHANGE:if(w==SPI_SETWORKAREA){if(app->resizeScreen)app->resizeScreen=app->screen_area();app->keep_on_screen();return 0;}break;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:if(GET_Y_LPARAM(l)<app->top){ReleaseCapture();SendMessageW(hwnd,WM_NCLBUTTONDOWN,HTCAPTION,0);}return 0;
    case WM_CONTEXTMENU:{POINT p={GET_X_LPARAM(l),GET_Y_LPARAM(l)};if(p.x==-1)GetCursorPos(&p);app->menu(p);return 0;}
    case WM_COMMAND:app->command(LOWORD(w));return 0;
    case WM_KEYDOWN:if(GetKeyState(VK_CONTROL)<0){if(w=='O'){app->command(OPEN);return 0;}if(w=='P'){app->command(PARTS);return 0;}if(w==VK_OEM_COMMA){app->command(SETTINGS);return 0;}}break;
    case WM_HOTKEY:if(!app->modal){switch(w){case 500:app->set_pass(!app->pass);break;case 501:app->toggle_hidden();break;case 502:app->pause();break;case 503:if(!app->busy){app->player.seek(-5);app->sync();}break;case 504:if(!app->busy){app->player.seek(5);app->sync();}break;case 505:app->step_speed(-1);break;case 506:app->step_speed(1);break;case 507:app->set_speed(1.0);break;case 508:app->command(MENU_BAR);break;}}return 0;
    case JOB_DONE:app->accept_results();return 0;
    case SHOW_EXISTING:app->show_existing();return 0;
    case FRAME_READY:app->refresh_frame_insets(!app->resizeScreen);if(!app->resizeScreen)app->keep_on_screen();return 0;
    // Keep the previous margins until refresh can reconcile the old docking against the new frame.
    case WM_DWMCOMPOSITIONCHANGED:case WM_THEMECHANGED:PostMessageW(hwnd,FRAME_READY,0,0);return 0;
    case MEDIA_EVENT:app->sync();return 0;
    case WM_TIMER:app->timer();return 0;
    case TRAY_EVENT:if(l==WM_RBUTTONUP){POINT p;GetCursorPos(&p);app->menu(p);}else if(l==WM_LBUTTONDBLCLK){if(app->hidden)app->toggle_hidden();if(app->pass)app->set_pass(false);}return 0;
    case WM_CLOSE:app->close();return 0;
    case WM_DESTROY:trace_event("window-destroy");PostQuitMessage(0);return 0;
    }
}catch(const std::exception& e){app->fail(e.what());}return DefWindowProcW(hwnd,msg,w,l);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    trace_event("application-start");
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    try {
        auto identity=std::to_wstring(std::hash<std::wstring>{}(executable_dir()));std::wstring className=L"GenshinVideoOverlay-"+identity;
        std::wstring name=L"Local\\GenshinVideo-"+identity;HANDLE mutex=CreateMutexW(nullptr,FALSE,name.c_str());
        if(GetLastError()==ERROR_ALREADY_EXISTS){HWND existing=FindWindowW(className.c_str(),nullptr);if(existing)PostMessageW(existing,SHOW_EXISTING,0,0);else MessageBoxW(nullptr,L"小窗正在启动，请稍后再次打开。",L"原神攻略小窗",MB_OK);if(mutex)CloseHandle(mutex);return 0;}
        App app;WNDCLASSEXW wc={sizeof(wc)};wc.lpfnWndProc=window_proc;wc.hInstance=instance;wc.lpszClassName=className.c_str();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassExW(&wc);
        RECT work={0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);UINT systemDpi=GetDpiForSystem();int minWidth=MulDiv(330,systemDpi,96),minHeight=MulDiv(230,systemDpi,96);int x=work.left+16,y=work.bottom-MulDiv(360,systemDpi,96),w=MulDiv(496,systemDpi,96),h=MulDiv(346,systemDpi,96);
        try {auto r=app.store.data.value("window",json::object());x=std::clamp(r.value("x",x),-100000,100000);y=std::clamp(r.value("y",y),-100000,100000);w=std::clamp(r.value("width",w),minWidth,3840);h=std::clamp(r.value("height",h),minHeight,2160);}catch(...){}
        RECT saved={x,y,x+w,y+h};HMONITOR mon=MonitorFromRect(&saved,MONITOR_DEFAULTTONULL);if(!mon){x=work.left+16;y=std::max(work.top,work.bottom-h-16);}
        HWND hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_APPWINDOW,wc.lpszClassName,L"原神攻略小窗",WS_POPUP|WS_THICKFRAME|WS_CLIPCHILDREN,x,y,w,h,nullptr,nullptr,instance,&app);
        if(!hwnd)throw std::runtime_error("无法创建悬浮窗口");app.keep_on_screen();ShowWindow(hwnd,SW_SHOWNOACTIVATE);SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);UpdateWindow(hwnd);PostMessageW(hwnd,FRAME_READY,0,0);
        int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);std::string initial;if(argc>1)initial=utf8(argv[1]);if(argv)LocalFree(argv);
        if(!initial.empty()){try{app.open(parse_video_url(initial));}catch(const std::exception& e){app.fail(e.what());}}
        MSG msg;int received;while((received=GetMessageW(&msg,nullptr,0,0))>0){if(IsDialogMessageW(hwnd,&msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}trace_event("message-loop-exit",received);if(mutex)CloseHandle(mutex);return 0;
    }catch(const std::exception& e){MessageBoxW(nullptr,wide(e.what()).c_str(),L"原神攻略小窗",MB_OK|MB_ICONERROR);return 1;}
}
