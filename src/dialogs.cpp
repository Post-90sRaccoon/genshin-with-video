#include "dialogs.h"
#include "resource.h"
#include <commctrl.h>
#include <commdlg.h>
#include <set>
namespace gv {
static std::wstring item_text(HWND dialog,int id) {HWND w=GetDlgItem(dialog,id);int n=GetWindowTextLengthW(w);std::wstring s(n+1,0);GetWindowTextW(w,&s[0],n+1);s.resize(n);return s;}
Settings read_settings(const json& data) {
    Settings s;try {
        if(data.contains("quality") && data["quality"].is_number_integer()){int q=data["quality"].get<int>();if(q==360 || q==480 || q==720)s.quality=q;}
        if(data.contains("volume") && data["volume"].is_number_integer())s.volume=std::clamp(data["volume"].get<int>(),0,100);
        if(data.contains("speed") && data["speed"].is_number()){double speed=data["speed"].get<double>();if(valid_playback_speed(speed))s.speed=speed;}
        if(data.contains("cookies") && data["cookies"].is_string())s.cookies=wide(data["cookies"].get<std::string>());
        auto stored=data.find("hotkeys");
        if(stored!=data.end() && stored->is_array() && (stored->size()==5 || stored->size()==s.hotkeys.size())){
            std::set<WORD> unique;auto keys=s.hotkeys;bool valid=true;
            for(size_t i=0;i<stored->size();++i){
                if(!(*stored)[i].is_number_integer()){valid=false;break;}
                int k=(*stored)[i].get<int>();
                if(k<=0 || k>65535 || LOBYTE(k)==VK_F12 || LOBYTE(k)==0 || (HIBYTE(k)&~7)!=0 || !unique.insert(static_cast<WORD>(k)).second){valid=false;break;}
                keys[i]=static_cast<WORD>(k);
            }
            if(valid){
                for(size_t i=stored->size();i<keys.size();++i){
                    if(!unique.insert(keys[i]).second){
                        for(int key=VK_F1;key<VK_F12;++key){
                            WORD fallback=MAKEWORD(key,HOTKEYF_CONTROL|HOTKEYF_ALT|HOTKEYF_SHIFT);
                            if(unique.insert(fallback).second){keys[i]=fallback;break;}
                        }
                    }
                }
                s.hotkeys=keys;
            }
        }
    }catch(...){}return s;
}
json write_settings(const Settings& s){return {{"quality",s.quality},{"volume",s.volume},{"speed",s.speed},{"cookies",utf8(s.cookies)},{"hotkeys",s.hotkeys}};}
UINT hotkey_modifiers(WORD key){BYTE m=HIBYTE(key);return MOD_NOREPEAT|((m&HOTKEYF_ALT)?MOD_ALT:0)|((m&HOTKEYF_CONTROL)?MOD_CONTROL:0)|((m&HOTKEYF_SHIFT)?MOD_SHIFT:0);}
std::wstring hotkey_name(WORD key){std::wstring r;BYTE m=HIBYTE(key);if(m&HOTKEYF_CONTROL)r+=L"Ctrl+";if(m&HOTKEYF_ALT)r+=L"Alt+";if(m&HOTKEYF_SHIFT)r+=L"Shift+";wchar_t name[128]={};GetKeyNameTextW(static_cast<LONG>(MapVirtualKeyW(LOBYTE(key),MAPVK_VK_TO_VSC)<<16),name,128);return r+name;}
struct OpenContext {std::string value;};
static INT_PTR CALLBACK open_proc(HWND d,UINT msg,WPARAM w,LPARAM l){auto c=reinterpret_cast<OpenContext*>(GetWindowLongPtrW(d,DWLP_USER));if(msg==WM_INITDIALOG){c=reinterpret_cast<OpenContext*>(l);SetWindowLongPtrW(d,DWLP_USER,l);SetDlgItemTextW(d,IDC_URL,wide(c->value).c_str());SendDlgItemMessageW(d,IDC_URL,EM_SETLIMITTEXT,4096,0);SetFocus(GetDlgItem(d,IDC_URL));SendDlgItemMessageW(d,IDC_URL,EM_SETSEL,0,-1);return FALSE;}if(msg==WM_COMMAND){if(LOWORD(w)==IDCANCEL){EndDialog(d,0);return TRUE;}if(LOWORD(w)==IDOK){try{auto value=utf8(item_text(d,IDC_URL));parse_video_url(value);c->value=value;EndDialog(d,1);}catch(const std::exception& e){MessageBoxW(d,wide(e.what()).c_str(),L"链接无效",MB_OK|MB_ICONINFORMATION);}return TRUE;}}return FALSE;}
std::optional<std::string> open_dialog(HWND owner,const std::string& initial){OpenContext c{initial};if(DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_OPEN),owner,open_proc,reinterpret_cast<LPARAM>(&c))==1)return c.value;return {};}
struct PartsContext {const Guide& guide;size_t selected;};
static INT_PTR CALLBACK parts_proc(HWND d,UINT msg,WPARAM w,LPARAM l){auto c=reinterpret_cast<PartsContext*>(GetWindowLongPtrW(d,DWLP_USER));if(msg==WM_INITDIALOG){c=reinterpret_cast<PartsContext*>(l);SetWindowLongPtrW(d,DWLP_USER,l);for(const auto& p:c->guide.parts){auto label=L"P"+std::to_wstring(p.page)+L"  "+wide(p.title);SendDlgItemMessageW(d,IDC_PARTLIST,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}SendDlgItemMessageW(d,IDC_PARTLIST,LB_SETCURSEL,c->selected,0);return TRUE;}if(msg==WM_COMMAND){if(LOWORD(w)==IDCANCEL){EndDialog(d,0);return TRUE;}if(LOWORD(w)==IDOK || (LOWORD(w)==IDC_PARTLIST && HIWORD(w)==LBN_DBLCLK)){auto i=SendDlgItemMessageW(d,IDC_PARTLIST,LB_GETCURSEL,0,0);if(i!=LB_ERR){c->selected=static_cast<size_t>(i);EndDialog(d,1);}return TRUE;}}return FALSE;}
std::optional<size_t> parts_dialog(HWND owner,const Guide& guide,size_t current){PartsContext c{guide,current};if(DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_PARTS),owner,parts_proc,reinterpret_cast<LPARAM>(&c))==1)return c.selected;return {};}
static INT_PTR CALLBACK settings_proc(HWND d,UINT msg,WPARAM w,LPARAM l){auto c=reinterpret_cast<Settings*>(GetWindowLongPtrW(d,DWLP_USER));if(msg==WM_INITDIALOG){c=reinterpret_cast<Settings*>(l);SetWindowLongPtrW(d,DWLP_USER,l);for(auto label:{L"360p",L"480p",L"720p"})SendDlgItemMessageW(d,IDC_QUALITY,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendDlgItemMessageW(d,IDC_QUALITY,CB_SETCURSEL,c->quality==360?0:c->quality==480?1:2,0);SetDlgItemInt(d,IDC_VOLUME,c->volume,FALSE);SetDlgItemTextW(d,IDC_COOKIES,c->cookies.c_str());for(int i=0;i<HOTKEY_COUNT;++i)SendDlgItemMessageW(d,IDC_HOTKEY_FIRST+i,HKM_SETHOTKEY,c->hotkeys[i],0);return TRUE;}if(msg==WM_COMMAND){if(LOWORD(w)==IDCANCEL){EndDialog(d,0);return TRUE;}if(LOWORD(w)==IDC_BROWSE){wchar_t file[32768]={};OPENFILENAMEW of={sizeof(of)};of.hwndOwner=d;of.lpstrFile=file;of.nMaxFile=32768;of.lpstrFilter=L"Cookie 文件 (*.txt)\0*.txt\0所有文件\0*.*\0";of.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&of))SetDlgItemTextW(d,IDC_COOKIES,file);return TRUE;}if(LOWORD(w)==IDOK){Settings s=*c;BOOL valid=FALSE;UINT volume=GetDlgItemInt(d,IDC_VOLUME,&valid,FALSE);std::set<WORD> keys;for(int i=0;i<HOTKEY_COUNT;++i){s.hotkeys[i]=static_cast<WORD>(SendDlgItemMessageW(d,IDC_HOTKEY_FIRST+i,HKM_GETHOTKEY,0,0));if(!LOBYTE(s.hotkeys[i]) || LOBYTE(s.hotkeys[i])==VK_F12 || !keys.insert(s.hotkeys[i]).second){MessageBoxW(d,L"快捷键不能为空、不能重复，也不能使用 F12。",L"请调整快捷键",MB_OK|MB_ICONINFORMATION);return TRUE;}}if(!valid || volume>100){MessageBoxW(d,L"音量请输入 0 到 100。",L"音量无效",MB_OK);return TRUE;}s.volume=static_cast<int>(volume);auto selection=SendDlgItemMessageW(d,IDC_QUALITY,CB_GETCURSEL,0,0);s.quality=selection==0?360:selection==1?480:720;s.cookies=item_text(d,IDC_COOKIES);if(!s.cookies.empty() && GetFileAttributesW(s.cookies.c_str())==INVALID_FILE_ATTRIBUTES){MessageBoxW(d,L"找不到选择的 cookies 文件。请重新选择或清空。",L"文件不存在",MB_OK|MB_ICONINFORMATION);return TRUE;}*c=s;EndDialog(d,1);return TRUE;}}return FALSE;}
std::optional<Settings> settings_dialog(HWND owner,const Settings& settings){Settings c=settings;if(DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_SETTINGS),owner,settings_proc,reinterpret_cast<LPARAM>(&c))==1)return c;return {};}
}
