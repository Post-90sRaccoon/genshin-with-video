// Exercise the actual controller, without sending input to external windows.
#include "../src/main.cpp"
#include <iostream>
#include <fstream>
#include <cmath>
#include <limits>
#include <set>
#include <dwmapi.h>
#include "../src/resource.h"
static void expect(bool value,const char* name){if(!value)throw std::runtime_error(name);}
static void visible_edge_docking_regressions(){
    HWND ui=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"",WS_POPUP|WS_THICKFRAME,-10000,-10000,500,340,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    expect(ui!=nullptr,"visible edge fixture");App app;app.window=ui;app.settings=Settings{};app.dpi=GetDpiForWindow(ui);app.top=app.scale(34);app.bottom=app.scale(25);
    ShowWindow(ui,SW_SHOWNOACTIVATE);UpdateWindow(ui);DwmFlush();
    RECT outer,visible;GetWindowRect(ui,&outer);expect(SUCCEEDED(DwmGetWindowAttribute(ui,DWMWA_EXTENDED_FRAME_BOUNDS,&visible,sizeof(visible))),"DWM visible frame is measurable");
    RECT margin={visible.left-outer.left,visible.top-outer.top,outer.right-visible.right,outer.bottom-visible.bottom};app.refresh_frame_insets(false);
    ShowWindow(ui,SW_HIDE);
    MONITORINFO info={sizeof(info)};GetMonitorInfoW(MonitorFromWindow(ui,MONITOR_DEFAULTTONEAREST),&info);const RECT screen=info.rcMonitor;
    LONG width=outer.right-outer.left,height=outer.bottom-outer.top;
    RECT bottom={screen.left+20,screen.bottom+margin.bottom-height,screen.left+20+width,screen.bottom+margin.bottom};
    RECT requested=bottom;app.fit_rect(bottom,screen);
    expect(bottom.bottom-margin.bottom==screen.bottom && EqualRect(&bottom,&requested),"visible bottom edge can touch the full monitor boundary without a transparent gap");
    RECT selected=app.screen_area();expect(EqualRect(&selected,&screen)!=FALSE,"overlay bounds use the full monitor rather than the taskbar work area");
    // Compose away from the actual desktop, but verify real DWM pixels at four virtual screen edges.
    RECT virtualScreen={-10500,-10500,-9000,-9000};app.resizeScreen=virtualScreen;app.resizing=true;
    for(int edge=0;edge<4;++edge){
        RECT target={virtualScreen.left+100,virtualScreen.top+100,virtualScreen.left+100+width,virtualScreen.top+100+height};
        if(edge==0)OffsetRect(&target,virtualScreen.left-margin.left-target.left,0);
        if(edge==1)OffsetRect(&target,virtualScreen.right+margin.right-target.right,0);
        if(edge==2)OffsetRect(&target,0,virtualScreen.top-margin.top-target.top);
        if(edge==3)OffsetRect(&target,0,virtualScreen.bottom+margin.bottom-target.bottom);
        RECT wanted=target;app.fit_rect(target,virtualScreen);expect(EqualRect(&wanted,&target)!=FALSE,"four-edge docking preserves the requested outer rectangle");
        SetWindowPos(ui,nullptr,target.left,target.top,width,height,SWP_NOZORDER|SWP_NOACTIVATE);ShowWindow(ui,SW_SHOWNOACTIVATE);UpdateWindow(ui);DwmFlush();
        expect(SUCCEEDED(DwmGetWindowAttribute(ui,DWMWA_EXTENDED_FRAME_BOUNDS,&visible,sizeof(visible))),"composed docking frame");
        expect((edge!=0 || visible.left==virtualScreen.left) && (edge!=1 || visible.right==virtualScreen.right) && (edge!=2 || visible.top==virtualScreen.top) && (edge!=3 || visible.bottom==virtualScreen.bottom),"actual visible pixels touch each screen edge without a gap");
    }
    if(margin.bottom>1){
        app.resizeScreen.reset();app.resizing=false;app.dpiScreen=virtualScreen;
        // Simulate a previous theme with no invisible border, then measure the current real frame.
        app.frameInsets={};app.frameDpi=app.dpi;app.frameMeasured=true;
        SetWindowPos(ui,nullptr,virtualScreen.left+100,virtualScreen.bottom-height,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowLongPtrW(ui,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&app));
        window_proc(ui,WM_THEMECHANGED,0,0);window_proc(ui,FRAME_READY,0,0);DwmFlush();
        expect(SUCCEEDED(DwmGetWindowAttribute(ui,DWMWA_EXTENDED_FRAME_BOUNDS,&visible,sizeof(visible))) && visible.bottom==virtualScreen.bottom,"theme frame growth preserves the previously docked visible bottom edge");
        app.dpiScreen.reset();
    }
    ShowWindow(ui,SW_HIDE);app.resizing=false;app.resizeScreen.reset();
    SetWindowPos(ui,nullptr,bottom.left,bottom.top,width,height,SWP_NOZORDER|SWP_NOACTIVATE);app.keep_on_screen();RECT kept;GetWindowRect(ui,&kept);
    expect(EqualRect(&bottom,&kept)!=FALSE,"releasing a bottom-docked window does not push it up");
    app.store=Store(executable_dir()+L"\\visible-edge-test-data");SetWindowLongPtrW(ui,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&app));
    window_proc(ui,WM_ENTERSIZEMOVE,0,0);window_proc(ui,WM_EXITSIZEMOVE,0,0);window_proc(ui,WM_SETTINGCHANGE,SPI_SETWORKAREA,0);GetWindowRect(ui,&kept);
    expect(EqualRect(&bottom,&kept)!=FALSE,"move completion and taskbar changes preserve visible bottom docking");
    Store saved(executable_dir()+L"\\visible-edge-test-data");auto geometry=saved.data["window"];
    RECT restored={geometry["x"].get<LONG>(),geometry["y"].get<LONG>(),geometry["x"].get<LONG>()+geometry["width"].get<LONG>(),geometry["y"].get<LONG>()+geometry["height"].get<LONG>()};
    app.fit_rect(restored,screen);expect(EqualRect(&bottom,&restored)!=FALSE && restored.bottom-margin.bottom==screen.bottom,"saved bottom docking restores flush to the physical screen");
    SetWindowLongPtrW(ui,GWLP_USERDATA,0);DestroyWindow(ui);
}
static void screen_bounds_regressions(){
    HWND ui=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Screen bounds fixture",WS_POPUP|WS_THICKFRAME,0,0,500,340,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    expect(ui!=nullptr,"screen bounds fixture window");
    App sizing;sizing.window=ui;sizing.settings=Settings{};sizing.dpi=GetDpiForWindow(ui);sizing.top=sizing.scale(34);sizing.bottom=sizing.scale(25);
    sizing.store=Store(executable_dir()+L"\\screen-bounds-test-data");
    SetWindowLongPtrW(ui,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&sizing));
    auto nativeProcedure=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(ui,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(window_proc)));
    expect(nativeProcedure!=nullptr,"native fixture routes nested sizing messages through the app procedure");
    MONITORINFO info={sizeof(info)};expect(GetMonitorInfoW(MonitorFromWindow(ui,MONITOR_DEFAULTTONEAREST),&info)!=FALSE,"fixture monitor work area");
    const RECT work=info.rcMonitor;
    SetWindowPos(ui,nullptr,work.left+20,work.top+20,500,340,SWP_NOZORDER|SWP_NOACTIVATE);
    window_proc(ui,WM_ENTERSIZEMOVE,0,0);
    RECT proposed;GetWindowRect(ui,&proposed);proposed.right=work.right+2000;proposed.bottom=work.bottom+2000;
    window_proc(ui,WM_SIZING,WMSZ_BOTTOMRIGHT,reinterpret_cast<LPARAM>(&proposed));
    RECT projected=sizing.visible_rect(proposed);expect(projected.left>=work.left && projected.top>=work.top && projected.right<=work.right && projected.bottom<=work.bottom,"resizing past the monitor stops at visible screen bounds");
    SetWindowPos(ui,nullptr,proposed.left,proposed.top,proposed.right-proposed.left,proposed.bottom-proposed.top,SWP_NOZORDER|SWP_NOACTIVATE);
    window_proc(ui,WM_EXITSIZEMOVE,0,0);
    RECT released;GetWindowRect(ui,&released);
    expect(EqualRect(&proposed,&released)!=FALSE,"releasing a bounded resize does not jump the window");
    Store saved(executable_dir()+L"\\screen-bounds-test-data");auto geometry=saved.data["window"];
    expect(geometry["x"]==released.left && geometry["y"]==released.top && geometry["width"]==released.right-released.left && geometry["height"]==released.bottom-released.top,"resize completion saves the final visible geometry");
    expect(!sizing.resizeScreen && !sizing.resizing,"releasing resize clears the captured monitor");
    sizing.resizeScreen=RECT{0,0,240,180};sizing.resizing=false;
    RECT dpiSuggestion=sizing.outer_bounds(work);sizing.constrain_size(WMSZ_BOTTOMRIGHT,dpiSuggestion,&work);
    MINMAXINFO moveLimits={};SendMessageW(ui,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&moveLimits));
    expect(moveLimits.ptMaxTrackSize.x==dpiSuggestion.right-dpiSuggestion.left && moveLimits.ptMaxTrackSize.y==dpiSuggestion.bottom-dpiSuggestion.top,"ordinary moves ignore the departure monitor's tracking limits");
    struct DpiProbe {App* app;RECT work;int queries=0;bool destinationUsed=true;};DpiProbe probe{&sizing,work};
    auto probeProcedure=[](HWND hwnd,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data)->LRESULT{
        auto probe=reinterpret_cast<DpiProbe*>(data);
        if(msg==WM_GETMINMAXINFO){++probe->queries;probe->destinationUsed=probe->destinationUsed && probe->app->dpiScreen && EqualRect(&*probe->app->dpiScreen,&probe->work);}
        return DefSubclassProc(hwnd,msg,w,l);
    };
    expect(SetWindowSubclass(ui,probeProcedure,1,reinterpret_cast<DWORD_PTR>(&probe))!=FALSE,"native DPI min/max probe");
    SendMessageW(ui,WM_DPICHANGED,MAKELONG(sizing.dpi,sizing.dpi),reinterpret_cast<LPARAM>(&dpiSuggestion));GetWindowRect(ui,&released);
    RemoveWindowSubclass(ui,probeProcedure,1);
    expect(EqualRect(&dpiSuggestion,&released)!=FALSE,"nested SetWindowPos validation preserves a valid move/DPI size");
    expect(probe.queries>0 && probe.destinationUsed && !sizing.dpiScreen,"native reentrant min/max validation uses the transient destination work area and clears it afterward");
    sizing.resizeScreen.reset();
    for(UINT edge:{WMSZ_LEFT,WMSZ_RIGHT,WMSZ_TOP,WMSZ_BOTTOM,WMSZ_TOPLEFT,WMSZ_TOPRIGHT,WMSZ_BOTTOMLEFT,WMSZ_BOTTOMRIGHT}){
        SetWindowPos(ui,nullptr,work.left+20,work.top+20,500,340,SWP_NOZORDER|SWP_NOACTIVATE);
        window_proc(ui,WM_ENTERSIZEMOVE,0,0);RECT request;GetWindowRect(ui,&request);
        if(edge==WMSZ_LEFT || edge==WMSZ_TOPLEFT || edge==WMSZ_BOTTOMLEFT)request.left=work.left-2000;else if(edge!=WMSZ_TOP && edge!=WMSZ_BOTTOM)request.right=work.right+2000;
        if(edge==WMSZ_TOP || edge==WMSZ_TOPLEFT || edge==WMSZ_TOPRIGHT)request.top=work.top-2000;else if(edge==WMSZ_BOTTOM || edge==WMSZ_BOTTOMLEFT || edge==WMSZ_BOTTOMRIGHT)request.bottom=work.bottom+2000;
        window_proc(ui,WM_SIZING,edge,reinterpret_cast<LPARAM>(&request));
        projected=sizing.visible_rect(request);expect(projected.left>=work.left && projected.top>=work.top && projected.right<=work.right && projected.bottom<=work.bottom,"every native resize edge respects the visible monitor boundary");
        SetWindowPos(ui,nullptr,request.left,request.top,request.right-request.left,request.bottom-request.top,SWP_NOZORDER|SWP_NOACTIVATE);
        window_proc(ui,WM_EXITSIZEMOVE,0,0);GetWindowRect(ui,&released);
        expect(EqualRect(&request,&released)!=FALSE,"every constrained edge stays stationary on release");
    }
    // Recover oversized legacy geometry without independently clipping width and height.
    SetWindowPos(ui,nullptr,work.left-100,work.top-100,(work.right-work.left)*2,(work.bottom-work.top)*2,SWP_NOZORDER|SWP_NOACTIVATE);
    sizing.dpi=GetDpiForWindow(ui);sizing.top=sizing.scale(34);sizing.bottom=sizing.scale(25);RECT recovery=sizing.screen_area();
    sizing.keep_on_screen();GetWindowRect(ui,&released);
    projected=sizing.visible_rect(released);expect(projected.left>=recovery.left && projected.top>=recovery.top && projected.right<=recovery.right && projected.bottom<=recovery.bottom,"oversized old windows recover within visible screen bounds");
    RECT client;GetClientRect(ui,&client);
    expect(std::abs(client.right*9-(client.bottom-sizing.top-sizing.bottom)*16)<=8,"oversized recovery preserves the actual video aspect");
    sizing.keep_on_screen();RECT stable;GetWindowRect(ui,&stable);expect(EqualRect(&released,&stable)!=FALSE,"recovery is idempotent");
    sizing.resizeScreen=RECT{-5000,-5000,5000,5000};sizing.resizing=true;window_proc(ui,WM_SETTINGCHANGE,SPI_SETWORKAREA,0);
    RECT changed=sizing.screen_area();expect(sizing.resizeScreen && EqualRect(&*sizing.resizeScreen,&changed)!=FALSE,"work-area changes replace stale taskbar bounds");
    window_proc(ui,WM_DISPLAYCHANGE,0,0);expect(!sizing.resizeScreen && !sizing.resizing,"display changes discard disconnected monitor bounds");
    SetWindowLongPtrW(ui,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(nativeProcedure));SetWindowLongPtrW(ui,GWLP_USERDATA,0);DestroyWindow(ui);
    std::cout<<"PASS native visible screen bounds and stable saved position\n";
}
static void bounded_resize_geometry_regressions(){
    const DWORD style=WS_POPUP|WS_THICKFRAME;
    HWND ui=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Bounded geometry fixture",style,0,0,500,340,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    expect(ui!=nullptr,"bounded geometry fixture");App sizing;sizing.window=ui;sizing.settings=Settings{};
    SetWindowLongPtrW(ui,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&sizing));
    auto inside=[&](const RECT& rect,const RECT& work){RECT visible=sizing.visible_rect(rect);return visible.right>visible.left && visible.bottom>visible.top && visible.left>=work.left && visible.top>=work.top && visible.right<=work.right && visible.bottom<=work.bottom;};
    int cases=0;
    for(UINT dpi:{96u,120u,144u,168u,192u})for(bool menuVisible:{true,false}){
        sizing.dpi=dpi;sizing.settings.menu_bar_visible=menuVisible;sizing.top=menuVisible?sizing.scale(34):0;sizing.bottom=sizing.scale(25);
        RECT frame={};expect(AdjustWindowRectExForDpi(&frame,style,FALSE,WS_EX_NOACTIVATE,dpi)!=FALSE,"bounded fixture frame");
        LONG frameWidth=frame.right-frame.left,overhead=frame.bottom-frame.top+sizing.top+sizing.bottom;
        for(const RECT& work:std::vector<RECT>{{0,40,1920,1040},{-1920,0,0,1040},{40,-1280,1440,-40},{0,0,240,180},{0,0,24,60}}){
            sizing.resizeScreen=work;sizing.resizing=false;
            RECT base={work.left+20,work.top+20,work.left+20+sizing.scale(480),work.top+20+sizing.scale(270)+overhead};
            sizing.constrain_size(WMSZ_BOTTOMRIGHT,base,&work);expect(inside(base,work),"initial bounded geometry fits even smaller work areas");
            sizing.resizing=true;MINMAXINFO limits={};window_proc(ui,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
            RECT allowed=sizing.outer_bounds(work);expect(limits.ptMaxTrackSize.x<=allowed.right-allowed.left && limits.ptMaxTrackSize.y<=allowed.bottom-allowed.top && limits.ptMinTrackSize.x>0 && limits.ptMinTrackSize.y>0 && limits.ptMinTrackSize.x<=limits.ptMaxTrackSize.x && limits.ptMinTrackSize.y<=limits.ptMaxTrackSize.y,"tracking minima and maxima include only invisible frame overflow");
            for(UINT edge:{WMSZ_LEFT,WMSZ_RIGHT,WMSZ_TOP,WMSZ_BOTTOM,WMSZ_TOPLEFT,WMSZ_TOPRIGHT,WMSZ_BOTTOMLEFT,WMSZ_BOTTOMRIGHT}){
                bool left=edge==WMSZ_LEFT || edge==WMSZ_TOPLEFT || edge==WMSZ_BOTTOMLEFT,upper=edge==WMSZ_TOP || edge==WMSZ_TOPLEFT || edge==WMSZ_TOPRIGHT;
                RECT request=base;
                if(edge!=WMSZ_TOP && edge!=WMSZ_BOTTOM){if(left)request.left=work.left-5000;else request.right=work.right+5000;}
                if(upper)request.top=work.top-5000;else if(edge==WMSZ_BOTTOM || edge==WMSZ_BOTTOMLEFT || edge==WMSZ_BOTTOMRIGHT)request.bottom=work.bottom+5000;
                RECT extreme=request;window_proc(ui,WM_SIZING,edge,reinterpret_cast<LPARAM>(&request));++cases;
                expect(inside(request,work),"outward resizing remains inside the original monitor including negative origins");
                expect((left?request.right==base.right:request.left==base.left) && (upper?request.bottom==base.bottom:request.top==base.top),"hitting the monitor boundary keeps the stationary edges fixed");
                if(work.right-work.left>frameWidth && work.bottom-work.top>overhead)expect(std::abs((request.right-request.left-frameWidth)*9-(request.bottom-request.top-overhead)*16)<=8,"screen limits preserve 16:9 at every DPI and menu state");
                for(int repeat=0;repeat<4;++repeat){RECT next=extreme;window_proc(ui,WM_SIZING,edge,reinterpret_cast<LPARAM>(&next));expect(EqualRect(&next,&request)!=FALSE,"continued outward dragging does not drift or change monitors");}
                RECT back=base;window_proc(ui,WM_SIZING,edge,reinterpret_cast<LPARAM>(&back));
                expect(inside(back,work) && back.right-back.left<=base.right-base.left+1 && back.bottom-back.top<=base.bottom-base.top+1,"dragging inward after the boundary can shrink again");
                expect(sizing.resizeScreen && EqualRect(&*sizing.resizeScreen,&work)!=FALSE,"oversized candidates cannot select an adjacent monitor");
            }
            RECT old={work.left-100,work.top-100,work.right+3000,work.bottom+3000};sizing.fit_rect(old,work);expect(inside(old,work),"restored oversized geometry is fitted to the same work area");
        }
    }
    sizing.resizeScreen=RECT{-1920,0,0,1040};sizing.resizing=true;
    RECT suggested={100,100,5000,4000};window_proc(ui,WM_DPICHANGED,MAKELONG(144,144),reinterpret_cast<LPARAM>(&suggested));RECT applied;GetWindowRect(ui,&applied);
    expect(inside(applied,*sizing.resizeScreen),"DPI changes during resize retain the captured monitor boundary");
    sizing.resizing=false;sizing.dpiScreen=RECT{0,0,240,180};MINMAXINFO destinationLimits={};window_proc(ui,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&destinationLimits));
    RECT destination=sizing.outer_bounds(*sizing.dpiScreen);expect(destinationLimits.ptMaxTrackSize.x<=destination.right-destination.left && destinationLimits.ptMaxTrackSize.y<=destination.bottom-destination.top,"DPI validation uses destination bounds independently of the HWND's old monitor");sizing.dpiScreen.reset();
    SetWindowLongPtrW(ui,GWLP_USERDATA,0);DestroyWindow(ui);
    std::cout<<"PASS "<<cases<<" bounded edge/DPI/menu/work-area cases\n";
}
static void resize_regressions(){
    const DWORD style=WS_POPUP|WS_THICKFRAME,extended=WS_EX_NOACTIVATE|WS_EX_APPWINDOW;
    HWND ui=CreateWindowExW(extended,L"STATIC",L"Resize fixture",style,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    expect(ui!=nullptr,"resize fixture window");
    App sizing;sizing.window=ui;sizing.settings=Settings{};
    // These geometry cases describe a virtual desktop, independent of the test machine's monitors.
    sizing.resizeScreen=RECT{-5000,-5000,5000,5000};
    SetWindowLongPtrW(ui,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&sizing));
    for(UINT dpi:{96u,144u,192u})for(bool menuVisible:{true,false}){
        sizing.dpi=dpi;sizing.settings.menu_bar_visible=menuVisible;sizing.top=menuVisible?sizing.scale(34):0;sizing.bottom=sizing.scale(25);
        RECT base={0,0,sizing.scale(640),sizing.scale(360)+sizing.top+sizing.bottom};
        expect(AdjustWindowRectExForDpi(&base,style,FALSE,extended,dpi)!=FALSE,"DPI-aware fixture frame");
        const LONG frameWidth=base.right-base.left-sizing.scale(640),overhead=base.bottom-base.top-sizing.scale(360);
        OffsetRect(&base,-1600-base.left,200-base.top);
        for(UINT edge:{WMSZ_TOP,WMSZ_BOTTOM,WMSZ_LEFT,WMSZ_RIGHT,WMSZ_TOPLEFT,WMSZ_TOPRIGHT,WMSZ_BOTTOMLEFT,WMSZ_BOTTOMRIGHT}){
            const bool vertical=edge==WMSZ_TOP || edge==WMSZ_BOTTOM;
            const bool left=edge==WMSZ_LEFT || edge==WMSZ_TOPLEFT || edge==WMSZ_BOTTOMLEFT;
            const bool upper=edge==WMSZ_TOP || edge==WMSZ_TOPLEFT || edge==WMSZ_TOPRIGHT;
            const bool lower=edge==WMSZ_BOTTOM || edge==WMSZ_BOTTOMLEFT || edge==WMSZ_BOTTOMRIGHT;
            for(int direction:{1,-1}){
                RECT proposed=base;LONG dx=sizing.scale(160)*direction,dy=sizing.scale(90)*direction;
                if(!vertical){if(left)proposed.left-=dx;else proposed.right+=dx;}
                if(upper)proposed.top-=dy;else if(lower)proposed.bottom+=dy;
                expect(window_proc(ui,WM_SIZING,edge,reinterpret_cast<LPARAM>(&proposed))==TRUE,"resize message is handled");
                expect(proposed.right-proposed.left-frameWidth==sizing.scale(640+160*direction),"dragging any resize edge changes video width with the requested scale");
                expect(proposed.bottom-proposed.top-overhead==sizing.scale(360+90*direction),"all edges and corners preserve the video aspect including native frame and menu strips");
                expect((left?proposed.right==base.right:proposed.left==base.left) && (upper?proposed.bottom==base.bottom:proposed.top==base.top),"resizing keeps the opposite edges anchored at negative screen coordinates");
            }
            RECT tiny=base;
            if(!vertical){if(left)tiny.left=tiny.right-1;else tiny.right=tiny.left+1;}
            if(upper)tiny.top=tiny.bottom-1;else if(lower)tiny.bottom=tiny.top+1;
            window_proc(ui,WM_SIZING,edge,reinterpret_cast<LPARAM>(&tiny));
            expect(tiny.right-tiny.left>=sizing.scale(330) && tiny.bottom-tiny.top>=sizing.scale(230),"aspect correction respects both existing minimum dimensions");
            LONG videoWidth=tiny.right-tiny.left-frameWidth,videoHeight=tiny.bottom-tiny.top-overhead;
            expect(videoWidth>0 && videoHeight>0 && std::abs(videoWidth*9-videoHeight*16)<=8,"minimum-size video remains 16:9 within half a pixel of rounding");
            expect((left?tiny.right==base.right:tiny.left==base.left) && (upper?tiny.bottom==base.bottom:tiny.top==base.top),"minimum-size clamping preserves the stationary edges");
        }
    }
    sizing.dpi=GetDpiForWindow(ui);sizing.bottom=sizing.scale(25);
    sizing.video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,1,1,ui,nullptr,GetModuleHandleW(nullptr),nullptr);
    for(bool menuVisible:{true,false}){
        sizing.settings.menu_bar_visible=menuVisible;sizing.layout();
        RECT proposed={-1500,200,-860,700};window_proc(ui,WM_SIZING,WMSZ_TOP,reinterpret_cast<LPARAM>(&proposed));
        SetWindowPos(ui,nullptr,proposed.left,proposed.top,proposed.right-proposed.left,proposed.bottom-proposed.top,SWP_NOZORDER|SWP_NOACTIVATE);sizing.layout();
        RECT video;GetClientRect(sizing.video,&video);
        expect(std::abs(video.right*9-video.bottom*16)<=8,"applying a constrained resize produces an actual 16:9 video client area");
    }
    SetWindowLongPtrW(ui,GWLP_USERDATA,0);DestroyWindow(ui);
    std::cout<<"PASS edge/corner resize, minimum bounds and DPI/menu variants\n";
}
static void premature_video_fixture(HWND host){
    auto u32=[](unsigned value){return std::string(reinterpret_cast<const char*>(&value),4);};
    auto u16=[](unsigned short value){return std::string(reinterpret_cast<const char*>(&value),2);};
    auto chunk=[&](const char* tag,const std::string& data){auto result=std::string(tag,4)+u32(static_cast<unsigned>(data.size()))+data;if(data.size()%2)result+='\0';return result;};
    auto list=[&](const char* tag,const std::string& data){return chunk("LIST",std::string(tag,4)+data);};
    std::string avih;for(unsigned value:{100000u,7680u,0u,16u,100u,0u,1u,768u,16u,16u,0u,0u,0u,0u})avih+=u32(value);
    std::string strh="vidsDIB ";strh+=u32(0)+u16(0)+u16(0);for(unsigned value:{0u,1u,10u,0u,100u,768u,0xffffffffu,0u})strh+=u32(value);strh+=u16(0)+u16(0)+u16(16)+u16(16);
    std::string strf=u32(40)+u32(16)+u32(16)+u16(1)+u16(24);for(unsigned value:{0u,768u,0u,0u,0u,0u})strf+=u32(value);
    std::string frames,index;for(unsigned i=0;i<100;++i){index+="00db";index+=u32(16)+u32(static_cast<unsigned>(frames.size())+4)+u32(768);frames+=chunk("00db",std::string(768,static_cast<char>(i*2)));}
    auto header=std::string("AVI ")+list("hdrl",chunk("avih",avih)+list("strl",chunk("strh",strh)+chunk("strf",strf)));
    auto body=header+list("movi",frames)+chunk("idx1",index);auto full=std::string("RIFF")+u32(static_cast<unsigned>(body.size()))+body;
    auto good=executable_dir()+L"\\video-complete.avi",bad=executable_dir()+L"\\video-truncated.avi",audio=executable_dir()+L"\\video-recovery-audio.wav";
    {std::ofstream file(good,std::ios::binary);file.write(full.data(),full.size());}
    {std::ofstream file(bad,std::ios::binary);auto cut=12+header.size()+12+20*(8+768);file.write(full.data(),cut);}
    {std::ofstream file(audio,std::ios::binary);auto data=std::string(160000,0);file<<"RIFF"<<u32(160036)<<"WAVEfmt "<<u32(16)<<u16(1)<<u16(1)<<u32(8000)<<u32(16000)<<u16(2)<<u16(16)<<"data"<<u32(160000);file.write(data.data(),data.size());}
    App app;app.window=host;app.video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,160,90,host,nullptr,GetModuleHandleW(nullptr),nullptr);app.store=Store(executable_dir()+L"\\video-recovery-test-data");app.settings=Settings{};app.settings.speed=1.0;
    app.guide=std::make_shared<Guide>();app.guide->bvid="BV1BMgj6MEz4";app.guide->parts.push_back({1,111,"truncated fixture",10});app.requested={app.guide->bvid,1,false,""};app.desiredPaused=false;
    app.player.initialize(app.video,executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll",[]{},SourceOptions{},[&](const char* event,const json& fields){app.diagnostics->record(event,fields);});app.player.volume(0);
    Media media;media.video=utf8(bad);media.audio=utf8(audio);app.player.load(media,0,false,"truncated video with complete audio");
    auto until=GetTickCount64()+8000;bool observed=false;
    do{MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}app.sync();auto current=app.player.diagnostic_snapshot();auto values=current["values"];
        if(values["time-pos"].is_number() && values["time-pos"].get<double>()>3 && values["demuxer-cache-state"].is_object() && values["demuxer-cache-state"].value("eof",false))observed=true;
        if(app.busy)break;Sleep(20);
    }while(GetTickCount64()<until);
    expect(observed,"native truncated video reaches input EOF while the complete audio keeps moving");
    if(!app.busy)std::cerr<<"Truncated fixture diagnostic: "<<json({{"native",app.player.diagnostic_snapshot()},{"input_valid",app.state.video_input.valid},{"input_end",app.state.video_input.video_end},{"input_remaining",app.state.video_input.video_remaining},{"input_audio",app.state.video_input.audio_position},{"predicate",premature_video_eof(app.state.video_input,app.state.position,app.state.duration)},{"candidate_entry",app.videoCandidateEntry},{"candidate_since",app.videoCandidateSince}}).dump()<<"\n";
    expect(app.busy,"a native video-only premature EOF automatically requests a fresh source");
    expect(app.videoRecoveryAttempts==1,"native premature EOF consumes exactly one automatic recovery attempt");
    for(auto& job:app.jobs)if(job->thread.joinable())job->thread.join();
    expect(app.results.size()==1,"automatic recovery resolves the same cached guide");
    double resume=app.results.front()->resume.seconds;expect(resume>0 && resume<2,"recovery resumes at the last video boundary rather than advanced audio");
    app.pause();app.set_speed(1.75);expect(app.desiredPaused,"a pause request made during recovery is preserved");
    app.results.front()->public_streams=true;app.results.front()->media.video=utf8(good);app.results.front()->media.audio=utf8(audio);app.accept_results();
    until=GetTickCount64()+8000;do{MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}app.sync();if(app.state.loaded && app.state.position_valid)break;Sleep(20);}while(GetTickCount64()<until);
    expect(app.state.loaded && app.state.paused && app.state.speed==1.75 && std::abs(app.state.position-resume)<0.3 && app.guide->parts[app.index].cid==111,"new source retains CID visual progress pause intent and latest speed");
    app.pause();until=GetTickCount64()+8000;do{MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}app.sync();if(app.state.position>5)break;Sleep(20);}while(GetTickCount64()<until);
    expect(app.state.loaded && app.state.position>5 && app.state.video_input.valid && app.state.video_input.video_end-app.state.video_input.video_remaining>4.5 && !app.busy && app.videoRecoveryAttempts==1,"replacement video packets pass the broken boundary without mistaking normal cached EOF for failure");
    std::cout<<"PASS native truncated video recovery and complete replacement\n";
}
static void background_playback_overlap(HWND host){
    auto good=executable_dir()+L"\\video-complete.avi",bad=executable_dir()+L"\\video-background-truncated.avi",audio=executable_dir()+L"\\video-recovery-audio.wav";
    {std::ifstream file(good,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());auto movi=bytes.find("movi");expect(movi!=std::string::npos,"background fixture AVI has a media list");std::ofstream cut(bad,std::ios::binary);cut.write(bytes.data(),movi+4+80*(8+768));}
    auto release=std::make_shared<std::atomic<bool>>(false);unsigned initializations=0;
    App app;app.window=host;app.video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,160,90,host,nullptr,GetModuleHandleW(nullptr),nullptr);app.store=Store(executable_dir()+L"\\background-overlap-test-data");app.settings=Settings{};app.desiredPaused=false;
    app.guide=std::make_shared<Guide>();app.guide->bvid="BV1BMgj6MEz4";app.guide->parts.push_back({1,111,"buffered truncation",20});app.requested={app.guide->bvid,1,false,""};
    app.extractBackground=[&,release](const VideoRef&,const Part&,const ExtractionOptions&,Cancellation& cancel){while(!*release){cancel.check();Sleep(5);}Media result;result.video=utf8(good);result.audio=utf8(audio);result.height=16;result.source="background_ytdl";return result;};
    app.player.initialize(app.video,executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll",[]{},SourceOptions{},[&](const char* event,const json& fields){if(std::string(event)=="mpv-initialized")++initializations;app.diagnostics->record(event,fields);});app.player.volume(0);
    Media original;original.video=utf8(bad);original.audio=utf8(audio);app.player.load(original,0,false,"buffered premature EOF");
    auto until=GetTickCount64()+6000;do{app.sync();if(app.preparationCancel)break;Sleep(20);}while(GetTickCount64()<until);
    expect(app.preparationCancel && !app.busy && app.state.video_input.video_remaining>2,"native buffered EOF prepares media while the current source remains loaded");
    auto entry=app.state.entry_id;double before=app.state.position;until=GetTickCount64()+1000;do{app.sync();Sleep(20);}while(GetTickCount64()<until);
    expect(app.state.entry_id==entry && app.state.position>before+0.65 && app.videoRecoveryAttempts==0,"native playback advances during a deliberately blocked background extraction");
    app.pause();app.set_speed(2.0);double latest=app.state.position;*release=true;for(auto& job:app.jobs)if(job->thread.joinable())job->thread.join();app.accept_results();
    expect(app.prepared && app.state.entry_id==entry && app.desiredPaused,"a completed preparation respects a pause issued during extraction");
    app.pause();expect(app.videoRecoveryAttempts==1 && !app.prepared && app.applyIntentOnLoad,"resuming applies the prepared source with one bounded attempt");app.guide->parts[0].duration=10;
    until=GetTickCount64()+5000;do{app.sync();if(app.state.loaded && app.state.position_valid && !app.applyIntentOnLoad)break;Sleep(20);}while(GetTickCount64()<until);
    expect(app.state.loaded && !app.state.paused && app.state.speed==2.0 && std::abs(app.state.position-latest)<0.5 && app.state.entry_id!=entry && initializations==1,"handoff uses the latest playhead and speed on the same Player");
    until=GetTickCount64()+6000;do{app.sync();if(app.state.position>8.6)break;Sleep(20);}while(GetTickCount64()<until);
    expect(app.state.position>8.6 && app.state.video_input.valid && app.state.video_input.video_end-app.state.video_input.video_remaining>8.4 && app.videoRecoveryAttempts==1,"replacement packets cross the original eight-second boundary without another recovery");
    std::cout<<"PASS native buffered extraction overlap and same-Player handoff\n";
}
static void background_preparation_failure(HWND host){
    App app;app.window=host;app.video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,160,90,host,nullptr,GetModuleHandleW(nullptr),nullptr);app.store=Store(executable_dir()+L"\\background-failure-test-data");app.settings=Settings{};app.desiredPaused=false;
    app.guide=std::make_shared<Guide>();app.guide->bvid="BV1BMgj6MEz4";app.guide->parts.push_back({1,111,"pending extraction failure",10});app.requested={app.guide->bvid,1,false,""};
    app.player.initialize(app.video,executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll",[]{},SourceOptions{});app.player.volume(0);Media broken;broken.video=utf8(executable_dir()+L"\\video-truncated.avi");broken.audio=utf8(executable_dir()+L"\\video-recovery-audio.wav");app.player.load(broken,0,false,"hold and fallback");
    // The process runner has its own cancellation tests. Inject a pending token
    // here to isolate the native exhausted-cache hold/failure protocol.
    app.preparationCancel=std::make_shared<Cancellation>();auto until=GetTickCount64()+7500;
    do{app.sync();if(app.preparationHeld)break;Sleep(20);}while(GetTickCount64()<until);
    expect(app.preparationHeld && app.state.paused && !app.desiredPaused && !app.busy && app.videoRecoveryAttempts==0,"drained video holds audio while an existing preparation completes");
    auto r=std::make_unique<Result>();r->generation=app.generation;r->preparation=true;r->preparation_epoch=app.preparationEpoch;r->entry=app.state.entry_id;r->cid=111;r->quality=app.settings.quality;r->cookies=app.settings.cookies;r->ref=app.requested;r->video_end=app.state.video_input.video_end;r->prepared_at=GetTickCount64();r->error="controlled extraction failure";app.results.push_back(std::move(r));
    app.modal=true;app.accept_results();expect(app.results.size()==1,"a modal report may defer the result notification");app.modal=false;app.timer();
    expect(!app.preparationCancel && !app.preparationHeld && (app.busy || !app.player.snapshot().paused),"timer recovers deferred results and releases the hold or immediately starts fallback after extraction failure");
    until=GetTickCount64()+4000;do{app.sync();if(app.busy)break;Sleep(20);}while(GetTickCount64()<until);
    expect(app.busy && app.videoRecoveryAttempts==1,"failed background preparation falls back to one bounded original recovery");
    app.cancel_jobs();std::cout<<"PASS native cache hold, deferred failure and bounded fallback\n";
}
static void background_preparation_overall_eof(HWND host){
    auto shortAudio=executable_dir()+L"\\background-short-audio.wav";
    {std::ifstream input(executable_dir()+L"\\video-recovery-audio.wav",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());std::ofstream output(shortAudio,std::ios::binary);output.write(bytes.data(),44+48000);}
    App app;app.window=host;app.video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,160,90,host,nullptr,GetModuleHandleW(nullptr),nullptr);app.store=Store(executable_dir()+L"\\background-overall-eof-data");app.settings=Settings{};app.desiredPaused=false;
    app.guide=std::make_shared<Guide>();app.guide->bvid="BV1BMgj6MEz4";app.guide->parts.push_back({1,111,"both inputs ended early",10});app.requested={app.guide->bvid,1,false,""};
    app.player.initialize(app.video,executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll",[]{},SourceOptions{});app.player.volume(0);Media broken;broken.video=utf8(executable_dir()+L"\\video-truncated.avi");broken.audio=utf8(shortAudio);app.player.load(broken,0,false,"early overall EOF");app.preparationCancel=std::make_shared<Cancellation>();
    double confirmedEnd=0;auto until=GetTickCount64()+6000;do{app.sync();if(app.state.video_input.valid)confirmedEnd=app.state.video_input.video_end;if(app.state.eof)break;Sleep(20);}while(GetTickCount64()<until);
    if(!app.state.eof || app.desiredPaused || app.busy)std::cerr<<"Overall EOF diagnostic "<<json({{"eof",app.state.eof},{"paused",app.state.paused},{"intent",app.desiredPaused},{"busy",app.busy},{"loaded",app.state.loaded},{"position",app.state.position},{"input_valid",app.state.video_input.valid},{"video_end",app.state.video_input.video_end},{"remaining",app.state.video_input.video_remaining},{"audio_position",app.state.video_input.audio_position}}).dump()<<"\n";
    expect(app.state.eof && !app.desiredPaused && !app.busy,"an early overall EOF during preparation does not become a user pause");
    auto r=std::make_unique<Result>();r->generation=app.generation;r->preparation=true;r->preparation_epoch=app.preparationEpoch;r->entry=app.state.entry_id;r->cid=111;r->quality=app.settings.quality;r->cookies=app.settings.cookies;r->ref=app.requested;r->prepared_at=GetTickCount64();r->video_end=confirmedEnd;r->media.video=utf8(executable_dir()+L"\\video-complete.avi");r->media.audio=utf8(executable_dir()+L"\\video-recovery-audio.wav");r->media.height=16;r->media.source="background_ytdl";double expected=r->video_end-0.5;
    app.results.push_back(std::move(r));app.lastSave=9;app.timer();
    expect(app.videoRecoveryAttempts==1 && !app.prepared && app.applyIntentOnLoad,"a prepared replacement recovers early overall EOF automatically");
    expect(std::abs(app.store.history(app.guide->bvid)["parts"]["111"].get<double>()-expected)<0.05,"the timer does not overwrite a clamped recovery point with the old audio position");
    until=GetTickCount64()+5000;do{app.sync();if(app.state.loaded && app.state.position>4)break;Sleep(20);}while(GetTickCount64()<until);
    expect(app.state.loaded && !app.state.paused && app.state.position>4,"replacement audio and video advance after early overall EOF");
    std::cout<<"PASS early overall EOF intent and clamped progress preservation\n";
}
static void recovery_source_failure_regression(HWND host){
    App app;app.window=host;app.video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,160,90,host,nullptr,GetModuleHandleW(nullptr),nullptr);app.store=Store(executable_dir()+L"\\recovery-fallback-test-data");app.settings=Settings{};app.settings.speed=1.75;app.desiredPaused=false;
    app.guide=std::make_shared<Guide>();app.guide->bvid="BV1PyxQ6sEbH";app.guide->parts.push_back({17,43354948144,"P17 recovery replay",934.649875});app.requested={app.guide->bvid,17,false,""};app.refreshUsed=true;app.hadPlayback=true;
    app.fetchPublic=[](const Guide& guide,const Part& part,int quality,Cancellation& cancel){cancel.check();if(guide.bvid!="BV1PyxQ6sEbH" || part.page!=17 || part.cid!=43354948144 || quality!=720)throw std::runtime_error("fallback identity changed");Media media;media.video=utf8(executable_dir()+L"\\missing-recovery-format-test.mp4");return media;};
    app.player.initialize(app.video,executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll",[]{},SourceOptions{});
    app.state.loaded=true;app.state.position_valid=true;app.state.position=811.827612;app.state.duration=934.649875;app.state.paused=false;app.state.entry_id=11;app.state.height=480;app.state.audio_codec="aac";app.state.video_input={true,true,807.366688,0,811.838117};
    expect(!app.recover_video_input(1000) && app.recover_video_input(3000),"P17 drained input starts its first replacement after the earlier generic fallback was consumed");
    for(auto& job:app.jobs)if(job->thread.joinable())job->thread.join();expect(app.results.size()==1,"replacement metadata resolves the preserved exact part");
    app.pause();app.results.front()->public_streams=true;app.results.front()->media.video=utf8(executable_dir()+L"\\missing-recovery-format-test.mp4");app.accept_results();
    auto until=GetTickCount64()+5000;do{app.sync();if(app.busy || !app.lastError.empty())break;Sleep(20);}while(GetTickCount64()<until);
    expect(app.busy && app.lastError.empty() && app.videoRecoveryAttempts==2,"a failed recovery source still gets one bounded public fallback after generic retry was consumed");
    for(auto& job:app.jobs)if(job->thread.joinable())job->thread.join();expect(app.results.size()==1 && app.results.front()->error.empty() && app.results.front()->public_streams && app.results.front()->guide->parts[0].cid==43354948144 && std::abs(app.results.front()->resume.seconds-806.866688)<0.001 && app.desiredPaused && app.settings.speed==1.75,"alternate source retains exact P17 video resume point CID pause intent and speed");
    app.accept_results();until=GetTickCount64()+5000;do{app.sync();if(!app.lastError.empty())break;Sleep(20);}while(GetTickCount64()<until);
    expect(!app.busy && !app.lastError.empty() && app.videoRecoveryAttempts==2 && !app.recoveryFallbackPending,"an alternate-source load failure terminates without a third automatic load");
    app.open(app.requested,true);expect(app.videoRecoveryAttempts==0 && !app.recoveryFallbackPending,"manual retry resets both recovery source state and its budget");app.cancel_jobs();
    for(int mode=0;mode<4;++mode){App timeout;timeout.window=host;timeout.video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,160,90,host,nullptr,GetModuleHandleW(nullptr),nullptr);timeout.store=Store(executable_dir()+L"\\recovery-timeout-test-data");timeout.settings=Settings{};timeout.guide=app.guide;timeout.requested=app.requested;timeout.desiredPaused=true;timeout.fetchPublic=app.fetchPublic;timeout.player.initialize(timeout.video,executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll",[]{},SourceOptions{});timeout.videoRecoveryAttempts=(mode==1 || mode==3)?2:1;timeout.arm_recovery_fallback(806.866688);timeout.refreshUsed=mode!=1 && mode!=3;if(mode==3)timeout.recoveryFallbackPending=false;timeout.loadStarted=GetTickCount64()-46000;if(mode==2)++timeout.recoveryFallbackGeneration;timeout.sync();
        if(mode==0)expect(timeout.busy && timeout.videoRecoveryAttempts==2,"recovery connection timeout uses the same bounded alternate-source path");
        else expect(!timeout.busy && !timeout.lastError.empty() && timeout.videoRecoveryAttempts==((mode==1 || mode==3)?2u:1u),"exhausted or stale recovery fallback cannot enter a generic third retry");timeout.cancel_jobs();
    }
    {
        App success;success.window=host;success.video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,160,90,host,nullptr,GetModuleHandleW(nullptr),nullptr);success.guide=app.guide;success.requested=app.requested;success.desiredPaused=true;success.player.initialize(success.video,executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll",[]{},SourceOptions{});Media media;media.video=utf8(executable_dir()+L"\\video-complete.avi");media.audio=utf8(executable_dir()+L"\\video-recovery-audio.wav");success.player.load(media,1,true,"successful replacement");success.videoRecoveryAttempts=1;success.arm_recovery_fallback(1);until=GetTickCount64()+5000;do{success.sync();if(success.state.loaded && success.state.position_valid)break;Sleep(20);}while(GetTickCount64()<until);expect(success.state.loaded && !success.recoveryFallbackPending && success.videoRecoveryAttempts==1,"a successfully loaded recovery source disarms only its initial-load fallback");
        expect(!success.refreshUsed,"a successful first recovery restores an unused generic retry instead of consuming it permanently");
        media.video=utf8(executable_dir()+L"\\missing-later-source-test.mp4");media.audio.clear();success.player.load(media,1,true,"later source failure");until=GetTickCount64()+5000;do{success.sync();if(success.busy || !success.lastError.empty())break;Sleep(20);}while(GetTickCount64()<until);expect(success.busy && success.videoRecoveryAttempts==2 && success.lastError.empty(),"a later source failure consumes the remaining second recovery attempt without bypassing the cap");success.cancel_jobs();
    }
    std::cout<<"PASS replacement source fallback, timeout, identity, intent and strict budget\n";
}
static void background_recovery_guards(HWND host){
    auto fixture=[&](App& app){app.window=host;app.store=Store(executable_dir()+L"\\background-recovery-test-data");app.settings=Settings{};app.guide=std::make_shared<Guide>();app.guide->bvid="BV1BMgj6MEz4";app.guide->parts.push_back({1,111,"buffered failure",1112.213});app.requested={app.guide->bvid,1,false,""};app.desiredPaused=false;app.state=Snapshot{};app.state.loaded=true;app.state.position_valid=true;app.state.position=672.6;app.state.duration=1112.213;app.state.paused=false;app.state.entry_id=2;app.state.height=720;app.state.audio_codec="aac";app.state.video_input={true,true,690.766687,18.166687,672.6};};
    auto result=[&](App& app){auto r=std::make_unique<Result>();r->generation=app.generation;r->preparation=true;r->preparation_epoch=app.preparationEpoch;r->entry=app.state.entry_id;r->cid=111;r->quality=app.settings.quality;r->cookies=app.settings.cookies;r->ref=app.requested;r->media.height=720;r->media.video="https://example.invalid/video?deadline=9999999999";r->prepared_at=GetTickCount64();r->video_end=app.state.video_input.video_end;return r;};
    {
        App app;fixture(app);app.extractBackground=[](const VideoRef&,const Part&,const ExtractionOptions&,Cancellation& cancel)->Media{while(!cancel.cancelled)Sleep(5);throw Cancelled();};
        expect(!app.prepare_video_input(1000) && app.prepare_video_input(2000),"stable early EOF starts preparation before playable cache drains");
        expect(!app.busy && app.state.loaded && app.state.position==672.6 && app.videoRecoveryAttempts==0,"preparation retains active playback and reserves no reload attempt");
        expect(!app.prepare_video_input(4000),"a damaged entry starts at most one preparation");
        app.invalidate_preparation();expect(!app.preparationCancel && !app.prepared && !app.prepare_video_input(5000),"cancellation invalidates pending work without restarting the same entry");
    }
    for(int guard=0;guard<8;++guard){App app;fixture(app);if(guard==0)app.modal=true;if(guard==1)app.menuActive=true;if(guard==2)app.hidden=true;if(guard==3)app.desiredPaused=true;if(guard==4)app.state.seeking=true;if(guard==5)app.state.video_input.reader_eof=false;if(guard==6)app.state.video_input.video_end=1112.213;if(guard==7)app.videoRecoveryAttempts=2;
        expect(!app.prepare_video_input(1000) && !app.prepare_video_input(4000),"unconfirmed input, user interaction and exhausted budget never start preparation");
    }
    for(int stale=0;stale<7;++stale){App app;fixture(app);auto r=result(app);if(stale==0)++r->generation;if(stale==1)++r->preparation_epoch;if(stale==2)++r->entry;if(stale==3)++r->cid;if(stale==4)r->quality=480;if(stale==5)r->cookies=L"different-cookie-file";if(stale==6)r->media.height=480;
        app.results.push_back(std::move(r));app.accept_results();expect((!app.prepared || !app.prepared->error.empty()) && !app.busy && app.videoRecoveryAttempts==0,"obsolete identity settings or lower resolution cannot replace active playback");
    }
    {
        App app;fixture(app);app.prepared=result(app);app.prepared->prepared_at=1;expect(!app.try_prepared_handoff(60002) && !app.prepared,"a prepared URL expires locally after sixty seconds");
    }
    {
        App app;fixture(app);app.prepared=result(app);app.prepared->prepared_at=1;app.prepared->media=Media{};app.prepared->error="controlled failure";expect(!app.try_prepared_handoff(60002) && app.prepared && !app.busy,"failed preparation preserves URL-free boundary evidence beyond the URL freshness limit");
    }
    {
        App app;fixture(app);app.prepared=result(app);app.modal=true;expect(!app.try_prepared_handoff(GetTickCount64()) && app.prepared,"a modal dialog defers replacement");app.modal=false;app.menuActive=true;expect(!app.try_prepared_handoff(GetTickCount64()) && app.prepared,"an open menu defers replacement");app.menuActive=false;app.hidden=true;expect(!app.try_prepared_handoff(GetTickCount64()) && app.prepared,"a hidden player defers replacement");app.hidden=false;app.desiredPaused=true;expect(!app.try_prepared_handoff(GetTickCount64()) && app.prepared,"pause intent defers replacement without resuming playback");
        auto previous=GetWindowLongPtrW(host,GWLP_USERDATA);SetWindowLongPtrW(host,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&app));app.desiredPaused=false;window_proc(host,WM_HOTKEY,503,0);expect(!app.prepared,"manual seeking invalidates a prepared URL before playback restarts");SetWindowLongPtrW(host,GWLP_USERDATA,previous);
    }
    std::cout<<"PASS buffered source preparation and stale-result guards\n";
}
static void premature_video_controller_guards(HWND host){
    auto fixture=[&](App& app){app.window=host;app.store=Store(executable_dir()+L"\\video-recovery-guard-data");app.guide=std::make_shared<Guide>();app.guide->bvid="BV1BMgj6MEz4";app.guide->parts.push_back({1,111,"report replay",648.1333125});app.requested={app.guide->bvid,1,false,""};app.settings=Settings{};app.settings.speed=1.75;app.desiredPaused=false;app.state=Snapshot{};app.state.loaded=true;app.state.position_valid=true;app.state.position=571.254610258116;app.state.duration=648.1333125;app.state.paused=false;app.state.entry_id=2;app.state.height=720;app.state.audio_codec="aac";app.state.video_input={true,true,568.033312,0,571.261245208116};};
    for(int mode=0;mode<11;++mode){App app;fixture(app);
        if(mode==0)app.modal=true;if(mode==1)app.hidden=true;if(mode==2)app.desiredPaused=true;if(mode==3)app.state.paused=true;if(mode==4)app.state.seeking=true;if(mode==5)app.busy=true;if(mode==6)app.applyIntentOnLoad=true;if(mode==7)app.state.video_input.valid=false;if(mode==8)app.guide->bvid="BVprevious";if(mode==9)app.requested.page=2;if(mode==10)app.menuActive=true;
        expect(!app.recover_video_input(1000) && !app.recover_video_input(5000) && app.videoRecoveryAttempts==0,"paused hidden modal loading seeking or unavailable observations never trigger recovery");
    }
    {
        App app;fixture(app);expect(!app.recover_video_input(1000) && !app.recover_video_input(2999),"premature EOF must persist for two seconds");app.state.entry_id=3;expect(!app.recover_video_input(3000) && !app.recover_video_input(4999),"a new entry starts a fresh confirmation window");
        app.state.video_input.video_end+=1;expect(!app.recover_video_input(5000),"a changed video boundary resets confirmation");
        app.modal=true;expect(!app.recover_video_input(9000),"a dialog cancels an in-progress confirmation");app.modal=false;expect(!app.recover_video_input(9001),"resuming after a dialog requires a new confirmation");
    }
    {
        App app;fixture(app);auto previous=GetWindowLongPtrW(host,GWLP_USERDATA);SetWindowLongPtrW(host,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&app));
        for(int key:{503,504}){expect(!app.recover_video_input(1000),"prepare a confirmation before manual seek");window_proc(host,WM_HOTKEY,key,0);expect(app.videoCandidateEntry==-1,"manual seek dispatch cancels a pending recovery even if restart events coalesce");}
        SetWindowLongPtrW(host,GWLP_USERDATA,previous);
    }
    {
        App app;fixture(app);PlaybackDiagnostics blocked(executable_path());app.diagnostics=&blocked;
        for(unsigned attempt=1;attempt<=2;++attempt){app.state.entry_id=attempt;if(attempt==2)app.state.duration=568.1;expect(!app.recover_video_input(attempt*5000),"first stable observation does not reload immediately");expect(app.recover_video_input(attempt*5000+2000),"qualified video premature EOF requests recovery even when container duration shrinks");expect(app.busy && app.videoRecoveryAttempts==attempt && app.settings.speed==1.75,"automatic retries keep a strict budget and preserve settings");for(auto& job:app.jobs)if(job->thread.joinable())job->thread.join();expect(app.results.size()==1 && std::abs(app.results.front()->resume.seconds-567.533312)<0.001,"report failure cannot block same-CID resume from the video boundary");app.results.clear();app.jobs.clear();app.busy=false;fixture(app);}
        app.state.entry_id=9;expect(!app.recover_video_input(20000) && app.recover_video_input(22000),"third failure enters the bounded terminal path");expect(app.videoRecoveryAttempts==2 && app.desiredPaused && !app.lastError.empty() && !app.state.loaded,"exhausted recovery budget stops audio and explains manual retry");expect(std::abs(app.store.history(app.guide->bvid)["parts"]["111"].get<double>()-567.533312)<0.001,"exhausted recovery preserves the last useful video position");
        app.diagnostics=&app_diagnostics();app.open(app.requested,true);expect(app.videoRecoveryAttempts==0,"manual retry starts a new recovery budget");app.cancel_jobs();
    }
}
int main(){
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    HWND host=CreateWindowExW(0,L"STATIC",L"Controller test",WS_POPUP,0,0,480,320,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    try{
        {
            App diagnosticMenu;HMENU menu=diagnosticMenu.context_menu();wchar_t label[80]={};
            GetMenuStringW(menu,113,label,80,MF_BYCOMMAND);DestroyMenu(menu);
            expect(std::wstring(label)==L"保存故障现场","context menu offers a non-destructive diagnostic capture");
            diagnosticMenu.guide=std::make_shared<Guide>();diagnosticMenu.guide->bvid="BVprevious";diagnosticMenu.guide->parts.push_back({1,111,"old",30});
            diagnosticMenu.requested={"BVrequested",3,false,""};diagnosticMenu.busy=true;auto context=diagnosticMenu.diagnostic_context();
            expect(context["bvid"]=="BVrequested" && context["page"]==3 && !context.contains("cid"),"pending new source must not inherit the old guide identity");
        }
        premature_video_fixture(host);
        background_playback_overlap(host);
        background_preparation_failure(host);
        background_preparation_overall_eof(host);
        recovery_source_failure_regression(host);
        premature_video_controller_guards(host);
        background_recovery_guards(host);
        visible_edge_docking_regressions();
        screen_bounds_regressions();
        bounded_resize_geometry_regressions();
        resize_regressions();
        expect(write_settings(read_settings({{"menu_bar_visible",false}})).value("menu_bar_visible",true)==false,"hidden menu bar survives settings roundtrip");
        expect(write_settings(read_settings(json::object())).value("menu_bar_visible",false),"legacy settings show the menu bar by default");
        for(const json& bad:std::vector<json>{nullptr,"false",0,1,json::array(),json::object()}){
            expect(write_settings(read_settings({{"menu_bar_visible",bad}})).value("menu_bar_visible",false),"invalid saved menu visibility falls back to visible");
        }
        {
            json oldKeys=json::array({VK_F6,VK_F7,VK_F8,VK_F9,VK_F10});
            auto migrated=read_settings({{"hotkeys",oldKeys}});
            auto keys=write_settings(migrated)["hotkeys"];
            expect(keys.size()==9,"legacy hotkey settings gain speed and menu-bar shortcuts");
            for(size_t i=0;i<5;++i)expect(keys[i]==oldKeys[i],"migration preserves existing hotkeys");
            expect(keys[5]==MAKEWORD(VK_OEM_4,HOTKEYF_CONTROL|HOTKEYF_ALT) && keys[6]==MAKEWORD(VK_OEM_6,HOTKEYF_CONTROL|HOTKEYF_ALT) && keys[7]==MAKEWORD('0',HOTKEYF_CONTROL|HOTKEYF_ALT),"new shortcuts have documented default combinations");
            expect(keys[8]==MAKEWORD('M',HOTKEYF_CONTROL|HOTKEYF_ALT),"menu-bar toggle defaults to Ctrl+Alt+M");
            oldKeys[3]=MAKEWORD(VK_OEM_4,HOTKEYF_CONTROL|HOTKEYF_ALT);
            oldKeys[4]=MAKEWORD(VK_OEM_6,HOTKEYF_CONTROL|HOTKEYF_ALT);
            auto custom=write_settings(read_settings({{"hotkeys",oldKeys}}))["hotkeys"];
            expect(custom[3]==oldKeys[3] && custom[4]==oldKeys[4],"legacy custom bindings retain priority over new defaults");
            std::set<int> unique;for(const auto& key:custom)unique.insert(key.get<int>());
            expect(unique.size()==9,"migration resolves new-default conflicts without duplicate bindings");
            custom[5]=MAKEWORD(VK_F2,HOTKEYF_SHIFT|HOTKEYF_CONTROL);
            custom[6]=MAKEWORD(VK_F3,HOTKEYF_SHIFT|HOTKEYF_CONTROL);
            custom[7]=MAKEWORD(VK_F4,HOTKEYF_SHIFT|HOTKEYF_CONTROL);
            custom[8]=MAKEWORD(VK_F5,HOTKEYF_SHIFT|HOTKEYF_CONTROL);
            expect(write_settings(read_settings({{"hotkeys",custom}}))["hotkeys"]==custom,"all nine customized hotkeys survive roundtrip");
            json eight=custom;eight.erase(eight.end()-1);eight[0]=MAKEWORD('M',HOTKEYF_CONTROL|HOTKEYF_ALT);
            auto upgraded=write_settings(read_settings({{"hotkeys",eight}}))["hotkeys"];
            expect(upgraded.size()==9,"eight-key settings gain the menu-bar shortcut");
            for(size_t i=0;i<eight.size();++i)expect(upgraded[i]==eight[i],"eight-key migration preserves every custom binding");
            unique.clear();for(const auto& key:upgraded)unique.insert(key.get<int>());
            expect(unique.size()==9 && upgraded[8]!=eight[0],"menu-bar shortcut chooses a fallback when a legacy binding owns its default");
        }
        {
            App shortcuts;shortcuts.window=host;shortcuts.settings=Settings{};
            shortcuts.store=Store(executable_dir()+L"\\speed-hotkeys-test-data");
            auto previous=GetWindowLongPtrW(host,GWLP_USERDATA);SetWindowLongPtrW(host,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&shortcuts));
            auto hotkey=[&](WPARAM id){window_proc(host,WM_HOTKEY,id,0);};
            hotkey(505);expect(shortcuts.settings.speed==0.75,"global slower shortcut selects the previous speed preset");
            hotkey(506);expect(shortcuts.settings.speed==1.0,"global faster shortcut selects the next speed preset");
            for(int i=0;i<12;++i)hotkey(506);
            expect(shortcuts.settings.speed==2.0,"faster shortcut clamps at maximum speed");
            for(int i=0;i<12;++i)hotkey(505);
            expect(shortcuts.settings.speed==0.5,"slower shortcut clamps at minimum speed");
            hotkey(507);expect(shortcuts.settings.speed==1.0,"normal-speed shortcut restores one-times playback");
            shortcuts.set_speed(1.5);shortcuts.modal=true;hotkey(505);hotkey(506);hotkey(507);
            expect(shortcuts.settings.speed==1.5,"settings and picker dialogs ignore speed hotkeys");
            shortcuts.modal=false;shortcuts.hidden=true;shortcuts.desiredPaused=true;hotkey(506);
            expect(shortcuts.settings.speed==1.75 && shortcuts.hidden && shortcuts.desiredPaused,"hidden speed changes preserve visibility and pause intent");
            for(int i=0;i<HOTKEY_COUNT;++i)shortcuts.settings.hotkeys[i]=MAKEWORD(VK_F13+i,HOTKEYF_CONTROL|HOTKEYF_ALT|HOTKEYF_SHIFT);
            shortcuts.register_keys();
            expect(shortcuts.registered[5] && shortcuts.registered[6] && shortcuts.registered[7],"new speed hotkeys are registered with Windows");
            expect(shortcuts.registered[8],"menu-bar hotkey is registered with Windows");
            shortcuts.unregister_keys();
            HWND owner=CreateWindowExW(0,L"STATIC",L"Hotkey release fixture",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            expect(RegisterHotKey(owner,1,hotkey_modifiers(shortcuts.settings.hotkeys[6]),LOBYTE(shortcuts.settings.hotkeys[6]))!=FALSE,"all new speed hotkeys are released on unregister");
            UnregisterHotKey(owner,1);
            expect(RegisterHotKey(owner,1,hotkey_modifiers(shortcuts.settings.hotkeys[8]),LOBYTE(shortcuts.settings.hotkeys[8]))!=FALSE,"menu-bar hotkey is released on unregister");
            UnregisterHotKey(owner,1);DestroyWindow(owner);SetWindowLongPtrW(host,GWLP_USERDATA,previous);
        }
        {
            auto procedure=[](HWND,UINT,WPARAM,LPARAM)->INT_PTR{return FALSE;};
            HWND settings=CreateDialogParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_SETTINGS),host,procedure,0);
            expect(settings!=nullptr,"settings resource can be created");
            RECT client;GetClientRect(settings,&client);LONG last=0;
            for(int i=0;i<HOTKEY_COUNT;++i){
                HWND key=GetDlgItem(settings,IDC_HOTKEY_FIRST+i);expect(key!=nullptr,"settings exposes every configurable hotkey");
                RECT bounds;GetWindowRect(key,&bounds);MapWindowPoints(nullptr,settings,reinterpret_cast<POINT*>(&bounds),2);
                expect(bounds.top>=last && bounds.bottom<=client.bottom,"settings hotkey fields fit without overlap");last=bounds.bottom;
            }
            RECT cookies;GetWindowRect(GetDlgItem(settings,IDC_COOKIES),&cookies);MapWindowPoints(nullptr,settings,reinterpret_cast<POINT*>(&cookies),2);
            expect(cookies.top>last && cookies.bottom<=client.bottom,"login fields remain below all nine hotkeys");DestroyWindow(settings);
        }
        for(double rate:{0.5,0.75,1.0,1.25,1.5,1.75,2.0}){
            auto saved=write_settings(read_settings({{"speed",rate}}));
            expect(saved.value("speed",0.0)==rate,"chosen playback speed must survive settings roundtrip");
        }
        expect(write_settings(read_settings(json::object())).value("speed",1.0)==1.0,"old settings default to normal playback");
        for(const json& bad:std::vector<json>{nullptr,"2",true,0,-1,0.1,1.1,3,json::object(),std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
            expect(write_settings(read_settings({{"speed",bad}})).value("speed",1.0)==1.0,"invalid saved speed falls back to normal playback");
        }
        {
            App speed;speed.window=host;speed.settings=Settings{};
            speed.store=Store(executable_dir()+L"\\speed-menu-test-data");
            speed.command(204);
            expect(write_settings(speed.settings).value("speed",1.0)==1.5,"speed menu command applies its displayed preset before media is opened");
            Store persisted(executable_dir()+L"\\speed-menu-test-data");
            expect(persisted.data["settings"].value("speed",1.0)==1.5,"chosen speed is saved immediately");
        }
        {
            HWND ui=CreateWindowExW(0,L"STATIC",L"Toolbar fixture",WS_POPUP|WS_THICKFRAME,0,0,330,230,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            expect(ui!=nullptr,"toolbar fixture window");
            App toolbar;toolbar.window=ui;toolbar.store=Store(executable_dir()+L"\\speed-toolbar-test-data");
            toolbar.settings=Settings{};toolbar.settings.speed=1.75;
            for(size_t i=0;i<toolbar.settings.hotkeys.size();++i)toolbar.settings.hotkeys[i]=MAKEWORD(VK_F13+static_cast<BYTE>(i),HOTKEYF_CONTROL|HOTKEYF_ALT);
            toolbar.init();
            expect(GetDlgItem(ui,SPEED)!=nullptr,"speed has a visible toolbar entry");
            toolbar.command(204);
            wchar_t label[32]={};GetWindowTextW(GetDlgItem(ui,SPEED),label,32);
            expect(std::wstring(label)==L"1.5×","toolbar label follows the selected speed");
            HMENU rates=toolbar.speed_menu();
            expect(GetMenuItemCount(rates)==7,"speed menu exposes all seven presets");
            expect((GetMenuState(rates,204,MF_BYCOMMAND)&MF_CHECKED)!=0,"the selected speed is checked in the native menu");
            expect((GetMenuState(rates,202,MF_BYCOMMAND)&MF_CHECKED)==0,"normal speed is unchecked after a change");
            DestroyMenu(rates);
            HMENU context=toolbar.context_menu();
            expect((GetMenuState(context,MENU_BAR,MF_BYCOMMAND)&MF_CHECKED)!=0,"context menu checks the visible menu bar");
            wchar_t menuLabel[128]={};GetMenuStringW(context,MENU_BAR,menuLabel,128,MF_BYCOMMAND);
            expect(std::wstring(menuLabel).find(hotkey_name(toolbar.settings.hotkeys[8]))!=std::wstring::npos,"context menu shows the configured menu-bar shortcut");DestroyMenu(context);
            for(UINT dpi:{96u,144u,192u}){
                toolbar.dpi=dpi;toolbar.top=toolbar.scale(34);toolbar.bottom=toolbar.scale(25);toolbar.set_font();
                SetWindowPos(ui,nullptr,0,0,toolbar.scale(330),toolbar.scale(230),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE);toolbar.layout();
                RECT client;GetClientRect(ui,&client);LONG previous=0;
                for(auto button:toolbar.buttons){
                    RECT bounds;GetWindowRect(button,&bounds);MapWindowPoints(nullptr,ui,reinterpret_cast<POINT*>(&bounds),2);
                    expect(bounds.left>=previous && bounds.right<=client.right && bounds.right>bounds.left,"all toolbar buttons fit at minimum width and scaled DPI");
                    previous=bounds.right;
                    wchar_t text[32]={};GetWindowTextW(button,text,32);HDC dc=GetDC(button);auto old=SelectObject(dc,toolbar.font);SIZE size;GetTextExtentPoint32W(dc,text,GetWindowTextLengthW(button),&size);SelectObject(dc,old);ReleaseDC(button,dc);
                    expect(size.cx<=bounds.right-bounds.left,"toolbar labels fit without clipping");
                }
            }
            auto previous=GetWindowLongPtrW(ui,GWLP_USERDATA);SetWindowLongPtrW(ui,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&toolbar));
            RECT before;GetWindowRect(ui,&before);
            window_proc(ui,WM_HOTKEY,508,0);
            expect(!write_settings(toolbar.settings).value("menu_bar_visible",true) && toolbar.top==0,"global menu-bar shortcut hides the bar and removes its inset");
            context=toolbar.context_menu();
            expect((GetMenuState(context,MENU_BAR,MF_BYCOMMAND)&(MF_CHECKED|MF_GRAYED))==0,"context menu offers enabled recovery while menu bar is hidden");DestroyMenu(context);
            for(auto button:toolbar.buttons)expect((GetWindowLongPtrW(button,GWL_STYLE)&WS_VISIBLE)==0,"hidden menu-bar buttons leave the visible and tab-navigation surfaces");
            expect((GetWindowLongPtrW(toolbar.statusLabel,GWL_STYLE)&WS_VISIBLE)!=0,"playback status stays visible when the menu bar is hidden");
            RECT after;GetWindowRect(ui,&after);expect(EqualRect(&before,&after)!=FALSE,"menu-bar toggle preserves outer window bounds");
            Store persisted(executable_dir()+L"\\speed-toolbar-test-data");
            expect(persisted.data["settings"].value("menu_bar_visible",true)==false,"menu-bar visibility is saved immediately");
            for(UINT dpi:{96u,144u,192u}){
                RECT suggested={0,0,toolbar.scale(330),toolbar.scale(230)};
                window_proc(ui,WM_DPICHANGED,MAKELONG(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));
                RECT video;GetWindowRect(toolbar.video,&video);MapWindowPoints(nullptr,ui,reinterpret_cast<POINT*>(&video),2);
                RECT client;GetClientRect(ui,&client);
                expect(toolbar.top==0 && video.top==0 && video.bottom==client.bottom-toolbar.bottom,"hidden layout stays flush with the video at every DPI");
                POINT drag={client.right/2,client.bottom-toolbar.bottom/2};ClientToScreen(ui,&drag);
                expect(window_proc(ui,WM_NCHITTEST,0,MAKELPARAM(drag.x,drag.y))==HTCAPTION,"status strip remains a drag area without the menu bar");
                expect(SendMessageW(toolbar.statusLabel,WM_NCHITTEST,0,MAKELPARAM(drag.x,drag.y))==HTTRANSPARENT,"status control lets the parent handle dragging");
                expect(window_proc(ui,WM_NCHITTEST,0,MAKELPARAM(suggested.left,suggested.top))!=HTCAPTION,"status dragging preserves resize borders");
                for(auto button:toolbar.buttons)expect((GetWindowLongPtrW(button,GWL_STYLE)&WS_VISIBLE)==0,"DPI change keeps every menu-bar button hidden");
            }
            toolbar.modal=true;window_proc(ui,WM_HOTKEY,508,0);
            expect(toolbar.top==0,"modal dialogs ignore the menu-bar shortcut");
            toolbar.modal=false;toolbar.hidden=true;toolbar.pass=true;toolbar.desiredPaused=true;window_proc(ui,WM_COMMAND,MENU_BAR,0);
            expect(toolbar.top==toolbar.scale(34) && toolbar.hidden && toolbar.pass && toolbar.desiredPaused,"restoring menu bar preserves window visibility, pass-through and pause intent");
            for(auto button:toolbar.buttons)expect((GetWindowLongPtrW(button,GWL_STYLE)&WS_VISIBLE)!=0,"restoring menu bar shows every button");
            toolbar.pass=false;SetWindowLongPtrW(ui,GWLP_USERDATA,previous);
            toolbar.close();
            App restored;restored.settings=read_settings(persisted.data["settings"]);
            restored.store=Store(executable_dir()+L"\\menu-bar-startup-test-data");
            restored.window=CreateWindowExW(0,L"STATIC",L"Hidden menu startup",WS_POPUP|WS_THICKFRAME,0,0,330,230,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            restored.init();
            expect(restored.top==0,"startup restores the saved hidden menu bar");
            for(auto button:restored.buttons)expect((GetWindowLongPtrW(button,GWL_STYLE)&WS_VISIBLE)==0,"startup hides buttons before the outer window is shown");
            restored.close();
        }
        App app;app.window=host;app.video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,100,100,host,nullptr,GetModuleHandleW(nullptr),nullptr);
        app.guide=std::make_shared<Guide>(Guide{"BV1BMgj6MEz4","Guide",{{1,111,"first",60},{2,222,"second",120}}});
        auto pending=std::make_unique<Result>();pending->generation=app.generation;pending->error="deferred result";
        app.results.push_back(std::move(pending));app.modal=true;
        app.accept_results();
        expect(app.results.size()==1,"a modal picker must defer load results");
        app.modal=false;app.hidden=true;app.desiredPaused=true;app.pause();
        expect(app.desiredPaused,"F8 must not resume hidden playback");
        app.message="storage failure";app.lastError.clear();app.update_status();
        expect(app.status.find(L"storage failure")!=std::wstring::npos,"playback status must surface actionable warnings");
        app.hidden=false;app.desiredPaused=true;app.message.clear();
        app.loadStarted=1;app.fail("failed request");
        expect(app.loadStarted==0,"a terminal failure cancels its load watchdog");
        app.lastError.clear();app.message.clear();
        {
            App renewal;renewal.window=host;renewal.video=app.video;
            renewal.store=Store(executable_dir()+L"\\renewal-test-data");
            renewal.guide=app.guide;renewal.requested={"BV1BMgj6MEz4",1,false,""};
            renewal.lastPosition=42.5;renewal.desiredPaused=false;
            renewal.settings.speed=1.75;
            renewal.state.loaded=true;renewal.state.position_valid=true;renewal.state.position=42.5;
            renewal.state.source_deadline=1793856353;
            renewal.state.paused=false;
            expect(!renewal.renew_source(1793849200),"fresh signed streams must not interrupt playback");
            renewal.state.paused=true;
            expect(!renewal.renew_source(1793856400),"an expired paused stream waits until resume");
            renewal.state.paused=false;renewal.modal=true;
            expect(!renewal.renew_source(1793856400),"defer renewal while a picker is open");
            renewal.modal=false;
            expect(renewal.renew_source(1793856400) && renewal.busy,"resume after two hours must re-extract expired streams");
            expect(!renewal.desiredPaused && !renewal.refreshUsed,"scheduled renewal preserves play intent and the error retry");
            expect(renewal.settings.speed==1.75,"source renewal retains the selected playback speed");
            for(auto& job:renewal.jobs)if(job->thread.joinable())job->thread.join();
            expect(renewal.results.size()==1 && renewal.results.front()->resume.seconds==42.5,"source renewal preserves the current CID and playhead");
            renewal.busy=false;renewal.state.loaded=true;renewal.state.position_valid=true;renewal.state.paused=false;renewal.state.source_deadline=1793856353;
            expect(!renewal.renew_source(1793856401),"a stale extractor result must not cause a reload loop");
            renewal.state.source_deadline=1793863553;
            expect(!renewal.renew_source(1793856401),"a newly renewed stream stays loaded");
            expect(renewal.renew_source(1793863500),"long sessions can renew again before the next deadline");
        }
        const auto wav=executable_dir()+L"\\controller-silence.wav";
        {std::ofstream file(wav,std::ios::binary);auto u32=[&](unsigned value){file.write(reinterpret_cast<const char*>(&value),4);};auto u16=[&](unsigned short value){file.write(reinterpret_cast<const char*>(&value),2);};file.write("RIFF",4);u32(480036);file.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(8000);u32(16000);u16(2);u16(16);file.write("data",4);u32(480000);std::string silence(480000,0);file.write(silence.data(),silence.size());}
        app.player.initialize(app.video,executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll",[]{},SourceOptions{},[&app](const char* event,const json& fields){app.diagnostics->record(event,fields);});app.player.volume(0);
        Media local;local.video=utf8(wav);app.player.load(local,12.5,true,"restart regression");
        auto deadline=GetTickCount64()+10000;
        do{MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}app.player.drain();app.state=app.player.snapshot();if(app.state.loaded)break;Sleep(10);}while(GetTickCount64()<deadline);
        expect(app.state.loaded && app.state.position>12,"local paused fixture loaded");
        expect(app.state.position_valid,"loaded playhead must be explicitly valid");
        {
            auto before=app.player.snapshot();auto properties=app.player.diagnostic_snapshot();
            expect(properties["values"]["time-pos"].is_number() && properties["status"]["time-pos"]==0,"diagnostics keep a valid numeric media clock");
            expect(properties["values"]["video-params/h"].is_null() && properties["status"]["video-params/h"].get<int>()<0,"audio-only media reports video properties as unavailable rather than zero");
            app.capture_diagnostics("test-capture");auto reportPath=app.save_fault_report(false);std::ifstream file(reportPath,std::ios::binary);json report;file>>report;
            expect(report.is_object() && report.contains("records") && !report["records"].empty(),"manual capture writes a readable report with recent evidence");
            const auto& captured=report["records"].back()["fields"]["snapshot"];
            expect(captured.contains("values") && captured["values"].contains("video-params/h") && captured["values"]["video-params/h"].is_null() && captured["status"]["video-params/h"].get<int>()<0,"real exported snapshots retain unavailable-property evidence within the line limit");
            auto after=app.player.snapshot();expect(after.paused==before.paused && after.entry_id==before.entry_id && std::abs(after.position-before.position)<0.01 && after.speed==before.speed,"manual report never resumes, seeks or replaces paused media");
            PlaybackDiagnostics unavailable(executable_path());auto previous=app.diagnostics;app.diagnostics=&unavailable;bool failed=false;
            try{app.save_fault_report(false);}catch(...){failed=true;}app.diagnostics=previous;
            expect(failed && app.player.snapshot().entry_id==before.entry_id && app.player.snapshot().paused,"unwritable diagnostics report fails honestly without changing playback");
        }
        {
            auto entry=app.state.entry_id;double position=app.state.position;
            app.desiredPaused=true;
            auto prior=GetWindowLongPtrW(host,GWLP_USERDATA);SetWindowLongPtrW(host,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&app));
            window_proc(host,WM_HOTKEY,508,0);auto menuHidden=app.player.snapshot();
            expect(menuHidden.paused && menuHidden.entry_id==entry && std::abs(menuHidden.position-position)<0.01 && app.desiredPaused,"menu-bar toggle leaves real paused playback and its playhead intact");
            window_proc(host,WM_HOTKEY,508,0);SetWindowLongPtrW(host,GWLP_USERDATA,prior);
            app.command(204);auto changed=app.player.snapshot();
            std::cerr<<"Paused speed snapshot: before="<<position<<" after="<<changed.position<<" valid="<<changed.position_valid<<" status="<<changed.position_status<<" paused="<<changed.paused<<" entries="<<entry<<"/"<<changed.entry_id<<" speed="<<changed.speed<<"\n";
            // Rebuilding audio tempo may adjust the reported audio clock by a small buffer duration.
            expect(changed.paused && !changed.seeking && changed.entry_id==entry && std::abs(changed.position-position)<0.3,"changing speed while paused neither reloads nor seeks");
            expect(std::abs(changed.speed-1.5)<0.001,"menu command applies speed to libmpv");
            auto previous=GetWindowLongPtrW(host,GWLP_USERDATA);SetWindowLongPtrW(host,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&app));
            window_proc(host,WM_HOTKEY,505,0);auto slower=app.player.snapshot();
            expect(slower.paused && slower.entry_id==entry && std::abs(slower.speed-1.25)<0.001,"global speed hotkey changes the real paused player without reloading");
            app.hidden=true;window_proc(host,WM_HOTKEY,506,0);
            expect(app.player.snapshot().paused && app.hidden,"global speed hotkey cannot resume a hidden player");
            app.hidden=false;window_proc(host,WM_HOTKEY,507,0);SetWindowLongPtrW(host,GWLP_USERDATA,previous);
            expect(std::abs(app.player.snapshot().speed-1.0)<0.001 && app.player.snapshot().paused,"global reset-speed hotkey retains pause");
            auto pump=[&](DWORD milliseconds){auto end=GetTickCount64()+milliseconds;do{MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}app.player.drain();Sleep(10);}while(GetTickCount64()<end);app.state=app.player.snapshot();};
            auto measure=[&](int command){
                app.command(command);app.desiredPaused=false;app.player.set_pause(false);pump(700);
                auto playing=app.player.snapshot();app.command(MENU_BAR);auto toggled=app.player.snapshot();
                expect(!toggled.paused && !app.desiredPaused && toggled.entry_id==playing.entry_id && toggled.speed==playing.speed,"menu-bar toggle preserves running playback, entry and speed");app.command(MENU_BAR);
                double before=app.state.position;auto wall=GetTickCount64();pump(1200);
                double rate=(app.state.position-before)/((GetTickCount64()-wall)/1000.0);
                app.player.set_pause(true);app.desiredPaused=true;return rate;
            };
            double slow=measure(200),fast=measure(206);
            std::cout<<"Measured playback rates: half="<<slow<<" double="<<fast<<"\n";
            expect(slow>0.3 && slow<0.8,"half speed advances half a media second per real second");
            expect(fast>1.5 && fast<2.5,"double speed advances two media seconds per real second");
            app.player.load(local,12.5,true,"speed reload");
            deadline=GetTickCount64()+10000;
            do{pump(20);}while(GetTickCount64()<deadline && (!app.state.loaded || !app.state.position_valid));
            expect(app.state.loaded && app.state.paused && std::abs(app.state.speed-2.0)<0.001,"loading another media entry retains speed and pause");
            app.player.shutdown();app.command(203);
            auto load=std::make_unique<Result>();load->generation=app.generation;load->guide=app.guide;load->ref={app.guide->bvid,1,false,""};load->resume={0,12.5};load->media=local;load->public_streams=true;
            app.results.push_back(std::move(load));app.accept_results();
            // A selection made while loading must win over the old preference.
            app.command(205);
            deadline=GetTickCount64()+10000;
            do{pump(20);}while(GetTickCount64()<deadline && (!app.state.loaded || !app.state.position_valid));
            expect(app.state.loaded && app.state.paused && std::abs(app.state.speed-1.75)<0.001,"recreated player uses the newest speed selected during loading");
            app.player.seek(5);deadline=GetTickCount64()+5000;
            do{pump(20);}while(GetTickCount64()<deadline && app.state.position<17);
            expect(app.state.paused && app.state.position>17 && app.state.position<18 && std::abs(app.state.speed-1.75)<0.001,"five-second seek uses media time and preserves pause and speed");
            app.player.seek(-5);deadline=GetTickCount64()+5000;
            do{pump(20);}while(GetTickCount64()<deadline && app.state.position>13);
            expect(app.state.position>12 && app.state.position<13,"restore local fixture playhead");
            app.command(202);
        }
        app.store.remember(app.guide->bvid,111,12.5);app.lastPosition=12.5;
        app.state.position=0;app.state.position_valid=false;app.save();
        expect(app.store.history(app.guide->bvid)["parts"]["111"]==12.5,"an unavailable playhead must preserve the saved position");
        app.state=app.player.snapshot();
        app.requested={app.guide->bvid,1,false,""};app.command(FROM_START);
        expect(app.store.history(app.guide->bvid)["parts"]["111"]==0,"restart must not overwrite zero with the old playhead");
        app.cancel_jobs();++app.generation;app.busy=false;
        Media missing;missing.video=utf8(executable_dir()+L"\\missing-fixture.mp4");app.player.load(missing,0,true,"failed load");
        deadline=GetTickCount64()+5000;
        do{app.player.drain();if(!app.player.snapshot().error.empty())break;Sleep(10);}while(GetTickCount64()<deadline);
        expect(!app.player.snapshot().error.empty(),"local failure fixture");
        {
            auto reportPath=app.save_fault_report(false);std::ifstream file(reportPath,std::ios::binary);json report;file>>report;bool log=false,ended=false,beforeSeek=false,afterSeek=false,beforeStop=false;
            for(const auto& entry:report["records"]){auto event=entry.value("event",std::string());log=log || event=="mpv-log";ended=ended || event=="mpv-end-file";beforeSeek=beforeSeek || event=="before-seek";afterSeek=afterSeek || event=="after-seek";beforeStop=beforeStop || event=="before-stop";}
            expect(log && ended,"real libmpv failures leave a warning/error summary and end-file evidence");
            expect(beforeSeek && afterSeek && beforeStop,"seek and stop preserve before/after evidence in the report");
        }
        app.modal=true;app.refreshUsed=false;app.sync();
        expect(!app.refreshUsed && !app.busy,"a modal dialog must not consume a refresh attempt");
        app.modal=false;app.sync();
        expect(app.refreshUsed && app.busy,"refresh starts after the modal dialog closes");
        app.cancel_jobs();
        HWND owner=CreateWindowExW(0,L"STATIC",L"Hotkey fixture",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        expect(RegisterHotKey(owner,1,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_F24)!=FALSE,"controlled hotkey owner");
        auto menuKey=app.settings.hotkeys[8];app.settings.hotkeys[8]=MAKEWORD(VK_F24,HOTKEYF_CONTROL|HOTKEYF_ALT);app.register_keys();
        expect(!app.registered[8] && !app.hotkeyWarning.empty(),"menu-bar hotkey conflict is reported");
        app.settings.menu_bar_visible=false;app.command(MENU_BAR);
        expect(app.settings.menu_bar_visible,"context-menu command can recover the menu bar when its shortcut is occupied");
        app.unregister_keys();app.settings.hotkeys[8]=menuKey;
        auto fasterKey=app.settings.hotkeys[6];app.settings.hotkeys[6]=MAKEWORD(VK_F24,HOTKEYF_CONTROL|HOTKEYF_ALT);app.register_keys();
        expect(!app.registered[6] && !app.hotkeyWarning.empty(),"a speed hotkey conflict is reported");app.unregister_keys();app.settings.hotkeys[6]=fasterKey;
        app.settings.hotkeys[0]=MAKEWORD(VK_F24,HOTKEYF_CONTROL|HOTKEYF_ALT);app.pass=true;app.register_keys();
        expect(!app.registered[0] && !app.pass && !app.hotkeyWarning.empty(),"hotkey conflict must unlock the window and remain visible");
        app.unregister_keys();UnregisterHotKey(owner,1);DestroyWindow(owner);
        app.hidden=true;app.desiredPaused=true;app.show_existing();
        expect(!app.hidden && app.desiredPaused,"relaunch recovery shows the window paused");
        app.store=Store(executable_path());app.save_safely();app.message.clear();app.update_status();
        expect(!app.storageWarning.empty() && app.status==wide(app.storageWarning),"save warning survives transient status changes");
        std::cout<<"PASS controller speed, menu bar, modal, visibility and warning regressions\n";
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";DestroyWindow(host);return 1;}
    DestroyWindow(host);
}
