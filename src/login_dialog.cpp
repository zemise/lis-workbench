#include "login_dialog.h"
#ifdef _WIN32
#include "login_database.h"
#include "login_adoption.h"
#include "search_ui_layout.h"
#include "window_task.h"
#include <optional>

namespace login {
namespace {
constexpr int Code=101,Password=102,Name=103,Submit=3,Cancel=4,Feedback=6;
constexpr int CodeLabel=201,NameLabel=202,PasswordLabel=203;
constexpr UINT_PTR NameTimer=1,SourceTimeout=2;
constexpr int Sources=301,UseSource=302,RefreshSource=303,SourceLabel=304;
struct State {
    HINSTANCE instance;const search::DbSettings& db;
    LoginMode mode;std::wstring directory;
    std::vector<Recommendation> recommendations;app::WindowTask sourceTask;
    bool manualTouched=false,adopting=false;
    std::optional<Identity> identity;app::WindowTask task;app::WindowTask nameTask;
    float scale=1.0f; HFONT ownFont=nullptr;
};
std::wstring text(HWND hwnd,int id) {
    HWND edit=GetDlgItem(hwnd,id);int n=GetWindowTextLengthW(edit);
    std::wstring result(std::size_t(n)+1,L'\0');GetWindowTextW(edit,result.data(),n+1);result.resize(n);return result;
}
void feedback(HWND hwnd,const wchar_t* value){SetWindowTextW(GetDlgItem(hwnd,Feedback),value);}
void busy(HWND hwnd,bool active) {
    for(int id:{Code,Password,Submit,Sources,RefreshSource})EnableWindow(GetDlgItem(hwnd,id),!active);
    auto* s=reinterpret_cast<State*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    EnableWindow(GetDlgItem(hwnd,UseSource),!active&&s&&!s->recommendations.empty());
}
void layout(HWND hwnd, State* s) {
    const auto px=[s](int n){return int(n*s->scale+0.5f);};
    RECT client{};GetClientRect(hwnd,&client);
    const int x=px(120),width=client.right-x-px(40);
    const auto move=[&](int id,int left,int top,int w,int h){MoveWindow(GetDlgItem(hwnd,id),left,px(top),w,px(h),TRUE);};
    move(CodeLabel,px(40),42,px(72),26); move(Code,x,34,width,36);
    move(NameLabel,px(40),98,px(72),26); move(Name,x,98,width,26);
    move(PasswordLabel,px(40),154,px(72),26); move(Password,x,146,width,36);
    move(Feedback,px(40),198,client.right-px(80),40);
    move(Submit,client.right-px(264),250,px(104),36);
    move(Cancel,client.right-px(144),250,px(104),36);
    if(s->mode!=LoginMode::manual){
        move(SourceLabel,px(40),302,client.right-px(80),24);
        move(Sources,px(40),332,client.right-px(80),120);
        move(UseSource,px(40),378,px(156),36);
        move(RefreshSource,px(212),378,px(120),36);
    }
}
void applyFont(HWND hwnd,State* s) {
    HFONT font=CreateFontW(-MulDiv(11,int(96*s->scale),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    if(!font)return;
    EnumChildWindows(hwnd,[](HWND child,LPARAM param)->BOOL{
        SendMessageW(child,WM_SETFONT,WPARAM(param),TRUE);return TRUE;
    },reinterpret_cast<LPARAM>(font));
    if(s->ownFont)DeleteObject(s->ownFont);s->ownFont=font;
}
void startNameLookup(HWND hwnd,State* s) {
    KillTimer(hwnd,NameTimer);
    if(s->task.active() || !IsWindowEnabled(GetDlgItem(hwnd,Code)))return;
    const auto code=trim(text(hwnd,Code));
    if(!valid_code(code))return;
    const auto db=s->db;
    s->nameTask.start<std::optional<std::wstring>>(
        [db,code](app::WindowTaskContext task){return lookup_name(db,code,[task]{return task.cancelled();});},
        [hwnd,code](std::optional<std::optional<std::wstring>> result,std::exception_ptr error){
            if(IsWindow(hwnd)&&trim(text(hwnd,Code))==code&&!error&&result&&*result)
                SetWindowTextW(GetDlgItem(hwnd,Name),(**result).c_str());
        });
}
void useSource(HWND hwnd,State* s,bool automatic=false) {
    if(s->task.active())return;
    const auto index=SendMessageW(GetDlgItem(hwnd,Sources),CB_GETCURSEL,0,0);
    if(index==CB_ERR||std::size_t(index)>=s->recommendations.size())return;
    const auto source=s->recommendations[std::size_t(index)].source;
    auto db=s->db;auto directory=s->directory;
    s->sourceTask.cancel();s->nameTask.cancel();KillTimer(hwnd,NameTimer);
    s->adopting=true;busy(hwnd,true);feedback(hwnd,L"正在核实所选用户…");SetTimer(hwnd,SourceTimeout,10000,nullptr);
    const bool started=s->task.start<Result>(
        [db,directory,source,automatic](app::WindowTaskContext t){return adopt_source(db,directory,source,[t]{return t.cancelled();},true,automatic);},
        [hwnd,s](std::optional<Result> result,std::exception_ptr error){
            KillTimer(hwnd,SourceTimeout);s->adopting=false;
            if(!error&&result&&result->status==Status::success&&result->identity){s->identity=std::move(result->identity);DestroyWindow(hwnd);return;}
            busy(hwnd,false);feedback(hwnd,L"当前来源无法核实，请刷新或手动登录。");
        });
    if(!started){s->adopting=false;KillTimer(hwnd,SourceTimeout);busy(hwnd,false);feedback(hwnd,L"无法开始核实，请手动登录。");}
}
void refreshSources(HWND hwnd,State* s) {
    if(s->task.active()||s->sourceTask.active()||s->mode==LoginMode::manual)return;
    s->recommendations.clear();SendMessageW(GetDlgItem(hwnd,Sources),CB_RESETCONTENT,0,0);
    EnableWindow(GetDlgItem(hwnd,UseSource),FALSE);EnableWindow(GetDlgItem(hwnd,RefreshSource),FALSE);
    SetWindowTextW(GetDlgItem(hwnd,SourceLabel),L"正在识别已登录用户…");
    auto db=s->db;auto directory=s->directory;SetTimer(hwnd,SourceTimeout,10000,nullptr);
    const bool started=s->sourceTask.start<std::vector<Recommendation>>(
        [db,directory](app::WindowTaskContext t){return recommendations(db,directory,[t]{return t.cancelled();});},
        [hwnd,s](std::optional<std::vector<Recommendation>> result,std::exception_ptr error){
            KillTimer(hwnd,SourceTimeout);EnableWindow(GetDlgItem(hwnd,RefreshSource),TRUE);
            if(!error&&result)s->recommendations=std::move(*result);
            for(const auto& r:s->recommendations){
                ULARGE_INTEGER bits{};bits.QuadPart=r.source.main_created;FILETIME utc{bits.LowPart,bits.HighPart},local{};SYSTEMTIME time{};
                FileTimeToLocalFileTime(&utc,&local);FileTimeToSystemTime(&local,&time);
                wchar_t started[24]{};swprintf(started,24,L"%02u:%02u:%02u",time.wHour,time.wMinute,time.wSecond);
                const auto label=r.identity.name+L"（"+r.identity.code+L"）  启动于 "+started;
                SendMessageW(GetDlgItem(hwnd,Sources),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            }
            SetWindowTextW(GetDlgItem(hwnd,SourceLabel),s->recommendations.empty()?L"未发现可采用的登录，可刷新或手动登录":L"当前已登录用户（可选择或手动输入）");
            if(s->recommendations.size()==1)SendMessageW(GetDlgItem(hwnd,Sources),CB_SETCURSEL,0,0);
            EnableWindow(GetDlgItem(hwnd,UseSource),!s->recommendations.empty());
            if(s->mode==LoginMode::startup&&!s->manualTouched&&s->recommendations.size()==1&&s->recommendations[0].automatic_allowed)useSource(hwnd,s,true);
        });
    if(!started){KillTimer(hwnd,SourceTimeout);EnableWindow(GetDlgItem(hwnd,RefreshSource),TRUE);SetWindowTextW(GetDlgItem(hwnd,SourceLabel),L"无法识别来源，请手动登录");}
}
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* s=reinterpret_cast<State*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){s=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
    switch(msg) {
    case WM_CREATE: {
        s->scale=search::dpi_scale_factor(hwnd);
        const auto scale=s->scale;
        auto control=[&](const wchar_t* cls,const wchar_t* label,DWORD style,int id,int x,int y,int w,int h) {
            auto child=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,label,
                WS_CHILD|WS_VISIBLE|style,int(x*scale),int(y*scale),int(w*scale),int(h*scale),hwnd,reinterpret_cast<HMENU>(INT_PTR(id)),s->instance,nullptr);
            return child;
        };
        control(L"STATIC",L"账号",0,CodeLabel,0,0,1,1);
        control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,Code,0,0,1,1);
        control(L"STATIC",L"用户名",0,NameLabel,0,0,1,1);
        control(L"STATIC",L"",SS_NOPREFIX,Name,0,0,1,1);
        control(L"STATIC",L"密码",0,PasswordLabel,0,0,1,1);
        control(L"EDIT",L"",WS_TABSTOP|ES_PASSWORD|ES_AUTOHSCROLL,Password,0,0,1,1);
        SendMessageW(GetDlgItem(hwnd,Code),EM_SETLIMITTEXT,50,0);
        SendMessageW(GetDlgItem(hwnd,Password),EM_SETLIMITTEXT,512,0);
        control(L"STATIC",L"",0,Feedback,0,0,1,1);
        control(L"BUTTON",L"登录",WS_TABSTOP|BS_DEFPUSHBUTTON,Submit,0,0,1,1);
        control(L"BUTTON",L"取消",WS_TABSTOP,Cancel,0,0,1,1);
        if(s->mode!=LoginMode::manual){
            control(L"STATIC",L"",0,SourceLabel,0,0,1,1);
            control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,Sources,0,0,1,1);
            control(L"BUTTON",L"使用此用户",WS_TABSTOP,UseSource,0,0,1,1);
            control(L"BUTTON",L"刷新推荐",WS_TABSTOP,RefreshSource,0,0,1,1);
            EnableWindow(GetDlgItem(hwnd,UseSource),FALSE);
            PostMessageW(hwnd,WM_APP+1,0,0);
        }
        applyFont(hwnd,s);layout(hwnd,s);
        SetFocus(GetDlgItem(hwnd,Code));return 0;
    }
    case WM_SIZE:layout(hwnd,s);return 0;
    case 0x02e0: { // WM_DPICHANGED; available at runtime on newer Windows.
        s->scale=LOWORD(wp)/96.0f;
        auto* bounds=reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER|SWP_NOACTIVATE);
        applyFont(hwnd,s);layout(hwnd,s);return 0;
    }
    case WM_APP+1:refreshSources(hwnd,s);return 0;
    case WM_TIMER:
        if(wp==NameTimer){startNameLookup(hwnd,s);return 0;}
        if(wp==SourceTimeout){
            KillTimer(hwnd,SourceTimeout);s->sourceTask.cancel();
            if(s->adopting){s->task.cancel();s->adopting=false;busy(hwnd,false);feedback(hwnd,L"核实超时，请刷新或手动登录。");}
            EnableWindow(GetDlgItem(hwnd,RefreshSource),TRUE);SetWindowTextW(GetDlgItem(hwnd,SourceLabel),L"来源识别超时，可刷新或手动登录");return 0;
        }break;
    case WM_COMMAND:
        if((LOWORD(wp)==Code||LOWORD(wp)==Password)&&HIWORD(wp)==EN_CHANGE)s->manualTouched=true;
        if(LOWORD(wp)==Code) {
            if(HIWORD(wp)==EN_CHANGE) {
                s->nameTask.cancel();KillTimer(hwnd,NameTimer);
                SetWindowTextW(GetDlgItem(hwnd,Name),L"");feedback(hwnd,L"");
                if(valid_code(trim(text(hwnd,Code))))SetTimer(hwnd,NameTimer,400,nullptr);
            } else if(HIWORD(wp)==EN_KILLFOCUS)startNameLookup(hwnd,s);
            return 0;
        }
        switch(LOWORD(wp)) {
        case UseSource:useSource(hwnd,s);return 0;
        case RefreshSource:refreshSources(hwnd,s);return 0;
        case IDOK:case Submit: {
            if(s->task.active())return 0;
            auto code=trim(text(hwnd,Code));auto password=trim(text(hwnd,Password));
            if(!valid_code(code)){feedback(hwnd,L"请输入登录名；登录名不能包含单引号、分号或双连字符。");return 0;}
            if(password.empty()){feedback(hwnd,L"密码不能为空。");return 0;}
            KillTimer(hwnd,NameTimer);s->nameTask.cancel();s->sourceTask.cancel();KillTimer(hwnd,SourceTimeout);
            s->manualTouched=true;auto db=s->db;
            busy(hwnd,true);feedback(hwnd,L"正在验证…");
            const bool started=s->task.start<Result>(
                [db,code,password=std::move(password)](app::WindowTaskContext task) mutable {
                    auto result=authenticate(db,code,std::move(password),[task]{return task.cancelled();});
                    if(!db.password.empty())SecureZeroMemory(db.password.data(),db.password.size()*sizeof(wchar_t));
                    return result;
                },[hwnd,s](std::optional<Result> result,std::exception_ptr error) {
                    if(!IsWindow(hwnd))return;
                    if(!error && result && result->status==Status::success && result->identity) {
                        s->identity=std::move(result->identity);DestroyWindow(hwnd);return;
                    }
                    busy(hwnd,false);SetWindowTextW(GetDlgItem(hwnd,Password),L"");
                    feedback(hwnd,message(error||!result?Status::failed:result->status));SetFocus(GetDlgItem(hwnd,Password));
                });
            if(!started){busy(hwnd,false);feedback(hwnd,message(Status::failed));}
            return 0;
        }
        case IDCANCEL:case Cancel:DestroyWindow(hwnd);return 0;
        }break;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:
        s->task.cancel();s->nameTask.cancel();s->sourceTask.cancel();KillTimer(hwnd,NameTimer);KillTimer(hwnd,SourceTimeout);
        // A sent close can be dispatched inside GetMessage while it still waits
        // for a posted message. Wake the modal loop without posting WM_QUIT.
        PostThreadMessageW(GetCurrentThreadId(),WM_NULL,0,0);
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
std::optional<Identity> show_dialog(HINSTANCE instance,const search::DbSettings& settings,LoginMode mode,const std::wstring& directory) {
    State state{instance,settings,mode,directory};
    const int clientHeight=mode==LoginMode::manual?314:442;
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);wc.lpszClassName=L"LISWorkbenchLogin";
    RegisterClassW(&wc);
    const auto scale=search::dpi_scale_factor(nullptr);
    const DWORD style=WS_CAPTION|WS_SYSMENU;
    RECT bounds{0,0,int(480*scale),int(clientHeight*scale)};
    AdjustWindowRectEx(&bounds,style,FALSE,WS_EX_DLGMODALFRAME);
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"登录 — LIS 工作台",
        style,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,
        width,height,nullptr,nullptr,instance,&state);
    if(!window)return {};
    // The screen DC can report 96 DPI even when this window's monitor uses 192.
    // Size the client area using the actual window DPI before making it visible.
    state.scale=search::dpi_scale_factor(window);
    bounds={0,0,int(480*state.scale),int(clientHeight*state.scale)};
    using AdjustForDpi=BOOL(WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT);
    auto adjust=reinterpret_cast<AdjustForDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"AdjustWindowRectExForDpi"));
    if(adjust)adjust(&bounds,style,FALSE,WS_EX_DLGMODALFRAME,UINT(96*state.scale));
    else AdjustWindowRectEx(&bounds,style,FALSE,WS_EX_DLGMODALFRAME);
    const int actualWidth=bounds.right-bounds.left,actualHeight=bounds.bottom-bounds.top;
    SetWindowPos(window,nullptr,work.left+(work.right-work.left-actualWidth)/2,
        work.top+(work.bottom-work.top-actualHeight)/2,actualWidth,actualHeight,SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(window,SW_SHOW);SetFocus(GetDlgItem(window,Code));
    MSG msg{};
    while(IsWindow(window) && GetMessageW(&msg,nullptr,0,0)>0) {
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_RETURN && GetAncestor(msg.hwnd,GA_ROOT)==window) {
            const int focused=GetDlgCtrlID(GetFocus());
            SendMessageW(window,WM_COMMAND,(focused==UseSource||focused==RefreshSource)?focused:Submit,0);continue;
        }
        if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    if(IsWindow(window))DestroyWindow(window);
    state.task.cancel();state.nameTask.cancel();state.sourceTask.cancel();
    if(state.ownFont)DeleteObject(state.ownFont);
    return state.identity;
}
}
#endif
