#include "login_source.h"
#include <limits>
namespace login {
namespace {
bool decimal(const std::wstring& s, std::int64_t& out, bool signed_handle = false) {
    if(s.size()<2 || s[0]!=L'/')return false;
    std::size_t i=1;bool negative=false;
    if(signed_handle && s[i]==L'-'){negative=true;++i;}
    if(i==s.size())return false;
    std::int64_t n=0;
    for(;i<s.size();++i){
        if(s[i]<L'0'||s[i]>L'9'||n>(4294967295LL-(s[i]-L'0'))/10)return false;
        n=n*10+(s[i]-L'0');
    }
    if(negative && n>2147483648LL)return false;
    out=negative?-n:n;return true;
}
}
bool parse_source_arguments(const std::vector<std::wstring>& args, Source& out) {
    if(args.size()!=6)return false;
    std::int64_t n[6]{};
    for(int i=0;i<6;++i)if(!decimal(args[i],n[i],i<4))return false;
    if(n[0]>2147483647||n[1]>2147483647||n[2]>2147483647||n[3]==0||n[4]<1||n[4]>65535||n[5]<1||n[5]>2147483647)return false;
    Source s;s.system_id=int(n[0]);s.dept_id=int(n[1]);s.employee_id=n[2];
    s.main_hwnd=std::uint32_t(n[3]);s.port=int(n[4]);s.main_pid=std::uint32_t(n[5]);
    out=std::move(s);return true;
}
bool same_instance(const Source& a,const Source& b) {
    return a.main_pid==b.main_pid && a.main_created==b.main_created && a.main_hwnd==b.main_hwnd;
}
bool same_observation(const Source& a,const Source& b) {
    return same_instance(a,b)&&a.employee_id==b.employee_id&&a.helper_pid==b.helper_pid&&
        a.helper_created==b.helper_created&&a.port==b.port&&a.system_id==b.system_id&&
        a.dept_id==b.dept_id&&a.machine==b.machine;
}
SourceMatch match_source(const SourceScan& scan,const Source& bound) {
    if(!scan.complete)return {};
    const SourceMain* main=nullptr;
    for(const auto& m:scan.mains)if(m.pid==bound.main_pid&&m.created==bound.main_created)main=&m;
    if(!main){
        for(auto pid:scan.unreadable_mains)if(pid==bound.main_pid)return {};
        return {SourceStatus::exited,{}};
    }
    if(main->locked)return {SourceStatus::locked,{}};
    const Source* found=nullptr;
    for(const auto& s:scan.sources)if(s.main_pid==bound.main_pid&&s.main_created==bound.main_created){
        if(found)return {}; // Never choose between stale or ambiguous helpers.
        found=&s;
    }
    if(!found)return {};
    if(!same_instance(*found,bound)||found->employee_id!=bound.employee_id)return {SourceStatus::changed,{}};
    return {SourceStatus::ready,*found};
}
}
