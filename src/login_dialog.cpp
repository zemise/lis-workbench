#include "login_dialog.h"
#ifdef _WIN32
#include "login_database.h"
#include "search_ui_layout.h"
#include "window_task.h"
#include <optional>

namespace login {
namespace {
constexpr int Code=101,Password=102,Name=103,Submit=3,Cancel=4,Feedback=6;
constexpr int CodeLabel=201,NameLabel=202,PasswordLabel=203;
constexpr UINT_PTR NameTimer=1;
struct State {
    HINSTANCE instance;const search::DbSettings& db;
    std::optional<Identity> identity;app::WindowTask task;app::WindowTask nameTask;
    float scale=1.0f; HFONT ownFont=nullptr;
};
std::wstring text(HWND hwnd,int id) {
    HWND edit=GetDlgItem(hwnd,id);int n=GetWindowTextLengthW(edit);
    std::wstring result(std::size_t(n)+1,L'\0');GetWindowTextW(edit,result.data(),n+1);result.resize(n);return result;
}
void feedback(HWND hwnd,const wchar_t* value){SetWindowTextW(GetDlgItem(hwnd,Feedback),value);}
void busy(HWND hwnd,bool active) {
    for(int id:{Code,Password,Submit})EnableWindow(GetDlgItem(hwnd,id),!active);
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
    case WM_TIMER:if(wp==NameTimer){startNameLookup(hwnd,s);return 0;}break;
    case WM_COMMAND:
        if(LOWORD(wp)==Code) {
            if(HIWORD(wp)==EN_CHANGE) {
                s->nameTask.cancel();KillTimer(hwnd,NameTimer);
                SetWindowTextW(GetDlgItem(hwnd,Name),L"");feedback(hwnd,L"");
                if(valid_code(trim(text(hwnd,Code))))SetTimer(hwnd,NameTimer,400,nullptr);
            } else if(HIWORD(wp)==EN_KILLFOCUS)startNameLookup(hwnd,s);
            return 0;
        }
        switch(LOWORD(wp)) {
        case IDOK:case Submit: {
            if(s->task.active())return 0;
            auto code=trim(text(hwnd,Code));auto password=trim(text(hwnd,Password));
            if(!valid_code(code)){feedback(hwnd,L"请输入登录名；登录名不能包含单引号、分号或双连字符。");return 0;}
            if(password.empty()){feedback(hwnd,L"密码不能为空。");return 0;}
            KillTimer(hwnd,NameTimer);s->nameTask.cancel();
            auto db=s->db;
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
        s->task.cancel();s->nameTask.cancel();KillTimer(hwnd,NameTimer);
        // A sent close can be dispatched inside GetMessage while it still waits
        // for a posted message. Wake the modal loop without posting WM_QUIT.
        PostThreadMessageW(GetCurrentThreadId(),WM_NULL,0,0);
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
std::optional<Identity> show_dialog(HINSTANCE instance,const search::DbSettings& settings) {
    State state{instance,settings};
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);wc.lpszClassName=L"LISWorkbenchLogin";
    RegisterClassW(&wc);
    const auto scale=search::dpi_scale_factor(nullptr);
    const DWORD style=WS_CAPTION|WS_SYSMENU;
    RECT bounds{0,0,int(480*scale),int(314*scale)};
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
    bounds={0,0,int(480*state.scale),int(314*state.scale)};
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
            SendMessageW(window,WM_COMMAND,Submit,0);continue;
        }
        if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    state.task.cancel();state.nameTask.cancel();
    if(state.ownFont)DeleteObject(state.ownFont);
    return state.identity;
}
}
#endif
