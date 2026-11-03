// Exercise the actual controller, without sending input to external windows.
#include "../src/main.cpp"
#include <iostream>
#include <fstream>
static void expect(bool value,const char* name){if(!value)throw std::runtime_error(name);}
int main(){
    HWND host=CreateWindowExW(0,L"STATIC",L"Controller test",WS_POPUP,0,0,480,320,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    try{
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
        const auto wav=executable_dir()+L"\\controller-silence.wav";
        {std::ofstream file(wav,std::ios::binary);auto u32=[&](unsigned value){file.write(reinterpret_cast<const char*>(&value),4);};auto u16=[&](unsigned short value){file.write(reinterpret_cast<const char*>(&value),2);};file.write("RIFF",4);u32(480036);file.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(8000);u32(16000);u16(2);u16(16);file.write("data",4);u32(480000);std::string silence(480000,0);file.write(silence.data(),silence.size());}
        app.player.initialize(app.video,executable_dir()+L"\\..\\third_party\\mpv\\libmpv-2.dll",[]{});app.player.volume(0);
        Media local;local.video=utf8(wav);app.player.load(local,12.5,true,"restart regression");
        auto deadline=GetTickCount64()+10000;
        do{MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}app.player.drain();app.state=app.player.snapshot();if(app.state.loaded)break;Sleep(10);}while(GetTickCount64()<deadline);
        expect(app.state.loaded && app.state.position>12,"local paused fixture loaded");
        expect(app.state.position_valid,"loaded playhead must be explicitly valid");
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
        app.settings.hotkeys[0]=MAKEWORD(VK_F24,HOTKEYF_CONTROL|HOTKEYF_ALT);app.pass=true;app.register_keys();
        expect(!app.registered[0] && !app.pass && !app.hotkeyWarning.empty(),"hotkey conflict must unlock the window and remain visible");
        app.unregister_keys();UnregisterHotKey(owner,1);DestroyWindow(owner);
        app.hidden=true;app.desiredPaused=true;app.show_existing();
        expect(!app.hidden && app.desiredPaused,"relaunch recovery shows the window paused");
        app.store=Store(executable_path());app.save_safely();app.message.clear();app.update_status();
        expect(!app.storageWarning.empty() && app.status==wide(app.storageWarning),"save warning survives transient status changes");
        std::cout<<"PASS controller modal, visibility and warning regressions\n";
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";DestroyWindow(host);return 1;}
    DestroyWindow(host);
}
