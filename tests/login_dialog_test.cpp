#include "login_dialog.h"
#include "login_database.h"
#include "login_adoption.h"
#include "search_ui_layout.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <windows.h>
namespace { int phase=0;DWORD changedAt=0;std::atomic<int> calls{0},adoptCalls{0};int recMode=0;
UINT layoutDpi=0;
void check(bool ok,const char* msg){if(!ok){std::cerr<<msg<<'\n';std::exit(1);}}
HWND thread_window(const wchar_t* name) {
    struct Match {const wchar_t* name;HWND window=nullptr;} match{name};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM parameter)->BOOL{
        auto* m=reinterpret_cast<Match*>(parameter);wchar_t cls[128]{};
        GetClassNameW(window,cls,128);if(std::wstring(cls)==m->name){m->window=window;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&match));return match.window;
}
RECT control_bounds(HWND dialog,int id) {
    HWND control=GetDlgItem(dialog,id);check(control!=nullptr,"expected login control exists");
    RECT rect{};GetWindowRect(control,&rect);
    MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&rect),2);return rect;
}
void check_layout(HWND dialog,bool sources) {
    RECT client{};GetClientRect(dialog,&client);
    SCROLLINFO scroll{sizeof(SCROLLINFO),SIF_ALL};GetScrollInfo(dialog,SB_VERT,&scroll);
    const int contentHeight=std::max(client.bottom,LONG(scroll.nMax+1));
    std::vector<int> ids{101,102,103,3,4,6,201,202,203,401,402,403,404,405};
    if(sources)for(int id:{301,302,303,304,406})ids.push_back(id);
    for(int id:ids){
        if(!IsWindowVisible(GetDlgItem(dialog,id)))continue;
        const auto rect=control_bounds(dialog,id);
        const bool brand=id>=401&&id<=403;
        const int offset=brand?0:scroll.nPos;
        const int limit=brand?client.bottom:contentHeight;
        if(rect.left<0||rect.top+offset<0||rect.right>client.right||rect.bottom+offset>limit){
            std::cerr<<"out of bounds phase="<<phase<<" dpi="<<layoutDpi<<" control="<<id
                <<" rect=("<<rect.left<<","<<rect.top<<","<<rect.right<<","<<rect.bottom
                <<") client=("<<client.right<<","<<client.bottom<<") scroll="<<scroll.nPos
                <<" content="<<contentHeight<<"\n";
        }
        check(rect.left>=0&&rect.top+offset>=0&&rect.right<=client.right&&rect.bottom+offset<=limit,
            "login controls remain inside the scrollable content at each DPI");
        check(rect.right>rect.left&&rect.bottom>rect.top,"login controls have positive area");
    }
    for(std::size_t i=0;i<ids.size();++i)for(std::size_t j=i+1;j<ids.size();++j){
        if(!IsWindowVisible(GetDlgItem(dialog,ids[i]))||!IsWindowVisible(GetDlgItem(dialog,ids[j])))continue;
        auto a=control_bounds(dialog,ids[i]),b=control_bounds(dialog,ids[j]);RECT overlap{};
        if(IntersectRect(&overlap,&a,&b)){
            std::cerr<<"overlap phase="<<phase<<" dpi="<<layoutDpi<<" controls="<<ids[i]<<","<<ids[j]
                <<" rects=("<<a.left<<","<<a.top<<","<<a.right<<","<<a.bottom
                <<") ("<<b.left<<","<<b.top<<","<<b.right<<","<<b.bottom<<")\n";
        }
        check(!IntersectRect(&overlap,&a,&b),"brand, form and source controls do not overlap");
    }
    if(IsWindowVisible(GetDlgItem(dialog,401)))
        check(control_bounds(dialog,401).right<control_bounds(dialog,101).left,"brand and login form occupy separate columns");
    HDC dc=GetDC(dialog);
    auto font=reinterpret_cast<HFONT>(SendMessageW(GetDlgItem(dialog,6),WM_GETFONT,0,0));
    auto previous=SelectObject(dc,font);
    const auto feedback=control_bounds(dialog,6);
    for(const wchar_t* value:{L"请输入登录名；登录名不能包含单引号、分号或双连字符。",
        L"该账号处于错误次数限时锁定期间，请稍后重试。"}){
        RECT measured{0,0,feedback.right-feedback.left,0};
        DrawTextW(dc,value,-1,&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
        check(measured.bottom<=feedback.bottom-feedback.top,"long login feedback fits without clipping");
    }
    SelectObject(dc,previous);ReleaseDC(dialog,dc);
}
void check_focus_navigation(HWND dialog,bool sources) {
    std::vector<int> ids{101,102,4,3};
    if(sources)for(int id:{301,303,302})ids.push_back(id);
    for(int id:ids){
        HWND control=GetDlgItem(dialog,id);
        if(!IsWindowEnabled(control))continue;
        SetFocus(control);
        check(IsWindow(dialog),"focusing a button never submits or cancels login");
        check(GetFocus()==control,"login controls remain keyboard focusable");
        const auto rect=control_bounds(dialog,id);RECT client{};GetClientRect(dialog,&client);
        check(rect.top>=0&&rect.bottom<=client.bottom,"focused login control scrolls fully into view");
    }
    SetFocus(GetDlgItem(dialog,101));
}
void check_dpi_layouts(HWND dialog,bool sources) {
    check_layout(dialog,sources);
    RECT original{};GetWindowRect(dialog,&original);
    const UINT actual=UINT(96*search::dpi_scale_factor(dialog)+0.5f);
    for(UINT dpi:{96u,120u,144u,192u}){
        layoutDpi=dpi;
        RECT suggested=original;
        SendMessageW(dialog,0x02e0,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));
        check_layout(dialog,sources);
        check_focus_navigation(dialog,sources);
    }
    SendMessageW(dialog,0x02e0,MAKEWPARAM(actual,actual),reinterpret_cast<LPARAM>(&original));
    // Exercise scrolling even when the runner has a large monitor.
    SetWindowPos(dialog,nullptr,0,0,original.right-original.left,300*actual/96,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    check_layout(dialog,sources);check_focus_navigation(dialog,sources);
    SendMessageW(dialog,0x02e0,MAKEWPARAM(actual,actual),reinterpret_cast<LPARAM>(&original));
    layoutDpi=0;
}
void CALLBACK probe(HWND,UINT,UINT_PTR,DWORD) {
    HWND dialog=thread_window(L"LISWorkbenchLogin");if(!dialog)return;
    check(!thread_window(L"LISWorkbenchMainWindow"),"business frame absent before login");
    if(phase>=6) {
        if(phase==6)return; // Startup must adopt the sole recommendation unaided.
        if(phase==7){
            if(SendMessageW(GetDlgItem(dialog,301),CB_GETCOUNT,0,0)!=2)return;
            check_dpi_layouts(dialog,true);
            check(SendMessageW(GetDlgItem(dialog,301),CB_GETCURSEL,0,0)==CB_ERR,"multiple sources require explicit selection");
            check(adoptCalls==1,"recommend mode never auto-submits");
            SendMessageW(GetDlgItem(dialog,301),CB_SETCURSEL,1,0);SendMessageW(dialog,WM_COMMAND,302,0);phase=8;return;
        }
        if(phase==9){SetWindowTextW(GetDlgItem(dialog,101),L"T123");SetWindowTextW(GetDlgItem(dialog,102),L"synthetic");phase=10;return;}
        if(phase==10){
            if(SendMessageW(GetDlgItem(dialog,301),CB_GETCOUNT,0,0)!=1)return;
            check(adoptCalls==2,"typing suppresses startup auto adoption");
            wchar_t code[64]{};GetWindowTextW(GetDlgItem(dialog,101),code,64);check(std::wstring(code)==L"T123","late recommendation does not overwrite manual input");
            SendMessageW(dialog,WM_COMMAND,3,0);phase=11;return;
        }
        return;
    }
    if(phase==0) {
        check(search::dpi_scale_factor(nullptr)>0,"pre-window DPI has nonzero fallback");
        RECT client{};GetClientRect(dialog,&client);
        check(client.right>=480&&client.bottom>=314,"login client area has useful minimum size");
        check_dpi_layouts(dialog,false);
        check(GetDlgItem(dialog,5)==nullptr,"connection settings removed");
        check(GetWindowTextLengthW(GetDlgItem(dialog,6))==0,"no initial prompt");
        SendMessageW(dialog,WM_COMMAND,3,0);
        check(calls==0,"empty login never submits to database");
        SetWindowTextW(GetDlgItem(dialog,101),L"T123");
        SetWindowTextW(GetDlgItem(dialog,102),L" \u3000 ");
        SendMessageW(dialog,WM_COMMAND,3,0);
        check(calls==0,"empty trimmed password never submits");
        SendMessageW(dialog,WM_COMMAND,4,0);phase=1;
    } else if(phase==1) {
        SetWindowTextW(GetDlgItem(dialog,101),L" T123 ");
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(101,EN_KILLFOCUS),0);
        phase=2;
    } else if(phase==2) {
        wchar_t name[64]{};GetWindowTextW(GetDlgItem(dialog,103),name,64);
        if(std::wstring(name)!=L"测试姓名")return;
        SetWindowTextW(GetDlgItem(dialog,101),L"T999");
        check(GetWindowTextLengthW(GetDlgItem(dialog,103))==0,"edited username clears old name immediately");
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(101,EN_KILLFOCUS),0);phase=3;
    } else if(phase==3) {
        SetWindowTextW(GetDlgItem(dialog,101),L"T123");changedAt=GetTickCount();phase=4;
    } else if(phase==4) {
        if(GetTickCount()-changedAt<150)return;
        check(GetWindowTextLengthW(GetDlgItem(dialog,103))==0,"late previous-account name cannot overwrite new input");
        SetWindowTextW(GetDlgItem(dialog,102),L" synthetic ");
        SendMessageW(dialog,WM_COMMAND,3,0);
        check(!IsWindowEnabled(GetDlgItem(dialog,3)),"duplicate submission disabled");
        phase=5;
    }
}
}
namespace login {
std::vector<Recommendation> recommendations(const search::DbSettings&,const std::wstring&,const std::function<bool()>& cancelled){
    Sleep(120);if(cancelled()||recMode==0)return {};
    Source s;s.employee_id=2;s.main_created=100;s.main_pid=10;s.main_hwnd=20;s.helper_pid=30;
    std::vector<Recommendation> r{{s,Identity{1,2,L"CURRENT",L"当前姓名"},true}};
    if(recMode==2){s.employee_id=3;s.main_pid=11;r.push_back({s,Identity{2,3,L"OTHER",L"另一姓名"}});}
    return r;
}
Result adopt_source(const search::DbSettings&,const std::wstring&,const Source& s,const std::function<bool()>& cancelled,bool,bool){
    ++adoptCalls;Sleep(40);if(cancelled())return {};
    Result r;r.status=Status::success;r.identity=Identity{s.employee_id==2?1:2,s.employee_id,s.employee_id==2?L"CURRENT":L"OTHER",s.employee_id==2?L"当前姓名":L"另一姓名"};r.identity->adopted_source=s;return r;
}

std::optional<std::wstring> lookup_name(const search::DbSettings&,std::wstring code,const std::function<bool()>& cancelled){
    if(code==L"T999"){Sleep(100);return L"旧账号姓名";}
    if(cancelled())return {};
    return code==L"T123"?std::optional<std::wstring>(L"测试姓名"):std::nullopt;
}
Result authenticate(const search::DbSettings&,std::wstring code,std::wstring password,const std::function<bool()>& cancelled){
    ++calls;check(code==L"T123"&&password==L"synthetic","UI normalizes credentials");
    if(cancelled())return {};
    Result r;r.status=Status::success;r.identity=Identity{1,2,L"T123",L"测试姓名"};return r;
}
}
int main(){
    search::DbSettings settings;
    auto timer=SetTimer(nullptr,0,30,probe);check(timer!=0,"UI probe timer available");
    auto first=login::show_dialog(GetModuleHandleW(nullptr),settings);
    check(!first&&phase==1,"cancel exits login without identity");
    auto second=login::show_dialog(GetModuleHandleW(nullptr),settings);
    check(second&&second->name==L"测试姓名"&&calls==1,"async successful login returns employee identity");
    phase=6;recMode=1;
    auto automatic=login::show_dialog(GetModuleHandleW(nullptr),settings,login::LoginMode::startup);
    check(automatic&&automatic->adopted_source&&automatic->name==L"当前姓名","startup automatically adopts sole verified source");
    phase=7;recMode=2;
    auto selected=login::show_dialog(GetModuleHandleW(nullptr),settings,login::LoginMode::recommend);
    check(selected&&selected->employee_id==3&&selected->adopted_source,"explicit choice adopts selected instance");
    phase=9;recMode=1;
    auto manual=login::show_dialog(GetModuleHandleW(nullptr),settings,login::LoginMode::startup);
    check(manual&&!manual->adopted_source&&manual->code==L"T123"&&calls==2,"manual authentication remains independent of source");
    KillTimer(nullptr,timer);
    std::cout<<"Login dialog gating, empty input, cancel and async success passed\n";
}
