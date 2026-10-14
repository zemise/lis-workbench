#include "login_source_guard.h"
#include "login_adoption.h"
#include "login_source_win32.h"
#include "window_task.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
namespace {
std::atomic<int> mode{0};int business=0,leaving=0;
void check(bool b,const char* s){if(!b){std::cerr<<s<<'\n';std::exit(1);}}
login::Source source(){login::Source s;s.main_pid=1;s.main_created=100;s.main_hwnd=2;s.helper_pid=mode==2?11:10;s.helper_created=mode==2?300:200;s.employee_id=mode==3?41:40;s.port=4310;s.machine=L"PC";return s;}
LRESULT CALLBACK businessProc(HWND w,UINT m,WPARAM p,LPARAM l){if(m==WM_COMMAND||m==WM_TIMER)++business;return DefWindowProcW(w,m,p,l);}
HWND threadWindow(bool messageOnly){
 HWND parent=messageOnly?HWND_MESSAGE:nullptr;HWND after=nullptr;
 while((after=FindWindowExW(parent,after,L"LISWorkbenchSourceGuard",nullptr))){
  DWORD pid=0;if(GetWindowThreadProcessId(after,&pid)==GetCurrentThreadId()&&pid==GetCurrentProcessId())return after;
 }
 return nullptr;
}
template<class Predicate> bool pump(Predicate done){
 DWORD start=GetTickCount();while(GetTickCount()-start<6000){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}if(done())return true;Sleep(5);}return false;
}
void probe(){PostMessageW(threadWindow(true),WM_APP+1,0,0);}
}
namespace login {
SourceScan scan_sources(const std::wstring&,const std::function<bool()>&){
 if(mode==4)return {true,{},{}};
 return {true,{{1,mode==6?101ULL:100ULL,mode==1}},{source()}};
}
Result adopt_source(const search::DbSettings&,const std::wstring&,const Source& s,const std::function<bool()>& cancelled,bool,bool){
 if(cancelled()||mode==5)return {};
 Result r;r.status=Status::success;r.identity=Identity{1,40,L"CODE",L"NAME"};r.identity->adopted_source=s;return r;
}
}
int main(){
 WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=businessProc;c.lpszClassName=L"OwnedSourceGuardBusinessTest";RegisterClassW(&c);
 HWND main=CreateWindowW(c.lpszClassName,L"Synthetic main",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,400,300,nullptr,nullptr,c.hInstance,nullptr);
 HWND detached=CreateWindowW(c.lpszClassName,L"Synthetic editor",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,300,200,nullptr,nullptr,c.hInstance,nullptr);
 check(main&&detached,"synthetic business windows created");
 login::Identity identity{1,40,L"CODE",L"NAME"};identity.adopted_source=source();search::DbSettings db;
 bool paused=false;
 {
  login::SourceGuard guard(main,identity,db,L"synthetic",[&](bool p,const login::Identity& i){paused=p;check(i.user_id==1,"guard never follows another identity");},[&](int a){leaving=a;});
  check(guard.start(),"guard starts");
  mode=1;probe();check(pump([&]{return paused;}),"lock pauses");
  check(!IsWindowVisible(main)&&!IsWindowEnabled(main)&&!IsWindowVisible(detached),"all business windows hidden and disabled");
  app::WindowTask blocked;check(!blocked.start<int>([]{return 1;},[](std::optional<int>,std::exception_ptr){}),"paused business cannot launch new backend work");
  const int before=business;PostMessageW(main,WM_COMMAND,42,0);PostMessageW(main,WM_TIMER,44,0);
  check(pump([&]{return true;}),"pump blocked commands");check(business==before,"pause filters business commands and timers");
  mode=2;probe();check(pump([&]{return !paused;}),"same identity with replaced helper automatically resumes");
  check(IsWindowVisible(main)&&IsWindowEnabled(main)&&IsWindowVisible(detached),"same-user pages preserved and restored");
  mode=3;probe();check(pump([&]{return paused;}),"changed employee pauses instead of automatically switching");
  mode=6;probe();check(pump([&]{return true;}),"probe restarted process");check(paused,"new process never restores old binding");
  auto cover=threadWindow(false);check(cover&&IsWindowVisible(cover),"pause offers independent login controls");
  SendMessageW(cover,WM_COMMAND,2,0);check(leaving==2,"manual takeover offered while paused");
 }
 DestroyWindow(main);DestroyWindow(detached);
 std::cout<<"Source guard privacy, input gating, same-user resume and manual takeover passed\n";
}
