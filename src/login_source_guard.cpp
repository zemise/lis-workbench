#include "login_source_guard.h"
#ifdef _WIN32
#include "login_source_win32.h"
#include "login_adoption.h"
#include "search_ui_layout.h"
#include "window_task.h"
#include <algorithm>
namespace login {
struct SourceGuard::Impl {
    HWND main=nullptr,monitor=nullptr,cover=nullptr,savedFocus=nullptr;
    Identity identity;search::DbSettings db;std::wstring directory;
    StateChanged changed;Leave leave;app::WindowTask task;
    HFONT font=nullptr;HHOOK hook=nullptr;bool suspended=false;ULONGLONG verified=0,startedAt=0;
    std::vector<app::window_task_detail::SuspendedTask> recovery;
    struct Saved {HWND window;bool enabled;};std::vector<Saved> hidden;
    static thread_local Impl* active;
    struct Probe {SourceMatch match;std::optional<Identity> identity;};
    Impl(HWND w,Identity i,search::DbSettings d,std::wstring path,StateChanged c,Leave l)
        :main(w),identity(std::move(i)),db(std::move(d)),directory(std::move(path)),changed(std::move(c)),leave(std::move(l)){}
    bool allowed(HWND window)const{return window==monitor||window==cover||GetAncestor(window,GA_ROOT)==cover;}
    void hideWindows(){
        EnumThreadWindows(GetCurrentThreadId(),[](HWND w,LPARAM param)->BOOL{
            auto* self=reinterpret_cast<Impl*>(param);
            if(!self->allowed(w)&&IsWindowVisible(w)){
                if(std::none_of(self->hidden.begin(),self->hidden.end(),[w](const Saved& s){return s.window==w;}))self->hidden.push_back({w,IsWindowEnabled(w)!=FALSE});
                EnableWindow(w,FALSE);ShowWindow(w,SW_HIDE);
            }
            return TRUE;
        },reinterpret_cast<LPARAM>(this));
    }
    static LRESULT CALLBACK filter(int code,WPARAM wp,LPARAM lp){
        if(code>=0&&wp==PM_REMOVE&&active&&active->suspended){
            auto* m=reinterpret_cast<MSG*>(lp);
            if(GetAncestor(m->hwnd,GA_ROOT)==active->cover&&m->message==WM_KEYDOWN){
                if(m->wParam==VK_TAB){SetFocus(GetNextDlgTabItem(active->cover,GetFocus(),GetKeyState(VK_SHIFT)<0));m->message=WM_NULL;}
                else if(m->wParam==VK_RETURN){int id=GetDlgCtrlID(GetFocus());if(id>=1&&id<=3){SendMessageW(active->cover,WM_COMMAND,id,0);m->message=WM_NULL;}}
                else if(m->wParam==VK_ESCAPE){active->leave(3);m->message=WM_NULL;}
            }
            if(!active->allowed(m->hwnd)&&m->hwnd){
                if(m->message==WM_COMMAND||m->message==WM_NOTIFY||m->message==WM_TIMER||
                   (m->message>=WM_KEYFIRST&&m->message<=WM_KEYLAST)||
                   (m->message>=WM_MOUSEFIRST&&m->message<=WM_MOUSELAST)||
                   m->message==WM_APP+61||m->message==WM_APP+0x531)m->message=WM_NULL;
            }
        }
        return CallNextHookEx(nullptr,code,wp,lp);
    }
    void pause(SourceStatus reason){
        if(!suspended){
            savedFocus=GetFocus();suspended=true;recovery=app::window_task_detail::suspendSessionTasks();
            changed(true,identity);hideWindows();ShowWindow(cover,SW_SHOW);SetForegroundWindow(cover);SetFocus(GetDlgItem(cover,1));
        }
        const wchar_t* text=reason==SourceStatus::locked?L"登录来源已锁定，解锁后将核实并自动恢复。":
            reason==SourceStatus::changed?L"来源用户已变化，请切换账号或手动登录。":
            reason==SourceStatus::exited?L"登录来源已退出，请重新选择来源或手动登录。":L"暂时无法核实登录来源，正在等待恢复。";
        SetWindowTextW(GetDlgItem(cover,10),text);
    }
    void resume(Identity refreshed){
        identity=std::move(refreshed);verified=GetTickCount64();
        if(!suspended)return;
        // The old task epoch remains invalid. Only newly started work may publish.
        suspended=false;ShowWindow(cover,SW_HIDE);
        for(const auto& w:hidden)if(IsWindow(w.window)){EnableWindow(w.window,w.enabled);ShowWindow(w.window,SW_SHOWNA);}
        hidden.clear();app::window_task_detail::discardSuspendedTasks();changed(false,identity);
        app::window_task_detail::recoverSuspendedTasks(std::move(recovery));SetForegroundWindow(main);
        if(IsWindow(savedFocus)&&IsWindowVisible(savedFocus)&&IsWindowEnabled(savedFocus)){SetForegroundWindow(GetAncestor(savedFocus,GA_ROOT));SetFocus(savedFocus);}
    }
    void poll(){
        if(suspended)hideWindows();
        if(task.active()){if(GetTickCount64()-startedAt>=10000){task.cancel();pause(SourceStatus::unavailable);}return;}
        if(!identity.adopted_source)return;
        const auto bound=*identity.adopted_source;const bool verify=suspended||GetTickCount64()-verified>=60000;
        auto settings=db;auto path=directory;
        startedAt=GetTickCount64();
        const bool started=task.start<Probe>(
            [settings,path,bound,verify](app::WindowTaskContext t){
                auto cancelled=[t]{return t.cancelled();};Probe p;
                p.match=match_source(scan_sources(path,cancelled),bound);
                if(p.match.status==SourceStatus::ready&&verify){
                    auto r=adopt_source(settings,path,*p.match.source,cancelled,false);
                    if(r.status==Status::success&&r.identity)p.identity=std::move(r.identity);
                    else p.match={};
                }
                return p;
            },[this,verify](std::optional<Probe> result,std::exception_ptr error){
                if(error||!result){pause(SourceStatus::unavailable);return;}
                if(result->match.status!=SourceStatus::ready){pause(result->match.status);return;}
                if(!suspended&&!same_observation(*result->match.source,*identity.adopted_source)){
                    pause(SourceStatus::unavailable);return;
                }
                if(verify){
                    if(!result->identity||result->identity->user_id!=identity.user_id||result->identity->employee_id!=identity.employee_id){pause(SourceStatus::changed);return;}
                    resume(std::move(*result->identity));
                }
            },true);
        if(!started)pause(SourceStatus::unavailable);
    }
    static LRESULT CALLBACK proc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,msg,wp,lp);
        if(msg==WM_TIMER){if(wp==1)self->poll();else if(self->suspended)self->hideWindows();return 0;}
        if(msg==WM_APP+1){self->poll();return 0;}
        if(msg==WM_COMMAND&&window==self->cover){int id=LOWORD(wp);if(id>=1&&id<=3)self->leave(id);return 0;}
        if(msg==WM_CLOSE&&window==self->cover){self->leave(3);return 0;}
        return DefWindowProcW(window,msg,wp,lp);
    }
    bool start(){
        if(!identity.adopted_source||active)return false;
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);wc.lpszClassName=L"LISWorkbenchSourceGuard";
        RegisterClassW(&wc);
        monitor=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,this);
        cover=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"会话已暂停 — LIS 工作台",WS_CAPTION|WS_SYSMENU,0,0,1,1,nullptr,nullptr,wc.hInstance,this);
        if(!monitor||!cover)return false;
        const auto scale=search::dpi_scale_factor(cover);auto px=[scale](int v){return int(v*scale+0.5f);};
        RECT r{0,0,px(520),px(188)};
        using AdjustForDpi=BOOL(WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT);
        auto adjust=reinterpret_cast<AdjustForDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"AdjustWindowRectExForDpi"));
        if(adjust)adjust(&r,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_TOOLWINDOW|WS_EX_TOPMOST,UINT(96*scale));
        else AdjustWindowRectEx(&r,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_TOOLWINDOW|WS_EX_TOPMOST);
        font=CreateFontW(-MulDiv(11,int(96*scale),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        SetWindowPos(cover,HWND_TOPMOST,work.left+(work.right-work.left-r.right+r.left)/2,work.top+(work.bottom-work.top-r.bottom+r.top)/2,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE);
        auto control=[&](const wchar_t* cls,const wchar_t* text,int id,int x,int y,int width,int height){
            auto child=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|(id<4?WS_TABSTOP:0),px(x),px(y),px(width),px(height),cover,reinterpret_cast<HMENU>(INT_PTR(id)),wc.hInstance,nullptr);
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font?font:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        };
        control(L"STATIC",L"",10,24,28,472,70);
        control(L"BUTTON",L"切换账号",1,24,124,144,36);control(L"BUTTON",L"手动登录",2,188,124,144,36);control(L"BUTTON",L"退出程序",3,352,124,144,36);
        hook=SetWindowsHookExW(WH_GETMESSAGE,filter,nullptr,GetCurrentThreadId());if(!hook)return false;
        active=this;verified=GetTickCount64();SetTimer(monitor,1,2000,nullptr);SetTimer(monitor,2,250,nullptr);PostMessageW(monitor,WM_APP+1,0,0);return true;
    }
    ~Impl(){task.cancel();if(suspended)app::window_task_detail::discardSuspendedTasks();if(hook)UnhookWindowsHookEx(hook);if(active==this)active=nullptr;if(monitor)DestroyWindow(monitor);if(cover)DestroyWindow(cover);if(font)DeleteObject(font);}
};
thread_local SourceGuard::Impl* SourceGuard::Impl::active=nullptr;
SourceGuard::SourceGuard(HWND w,Identity i,search::DbSettings d,std::wstring p,StateChanged c,Leave l)
    :impl_(std::make_unique<Impl>(w,std::move(i),std::move(d),std::move(p),std::move(c),std::move(l))){}
SourceGuard::~SourceGuard()=default;
bool SourceGuard::start(){return impl_->start();}
bool SourceGuard::paused()const{return impl_->suspended;}
}
#endif
