#include "login_source_win32.h"
#ifdef _WIN32
#include "update_manifest.h"
#include <windows.h>
#include <tlhelp32.h>
#include <wbemidl.h>
#include <shellapi.h>
#include <map>
#include <mutex>
#include <array>
#include <algorithm>

namespace login {
namespace {
struct Handle {
    HANDLE value=nullptr;
    explicit Handle(HANDLE h=nullptr):value(h){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
std::uint64_t ticks(FILETIME f){return (std::uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;}
bool equal_path(const std::wstring& a,const std::wstring& b){return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL;}
std::wstring path_join(std::wstring dir,const wchar_t* file){while(!dir.empty()&&(dir.back()==L'\\'||dir.back()==L'/'))dir.pop_back();return dir+L"\\"+file;}
std::vector<unsigned char> token_user(HANDLE process){
    HANDLE raw=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&raw))return {};
    Handle token(raw);DWORD n=0;GetTokenInformation(raw,TokenUser,nullptr,0,&n);
    if(!n||n>65536)return {};
    std::vector<unsigned char> data(n);
    if(!GetTokenInformation(raw,TokenUser,data.data(),n,&n))return {};
    return data;
}
struct Process {DWORD pid=0,parent=0;std::uint64_t created=0;};
bool process_info(DWORD pid,DWORD parent,const std::wstring& expected,Process& out){
    Handle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));
    if(!h.value||WaitForSingleObject(h.value,0)!=WAIT_TIMEOUT)return false;
    std::array<wchar_t,32768> path{};DWORD count=DWORD(path.size());
    if(!QueryFullProcessImageNameW(h.value,0,path.data(),&count)||!equal_path(std::wstring(path.data(),count),expected))return false;
    DWORD ours=0,theirs=0;
    if(!ProcessIdToSessionId(GetCurrentProcessId(),&ours)||!ProcessIdToSessionId(pid,&theirs)||ours!=theirs)return false;
    auto a=token_user(GetCurrentProcess()),b=token_user(h.value);
    if(a.empty()||b.empty()||!EqualSid(reinterpret_cast<TOKEN_USER*>(a.data())->User.Sid,reinterpret_cast<TOKEN_USER*>(b.data())->User.Sid))return false;
    FILETIME created{},exit{},kernel{},user{};
    if(!GetProcessTimes(h.value,&created,&exit,&kernel,&user))return false;
    out={pid,parent,ticks(created)};return true;
}
bool trusted_file(const std::wstring& path,const char* expected){
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    struct Entry {std::uint64_t modified=0,size=0;bool trusted=false;};
    static std::mutex mutex;static std::map<std::wstring,Entry> cache;
    std::lock_guard<std::mutex> lock(mutex);
    auto& e=cache[path];const auto size=(std::uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
    if(e.modified!=ticks(info.ftLastWriteTime)||e.size!=size){
        std::string hash,error;e={ticks(info.ftLastWriteTime),size,false};
        e.trusted=lis_update::sha256_file_hex(path,hash,error)&&hash==expected;
    }
    return e.trusted;
}
// WMI is used only for one already-scoped helper PID. Never query the main
// program's command line, which may carry credentials in other launch modes.
class Commands {
    bool uninit=false;IWbemServices* service=nullptr;
public:
    Commands(){
        auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);uninit=SUCCEEDED(hr);
        if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)return;
        IWbemLocator* locator=nullptr;
        if(FAILED(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_IWbemLocator,reinterpret_cast<void**>(&locator))))return;
        BSTR ns=SysAllocString(L"ROOT\\CIMV2");
        hr=locator->ConnectServer(ns,nullptr,nullptr,nullptr,0,nullptr,nullptr,&service);
        SysFreeString(ns);locator->Release();
        if(FAILED(hr)){service=nullptr;return;}
        if(FAILED(CoSetProxyBlanket(service,RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE))){service->Release();service=nullptr;}
    }
    ~Commands(){if(service)service->Release();if(uninit)CoUninitialize();}
    bool read(DWORD pid,std::vector<std::wstring>& out){
        if(!service)return false;
        auto query=L"SELECT CommandLine FROM Win32_Process WHERE ProcessId="+std::to_wstring(pid);
        BSTR language=SysAllocString(L"WQL"),statement=SysAllocString(query.c_str());
        IEnumWbemClassObject* result=nullptr;
        auto hr=service->ExecQuery(language,statement,WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,&result);
        SysFreeString(language);SysFreeString(statement);
        if(FAILED(hr)||!result)return false;
        IWbemClassObject* row=nullptr;ULONG count=0;hr=result->Next(3000,1,&row,&count);result->Release();
        if(FAILED(hr)||count!=1||!row){if(row)row->Release();return false;}
        VARIANT value;VariantInit(&value);hr=row->Get(L"CommandLine",0,&value,nullptr,nullptr);row->Release();
        bool ok=false;
        if(SUCCEEDED(hr)&&value.vt==VT_BSTR&&value.bstrVal&&SysStringLen(value.bstrVal)<32768){
            int argc=0;auto argv=CommandLineToArgvW(value.bstrVal,&argc);
            if(argv){if(argc==7){for(int i=1;i<argc;++i)out.emplace_back(argv[i]);ok=true;}LocalFree(argv);}
        }
        VariantClear(&value);return ok;
    }
};
struct WindowProbe {DWORD pid=0;bool locked=false;};
BOOL CALLBACK labels(HWND child,LPARAM param){
    if(!IsWindowVisible(child))return TRUE;
    wchar_t cls[128]{};GetClassNameW(child,cls,128);std::wstring c=cls;
    if(c!=L"STATIC"&&c!=L"BUTTON"&&c.find(L"WindowsForms10.STATIC.")!=0&&c.find(L"WindowsForms10.BUTTON.")!=0)return TRUE;
    wchar_t text[1024]{};GetWindowTextW(child,text,1024);std::wstring s=text;
    if(s.find(L"系统已经自动锁屏")!=std::wstring::npos||(s.find(L"当前系统已被用户")!=std::wstring::npos&&s.find(L"锁定")!=std::wstring::npos))reinterpret_cast<WindowProbe*>(param)->locked=true;
    return TRUE;
}
BOOL CALLBACK windows(HWND window,LPARAM param){
    auto* p=reinterpret_cast<WindowProbe*>(param);DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid==p->pid&&IsWindowVisible(window))EnumChildWindows(window,labels,param);
    return TRUE;
}
}
SourceScan scan_sources(const std::wstring& directory,const std::function<bool()>& cancelled){
    SourceScan scan;auto stopped=[&]{return cancelled&&cancelled();};
    try {
        static std::mutex scanMutex;std::unique_lock<std::mutex> onlyScan(scanMutex,std::try_to_lock);
        if(!onlyScan.owns_lock()||stopped())return scan;
        if(!trusted_file(path_join(directory,L"Trasen.exe"),"9dd66c8dcd3cfaf4c4eea3bf858135dc1547e91ae4dfdfe1da097636655c4535")||
           !trusted_file(path_join(directory,L"TrasenMainWindow.dll"),"41eb00292f6a1944e6ff9299445c9e11c87d2191d8838d36d924e56a8a707faf")||
           !trusted_file(path_join(directory,L"TrasenFrame.dll"),"8cd17a7fb742405397102031f9e37d8ca06a96f2d643639d37ab4244692b3289")||
           !trusted_file(path_join(directory,L"tsmsgDtc.exe"),"2217c5be5bab6feb41275b172a730a449e51b1772791c9b23b09eb03cdb9f9c2"))return scan;
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));if(snapshot.value==INVALID_HANDLE_VALUE)return scan;
        std::vector<Process> mains,helpers;PROCESSENTRY32W item{};item.dwSize=sizeof(item);
        if(!Process32FirstW(snapshot.value,&item))return scan;
        do {
            if(stopped())return {};
            const bool main=_wcsicmp(item.szExeFile,L"Trasen.exe")==0;
            if(!main&&_wcsicmp(item.szExeFile,L"tsmsgDtc.exe")!=0)continue;
            Process p;
            if(process_info(item.th32ProcessID,item.th32ParentProcessID,path_join(directory,main?L"Trasen.exe":L"tsmsgDtc.exe"),p))(main?mains:helpers).push_back(p);
            else if(main)scan.unreadable_mains.push_back(item.th32ProcessID);
        }while(Process32NextW(snapshot.value,&item));
        if(GetLastError()!=ERROR_NO_MORE_FILES)return scan;
        for(const auto& m:mains){WindowProbe p{m.pid};EnumWindows(windows,reinterpret_cast<LPARAM>(&p));scan.mains.push_back({m.pid,m.created,p.locked});}
        if(helpers.empty()){scan.complete=true;return scan;}
        Commands commands;wchar_t machine[MAX_COMPUTERNAME_LENGTH+1]{};DWORD size=MAX_COMPUTERNAME_LENGTH+1;
        if(!GetComputerNameW(machine,&size))return scan;
        for(const auto& helper:helpers){
            if(stopped())return {};
            std::vector<std::wstring> args;Source s;
            if(!commands.read(helper.pid,args)||!parse_source_arguments(args,s))continue;
            auto main=std::find_if(mains.begin(),mains.end(),[&](const Process& m){return m.pid==s.main_pid&&m.pid==helper.parent&&m.created<helper.created;});
            if(main==mains.end())continue;
            HWND hwnd=reinterpret_cast<HWND>(std::uintptr_t(s.main_hwnd));DWORD owner=0;
            GetWindowThreadProcessId(hwnd,&owner);
            if(!IsWindow(hwnd)||!IsWindowVisible(hwnd)||owner!=main->pid||GetWindow(hwnd,GW_OWNER))continue;
            s.main_created=main->created;s.helper_pid=helper.pid;s.helper_created=helper.created;s.machine=machine;s.main_enabled=IsWindowEnabled(hwnd)!=FALSE;
            Process afterMain,afterHelper;
            if(!process_info(main->pid,main->parent,path_join(directory,L"Trasen.exe"),afterMain)||afterMain.created!=main->created||
               !process_info(helper.pid,helper.parent,path_join(directory,L"tsmsgDtc.exe"),afterHelper)||afterHelper.created!=helper.created)continue;
            scan.sources.push_back(std::move(s));
        }
        scan.complete=!stopped();return scan;
    }catch(...){return {};}
}
}
#endif
