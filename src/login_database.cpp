#include "login_database.h"
#include "odbc_connection_policy.h"
#include "search_text.h"
#ifdef _WIN32
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <array>

namespace login {
namespace {
struct Connection {
    SQLHENV env=SQL_NULL_HENV; SQLHDBC db=SQL_NULL_HDBC; SQLHSTMT stmt=SQL_NULL_HSTMT;
    ~Connection() {
        if(stmt)SQLFreeHandle(SQL_HANDLE_STMT,stmt);
        if(db) {SQLDisconnect(db);SQLFreeHandle(SQL_HANDLE_DBC,db);}
        if(env)SQLFreeHandle(SQL_HANDLE_ENV,env);
    }
    bool open(const search::DbSettings& settings) {
        auto cs=search::odbc::query_connection_string(settings);if(cs.empty())return false;
        if(!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV,SQL_NULL_HANDLE,&env)) ||
           !SQL_SUCCEEDED(SQLSetEnvAttr(env,SQL_ATTR_ODBC_VERSION,reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3),0)) ||
           !SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC,env,&db)) ||
           !SQL_SUCCEEDED(SQLSetConnectAttr(db,SQL_ATTR_LOGIN_TIMEOUT,reinterpret_cast<SQLPOINTER>(5),0)) ||
           !SQL_SUCCEEDED(SQLSetConnectAttr(db,SQL_ATTR_ACCESS_MODE,reinterpret_cast<SQLPOINTER>(SQL_MODE_READ_ONLY),0)))return false;
        const auto key=search::wide_to_utf8(cs);std::string cached;
        for(const auto& candidate:search::odbc::prioritized_odbc_candidates(key,cached)) {
            auto text=search::utf8_to_wide(candidate);
            if(SQL_SUCCEEDED(SQLDriverConnectW(db,nullptr,reinterpret_cast<SQLWCHAR*>(text.data()),SQL_NTS,nullptr,0,nullptr,SQL_DRIVER_NOPROMPT))) {
                search::odbc::remember_odbc_candidate(key,candidate);
                return SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT,db,&stmt)) &&
                       SQL_SUCCEEDED(SQLSetStmtAttr(stmt,SQL_ATTR_QUERY_TIMEOUT,reinterpret_cast<SQLPOINTER>(10),0));
            }
        }
        return false;
    }
    bool text(int column,std::wstring& out,bool& is_null) {
        std::array<wchar_t,2049> buffer{};SQLLEN size=0;
        const auto rc=SQLGetData(stmt,SQLUSMALLINT(column),SQL_C_WCHAR,buffer.data(),sizeof(buffer),&size);
        if(rc!=SQL_SUCCESS)return false;
        is_null=size==SQL_NULL_DATA;
        if(is_null){out.clear();return true;}
        if(size<0 || size>=SQLLEN(sizeof(buffer)) || size%sizeof(wchar_t))return false;
        out.assign(buffer.data(),std::size_t(size)/sizeof(wchar_t));return true;
    }
};
struct PasswordGuard {
    std::wstring& password;
    ~PasswordGuard(){if(!password.empty())SecureZeroMemory(password.data(),password.size()*sizeof(wchar_t));}
};
std::int64_t number(const std::wstring& s) {
    std::size_t used=0;auto n=std::stoll(s,&used);if(used!=s.size())throw 0;return n;
}
}
std::optional<std::wstring> lookup_name(const search::DbSettings& settings,
    std::wstring code,const std::function<bool()>& cancelled) {
    code=trim(code);if(!valid_code(code))return {};
    auto stopped=[&]{return cancelled && cancelled();};
    try {
        Connection c;if(stopped()||!c.open(settings)||stopped())return {};
        const wchar_t* sql=L"SELECT TOP (2) e.NAME FROM dbo.Pub_User u "
            L"INNER JOIN dbo.JC_EMPLOYEE_PROPERTY e ON e.EMPLOYEE_ID=u.Employee_Id AND e.DELETE_BIT=0 "
            L"WHERE u.Delete_Bit=0 AND UPPER(u.Code)=UPPER(?)";
        SQLLEN length=SQL_NTS;
        if(!SQL_SUCCEEDED(SQLPrepareW(c.stmt,reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(sql)),SQL_NTS)) ||
           !SQL_SUCCEEDED(SQLBindParameter(c.stmt,1,SQL_PARAM_INPUT,SQL_C_WCHAR,SQL_WVARCHAR,50,0,code.data(),SQLLEN((code.size()+1)*sizeof(wchar_t)),&length)) ||
           !SQL_SUCCEEDED(SQLExecute(c.stmt)) || !SQL_SUCCEEDED(SQLFetch(c.stmt)))return {};
        std::wstring name;bool null=false;
        if(!c.text(1,name,null)||null||SQLFetch(c.stmt)!=SQL_NO_DATA||stopped())return {};
        return trim(name);
    }catch(...){return {};}
}
Result authenticate(const search::DbSettings& settings,std::wstring code,std::wstring password,
                    const std::function<bool()>& cancelled) {
    PasswordGuard secret{password};Result result;
    code=trim(code);password=trim(password);
    if(!valid_code(code) || password.empty()){result.status=Status::invalid_credentials;return result;}
    auto stopped=[&]{return cancelled && cancelled();};
    try {
        Connection c;if(stopped() || !c.open(settings) || stopped())return result;
        // Authentication uses fixed SELECT statements and bound parameters only.
        const wchar_t* config=L"SELECT CONFIG FROM dbo.JC_CONFIG WHERE ID=15";
        if(!SQL_SUCCEEDED(SQLExecDirectW(c.stmt,reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(config)),SQL_NTS)) ||
           !SQL_SUCCEEDED(SQLFetch(c.stmt)))return result;
        std::wstring value;bool null=false;int seconds=0;
        if(!c.text(1,value,null) || null || !parse_lock_seconds(value,seconds) || SQLFetch(c.stmt)!=SQL_NO_DATA)return result;
        SQLFreeStmt(c.stmt,SQL_CLOSE);
        if(stopped())return result;
        const wchar_t* accounts=L"SELECT TOP (2) u.Id,u.Employee_Id,u.Code,u.Password,u.Locked_Bit,"
            L"e.EMPLOYEE_ID,e.NAME,"
            L"CASE WHEN CAST(ISNULL(e.dlxq,'9999-01-01') AS datetime)<GETDATE() THEN 1 ELSE 0 END,"
            L"CASE WHEN ?>0 AND u.Errtimes>=5 AND DATEDIFF(second,u.Errdatetime,GETDATE())<? THEN 1 ELSE 0 END "
            L"FROM dbo.Pub_User u LEFT JOIN dbo.JC_EMPLOYEE_PROPERTY e "
            L"ON e.EMPLOYEE_ID=u.Employee_Id AND e.DELETE_BIT=0 "
            L"WHERE u.Delete_Bit=0 AND UPPER(u.Code)=UPPER(?)";
        SQLINTEGER lock=seconds;SQLLEN int_size=sizeof(lock),code_size=SQL_NTS;
        if(!SQL_SUCCEEDED(SQLPrepareW(c.stmt,reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(accounts)),SQL_NTS)))return result;
        for(int i=1;i<=2;++i)if(!SQL_SUCCEEDED(SQLBindParameter(c.stmt,SQLUSMALLINT(i),SQL_PARAM_INPUT,SQL_C_SLONG,SQL_INTEGER,0,0,&lock,0,&int_size)))return result;
        if(!SQL_SUCCEEDED(SQLBindParameter(c.stmt,3,SQL_PARAM_INPUT,SQL_C_WCHAR,SQL_WVARCHAR,50,0,code.data(),SQLLEN((code.size()+1)*sizeof(wchar_t)),&code_size)) ||
           !SQL_SUCCEEDED(SQLExecute(c.stmt)))return result;
        auto fetched=SQLFetch(c.stmt);
        if(fetched==SQL_NO_DATA){result.status=Status::invalid_credentials;return result;}
        if(!SQL_SUCCEEDED(fetched))return result;
        std::array<std::wstring,9> columns;std::array<bool,9> nulls{};
        PasswordGuard stored{columns[3]};
        for(int i=0;i<9;++i)if(!c.text(i+1,columns[i],nulls[i]))return result;
        if(SQLFetch(c.stmt)!=SQL_NO_DATA || stopped())return result; // Reject duplicates and fetch errors.
        if(nulls[0]||nulls[1]||nulls[2]||nulls[3]||nulls[4]||nulls[7]||nulls[8])return result;
        if(nulls[5]){result.status=Status::invalid_credentials;return result;}
        if(number(columns[4])>0){result.status=Status::locked;return result;}
        if(number(columns[8])!=0){result.status=Status::temporary_locked;return result;}
        std::wstring decoded;PasswordGuard plain{decoded};
        if(!decrypt_legacy(columns[3],decoded))return result;
        if(decoded!=password){result.status=Status::invalid_credentials;return result;}
        if(number(columns[7])!=0){result.status=Status::expired;return result;}
        Identity identity{number(columns[0]),number(columns[1]),columns[2],trim(columns[6])};
        if(identity.name.empty())identity.name=identity.code;
        if(stopped())return result;
        result.status=Status::success;result.identity=std::move(identity);return result;
    } catch(...) {return result;}
}
}
#endif
