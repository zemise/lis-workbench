// An ODBC double: runs the production authentication path without a server.
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include "login_database.h"
#include "login_legacy_vectors.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>

namespace {
void check(bool ok,const char* msg){if(!ok){std::cerr<<msg<<'\n';std::exit(1);}}
struct Scenario {
    std::wstring config=L"60",stored=legacy_vectors[3].stored,name=L"测试人员";
    bool config_null=false,employee_missing=false,locked=false,expired=false,query_error=false,date_null=false;
    int config_rows=1,accounts=1,errors=0,elapsed=0;
    bool online=true;
} scenario;
int phase=0,cursor=0;bool readonly=false;
std::map<int,SQLPOINTER> bound;
void reset(){scenario=Scenario{};phase=0;cursor=0;readonly=false;bound.clear();}
login::Result run(const std::wstring& password=L"Abc123!") {
    search::DbSettings settings{L"fake",L"fake",L"fake",L"fake"};
    return login::authenticate(settings,L" t123 ",password,[]{return false;});
}
}
extern "C" {
SQLRETURN SQL_API SQLAllocHandle(SQLSMALLINT type,SQLHANDLE,SQLHANDLE* out){*out=reinterpret_cast<SQLHANDLE>(INT_PTR(type));return SQL_SUCCESS;}
SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT,SQLHANDLE){return SQL_SUCCESS;}
SQLRETURN SQL_API SQLSetEnvAttr(SQLHENV,SQLINTEGER,SQLPOINTER,SQLINTEGER){return SQL_SUCCESS;}
SQLRETURN SQL_API SQLSetConnectAttr(SQLHDBC,SQLINTEGER attr,SQLPOINTER value,SQLINTEGER){if(attr==SQL_ATTR_ACCESS_MODE)readonly=INT_PTR(value)==SQL_MODE_READ_ONLY;return SQL_SUCCESS;}
SQLRETURN SQL_API SQLDriverConnectW(SQLHDBC,SQLHWND,SQLWCHAR*,SQLSMALLINT,SQLWCHAR*,SQLSMALLINT,SQLSMALLINT*,SQLUSMALLINT){check(readonly,"connection read-only before connecting");return SQL_SUCCESS;}
SQLRETURN SQL_API SQLDisconnect(SQLHDBC){return SQL_SUCCESS;}
SQLRETURN SQL_API SQLSetStmtAttr(SQLHSTMT,SQLINTEGER,SQLPOINTER,SQLINTEGER){return SQL_SUCCESS;}
SQLRETURN SQL_API SQLFreeStmt(SQLHSTMT,SQLUSMALLINT){cursor=0;return SQL_SUCCESS;}
SQLRETURN SQL_API SQLExecDirectW(SQLHSTMT,SQLWCHAR* sql,SQLINTEGER){check(std::wstring(reinterpret_cast<wchar_t*>(sql))==L"SELECT CONFIG FROM dbo.JC_CONFIG WHERE ID=15","only fixed read-only configuration query executes");phase=1;cursor=0;return SQL_SUCCESS;}
SQLRETURN SQL_API SQLPrepareW(SQLHSTMT,SQLWCHAR* sql,SQLINTEGER){
    const std::wstring text(reinterpret_cast<wchar_t*>(sql));
    check(text.find(L"SELECT TOP (2)")==0,"authentication prepares SELECT only");
    check(text.find(L"e.DELETE_BIT=0")!=std::wstring::npos && text.find(L"u.Delete_Bit=0")!=std::wstring::npos,"both soft-delete predicates");
    const bool source=text.find(L"u.Employee_Id=?")!=std::wstring::npos;
    if(source){check(text.find(L"Password")==std::wstring::npos,"source validation never selects password");check(text.find(L"u.login_port=?")!=std::wstring::npos&&text.find(L"UPPER(u.login_pcname)=UPPER(?)")!=std::wstring::npos,"source terminal is parameterized");}
    else check(text.find(L"UPPER(u.Code)=UPPER(?)")!=std::wstring::npos,"username parameterized case-insensitive comparison");
    phase=source?4:text.find(L"SELECT TOP (2) e.NAME")==0?3:2;cursor=0;bound.clear();return SQL_SUCCESS;
}
SQLRETURN SQL_API SQLBindParameter(SQLHSTMT,SQLUSMALLINT index,SQLSMALLINT,SQLSMALLINT,SQLSMALLINT,SQLULEN,SQLSMALLINT,SQLPOINTER value,SQLLEN,SQLLEN*){bound[index]=value;return SQL_SUCCESS;}
SQLRETURN SQL_API SQLExecute(SQLHSTMT){
    check(bound.size()==(phase==4?5:phase==3?1:3),"no password bound into SQL");
    if(phase==4){check(*static_cast<SQLBIGINT*>(bound[5])==200&&*static_cast<SQLINTEGER*>(bound[3])==4310&&std::wstring(static_cast<wchar_t*>(bound[4]))==L"TESTPC","source employee and terminal bound");}
    else check(std::wstring(static_cast<wchar_t*>(bound[phase==3?1:3]))==L"t123","normalized username bound");
    return scenario.query_error?SQL_ERROR:SQL_SUCCESS;
}
SQLRETURN SQL_API SQLFetch(SQLHSTMT){return cursor++<(phase==1?scenario.config_rows:scenario.accounts)?SQL_SUCCESS:SQL_NO_DATA;}
SQLRETURN SQL_API SQLGetData(SQLHSTMT,SQLUSMALLINT column,SQLSMALLINT type,SQLPOINTER buffer,SQLLEN size,SQLLEN* indicator){
    check(type==SQL_C_WCHAR,"Unicode output retrieval");
    bool null=false;std::wstring value;
    if(phase==1){value=scenario.config;null=scenario.config_null;}
    else if(phase==3){value=scenario.name;null=scenario.employee_missing;}
    else {
        const int lock=*static_cast<SQLINTEGER*>(bound[1]);
        const bool limited=lock>0&&scenario.errors>=5&&!scenario.date_null&&scenario.elapsed<lock;
        std::array<std::wstring,9> values{L"100",L"200",L"T123",scenario.stored,scenario.locked?L"1":L"0",L"200",scenario.name,scenario.expired?L"1":L"0",limited?L"1":L"0"};
        if(phase==4){
            values={L"100",L"200",L"T123",scenario.locked?L"1":L"0",L"200",scenario.name,scenario.expired?L"1":L"0",limited?L"1":L"0",scenario.online?L"1":L"0"};
        }
        value=values.at(column-1);null=scenario.employee_missing&&column==(phase==4?5:6);
    }
    if(null){*indicator=SQL_NULL_DATA;return SQL_SUCCESS;}
    *indicator=SQLLEN(value.size()*sizeof(wchar_t));
    check(size>*indicator,"test text fits buffer");std::memcpy(buffer,value.c_str(),std::size_t(*indicator)+sizeof(wchar_t));return SQL_SUCCESS;
}
}
int main(){
    search::DbSettings source_settings{L"fake",L"fake",L"fake",L"fake"};
    login::Source source;source.employee_id=200;source.port=4310;source.machine=L"TESTPC";
    auto adopted=[&]{return login::validate_source_account(source_settings,source,[]{return false;});};
    reset();auto account=adopted();check(account.status==login::Status::success&&!account.identity->adopted_source,"database alone does not publish a trusted source binding");
    reset();scenario.online=false;check(adopted().status!=login::Status::success,"offline or wrong terminal rejected");
    reset();scenario.locked=true;check(adopted().status==login::Status::locked,"source locked rejected");
    reset();scenario.expired=true;check(adopted().status==login::Status::expired,"source expired rejected");
    reset();scenario.errors=5;scenario.elapsed=59;check(adopted().status==login::Status::temporary_locked,"source error lock rejected");
    reset();scenario.employee_missing=true;check(adopted().status!=login::Status::success,"source missing employee rejected");
    reset();scenario.accounts=2;check(adopted().status!=login::Status::success,"unexpected source duplicates fail closed");
    reset();scenario.query_error=true;check(adopted().status!=login::Status::success,"source query failure rejected");
    reset();check(login::validate_source_account(source_settings,source,[]{return true;}).status!=login::Status::success,"cancelled source validation rejected");
    search::DbSettings name_settings{L"fake",L"fake",L"fake",L"fake"};
    reset();check(login::lookup_name(name_settings,L" t123 ",[]{return false;})==L"测试人员","read-only parameterized name lookup");
    reset();scenario.accounts=2;check(!login::lookup_name(name_settings,L"t123",[]{return false;}),"ambiguous name lookup stays blank");
    reset();scenario.accounts=0;check(!login::lookup_name(name_settings,L"t123",[]{return false;}),"unknown login name stays blank");
    reset();check(!login::lookup_name(name_settings,L"t123",[]{return true;}),"cancelled lookup cannot deliver name");
    reset();auto result=run();check(result.status==login::Status::success&&result.identity&&result.identity->employee_id==200,"successful identity mapping");
    reset();scenario.name=L" \u3000 ";check(run().identity->name==L"T123","empty employee name uses canonical login code");
    reset();scenario.accounts=0;check(run().status==login::Status::invalid_credentials,"no active account rejected");
    reset();scenario.accounts=2;check(run().status==login::Status::failed,"duplicate active accounts rejected");
    reset();scenario.employee_missing=true;check(run().status==login::Status::invalid_credentials,"missing active employee rejected");
    reset();scenario.locked=true;check(run().status==login::Status::locked,"locked account rejected");
    for(int elapsed:{59,60,61}){reset();scenario.errors=5;scenario.elapsed=elapsed;check(run().status==(elapsed<60?login::Status::temporary_locked:login::Status::success),"strict lock time boundary");}
    reset();scenario.errors=4;scenario.elapsed=0;check(run().status==login::Status::success,"four prior failures not blocked");
    reset();scenario.errors=5;scenario.date_null=true;check(run().status==login::Status::success,"NULL Errdatetime does not match SQL lock predicate");
    reset();scenario.errors=8;scenario.config=L"0";check(run().status==login::Status::success,"zero interval disables temporary lock");
    reset();scenario.expired=true;check(run().status==login::Status::expired,"expired account rejected");
    reset();check(run(L"abc123!").status==login::Status::invalid_credentials,"password comparison case-sensitive");
    reset();scenario.stored=L"invalid";check(run().status==login::Status::failed,"corrupt ciphertext fails");
    reset();scenario.config_rows=0;check(run().status==login::Status::failed,"missing configuration fails");
    reset();scenario.config_null=true;check(run().status==login::Status::failed,"NULL configuration fails");
    reset();scenario.config=L"bad";check(run().status==login::Status::failed,"invalid configuration fails");
    reset();scenario.query_error=true;check(run().status==login::Status::failed,"query/date conversion errors fail");
    reset();check(run(L" \u3000 ").status==login::Status::invalid_credentials&&!readonly,"empty password rejected before connecting");
    reset();int checks=0;
    search::DbSettings settings{L"fake",L"fake",L"fake",L"fake"};
    auto cancelled=login::authenticate(settings,L"t123",L"Abc123!",[&]{return ++checks>=5;});
    check(!cancelled.identity&&cancelled.status==login::Status::failed,"session cancellation after validation cannot publish identity");
    std::cout<<"Production authentication with read-only ODBC double passed\n";
}
