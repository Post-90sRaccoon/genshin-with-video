// Exercise the actual controller, without sending input to external windows.
#include "../src/main.cpp"
#include <iostream>
#include <fstream>
#include <cmath>
#include <limits>
#include <set>
#include "../src/resource.h"
static void expect(bool value,const char* name){if(!value)throw std::runtime_error(name);}
static void resize_regressions(){
    const DWORD style=WS_POPUP|WS_THICKFRAME,extended=WS_EX_NOACTIVATE|WS_EX_APPWINDOW;
    HWND ui=CreateWindowExW(extended,L"STATIC",L"Resize fixture",style,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    expect(ui!=nullptr,"resize fixture window");
    App sizing;sizing.window=ui;sizing.settings=Settings{};
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
int main(){
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    HWND host=CreateWindowExW(0,L"STATIC",L"Controller test",WS_POPUP,0,0,480,320,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    try{
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
        app.player.initialize(app.video,executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll",[]{});app.player.volume(0);
        Media local;local.video=utf8(wav);app.player.load(local,12.5,true,"restart regression");
        auto deadline=GetTickCount64()+10000;
        do{MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}app.player.drain();app.state=app.player.snapshot();if(app.state.loaded)break;Sleep(10);}while(GetTickCount64()<deadline);
        expect(app.state.loaded && app.state.position>12,"local paused fixture loaded");
        expect(app.state.position_valid,"loaded playhead must be explicitly valid");
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
