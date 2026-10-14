#include "login_adoption.h"
#include "login_source_win32.h"
#include "login_database.h"
#include <cstdlib>
#include <iostream>
namespace {
int scans=0,queries=0,mode=0;
login::Source source(){login::Source s;s.main_pid=10;s.main_created=100;s.main_hwnd=20;s.helper_pid=30;s.helper_created=200;s.employee_id=40;s.port=4310;s.machine=L"PC";return s;}
void check(bool b,const char* s){if(!b){std::cerr<<s<<'\n';std::exit(1);}}
void reset(int m){scans=queries=0;mode=m;}
}
namespace login {
SourceScan scan_sources(const std::wstring&,const std::function<bool()>&){
    ++scans;auto s=source();SourceScan result{true,{{10,100,false}},{s}};
    if(mode==1&&scans>1)result.sources[0].employee_id=41;
    if(mode==2&&scans>1)result.sources[0].helper_created=201;
    if(mode==3)result.mains[0].locked=true;
    if(mode==4)result.sources[0].main_enabled=false;
    if(mode==6)result.mains.push_back({11,100,false});
    return result;
}
Result validate_source_account(const search::DbSettings&,const Source&,const std::function<bool()>&){++queries;Result r;r.status=Status::success;r.identity=Identity{1,40,L"CODE",L"NAME"};return r;}
}
int main(){
    search::DbSettings db;auto cancel=[]{return false;};
    reset(0);auto r=login::adopt_source(db,L"",source(),cancel);check(r.identity&&r.identity->adopted_source&&scans==2&&queries==1,"source verified before and after database query");
    reset(1);check(!login::adopt_source(db,L"",source(),cancel).identity,"identity change during validation cannot publish");
    reset(2);check(!login::adopt_source(db,L"",source(),cancel).identity,"helper replacement during validation cannot publish");
    reset(3);check(!login::adopt_source(db,L"",source(),cancel).identity&&queries==0,"locked source never queries an account for adoption");
    reset(4);check(!login::adopt_source(db,L"",source(),cancel).identity,"disabled source not initially adopted");
    reset(4);check(login::adopt_source(db,L"",source(),cancel,false).identity.has_value(),"existing same-user session may verify ordinary modal activity");
    reset(0);check(!login::adopt_source(db,L"",source(),[]{return true;}).identity&&queries==0,"cancelled adoption cannot publish");
    reset(6);auto multi=login::recommendations(db,L"",cancel);check(multi.size()==1&&!multi[0].automatic_allowed,"multiple running instances still require choice even if only one account validates");
    reset(6);check(!login::adopt_source(db,L"",source(),cancel,true,true).identity&&queries==0,"newly appearing second instance cancels automatic adoption");
    reset(1);check(login::recommendations(db,L"",cancel).empty(),"changed recommendation filtered before display");
    std::cout<<"Adoption snapshot and TOCTOU checks passed\n";
}
