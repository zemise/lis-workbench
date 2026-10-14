#include "login_source.h"
#include <cstdlib>
#include <iostream>
namespace {void check(bool value,const char* msg){if(!value){std::cerr<<msg<<'\n';std::exit(1);}}}
int main(){
    login::Source s;
    std::vector<std::wstring> args{L"/9",L"/20",L"/200",L"/80000",L"/5000",L"/100"};
    check(login::parse_source_arguments(args,s)&&s.employee_id==200&&s.main_pid==100,"synthetic six numeric arguments parse");
    for(auto bad:{L"/0",L"/-1",L"/65536",L"/5000 extra",L"/5000;"}){auto a=args;a[4]=bad;check(!login::parse_source_arguments(a,s),"invalid port rejected");}
    for(auto bad:{L"",L"/",L"/+200",L"/2147483648",L"/999999999999999999999",L"/200 abc"}){auto a=args;a[2]=bad;check(!login::parse_source_arguments(a,s),"malformed employee rejected");}
    auto zero=args;zero[2]=L"/0";check(login::parse_source_arguments(zero,s),"employee identity is a database key, not an invented positive-only policy");
    auto shortArgs=args;shortArgs.pop_back();check(!login::parse_source_arguments(shortArgs,s),"missing argument rejected");
    auto a=args;a[3]=L"/-2147483648";check(login::parse_source_arguments(a,s)&&s.main_hwnd==2147483648ULL,"legacy signed 32-bit HWND preserved");
    login::parse_source_arguments(args,s);s.main_created=100;s.helper_created=200;s.helper_pid=10;s.machine=L"TESTPC";
    login::SourceScan scan{true,{{100,100,false}},{s}};
    check(login::match_source(scan,s).status==login::SourceStatus::ready,"matching source ready");
    scan.mains[0].locked=true;check(login::match_source(scan,s).status==login::SourceStatus::locked,"lock takes precedence over stale live helper");
    scan.mains[0].locked=false;scan.sources[0].helper_pid=11;scan.sources[0].helper_created=300;
    check(login::match_source(scan,s).status==login::SourceStatus::ready&&!login::same_observation(scan.sources[0],s),"same-account unlock permits new helper but requires revalidation");
    scan.sources[0].employee_id=99;check(login::match_source(scan,s).status==login::SourceStatus::changed,"new source employee never silently resumes old identity");
    scan.sources={s,s};check(login::match_source(scan,s).status==login::SourceStatus::unavailable,"ambiguous helpers not selected");
    scan.sources.clear();check(login::match_source(scan,s).status==login::SourceStatus::unavailable,"missing helper suspends");
    scan.mains[0].created=101;check(login::match_source(scan,s).status==login::SourceStatus::exited,"PID reuse cannot resume old process");
    scan.mains.clear();scan.unreadable_mains={100};check(login::match_source(scan,s).status==login::SourceStatus::unavailable,"unreadable existing process is not reported as exited");
    scan.complete=false;check(login::match_source(scan,s).status==login::SourceStatus::unavailable,"incomplete observation fails closed");
    std::cout<<"Source arguments and binding lifecycle passed\n";
}
