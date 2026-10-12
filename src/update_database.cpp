#include "update_database.h"
#include "update_package.h"
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <sqlext.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {
std::wstring quoted(const std::wstring& s) {
    std::wstring out=L"{";
    for (wchar_t c:s) { out+=c; if(c==L'}') out+=c; }
    return out+L"}";
}
std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int len=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(size_t(len),'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),len,nullptr,nullptr);
    return out;
}
struct Connection {
    SQLHENV env=SQL_NULL_HENV; SQLHDBC db=SQL_NULL_HDBC; SQLHSTMT stmt=SQL_NULL_HSTMT;
    ~Connection() {
        if(stmt!=SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT,stmt);
        if(db!=SQL_NULL_HDBC) { SQLDisconnect(db); SQLFreeHandle(SQL_HANDLE_DBC,db); }
        if(env!=SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV,env);
    }
    bool open(const search::DbSettings& cfg, bool trust, unsigned timeout, std::string& error) {
        error="LIS update connection failed; check database settings, driver and certificate";
        if(cfg.server.empty() || cfg.initial_database.empty() || cfg.user.empty()) {
            error="configure the LIS database connection before checking updates"; return false;
        }
        if(!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV,SQL_NULL_HANDLE,&env)) ||
           !SQL_SUCCEEDED(SQLSetEnvAttr(env,SQL_ATTR_ODBC_VERSION,(SQLPOINTER)SQL_OV_ODBC3,0)) ||
           !SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC,env,&db))) return false;
        SQLSetConnectAttr(db,SQL_LOGIN_TIMEOUT,(SQLPOINTER)5,0);
        // Same catalog and credentials, dedicated read-only connection; never executes DDL/DML.
        if (!SQL_SUCCEEDED(SQLSetConnectAttr(db,SQL_ATTR_ACCESS_MODE,(SQLPOINTER)SQL_MODE_READ_ONLY,0))) return false;
        for (const wchar_t* driver:{L"ODBC Driver 18 for SQL Server",L"ODBC Driver 17 for SQL Server",L"SQL Server"}) {
            std::wstring cs=L"DRIVER="+quoted(driver)+L";SERVER="+quoted(cfg.server)+L";DATABASE="+quoted(cfg.initial_database)+
                L";UID="+quoted(cfg.user)+L";PWD="+quoted(cfg.password)+L";Encrypt=Yes;TrustServerCertificate="+(trust?L"Yes;":L"No;");
            SQLRETURN rc=SQLDriverConnectW(db,nullptr,(SQLWCHAR*)cs.data(),SQL_NTS,nullptr,0,nullptr,SQL_DRIVER_NOPROMPT);
            if(SQL_SUCCEEDED(rc)) {
                if(!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT,db,&stmt)) ||
                   !SQL_SUCCEEDED(SQLSetStmtAttr(stmt,SQL_ATTR_QUERY_TIMEOUT,(SQLPOINTER)(uintptr_t)timeout,0))) return false;
                error.clear(); return true;
            }
            // Only missing-driver errors permit trying another driver, never TLS/auth fallback.
            SQLWCHAR state[6]={}; SQLINTEGER native=0;
            SQLGetDiagRecW(SQL_HANDLE_DBC,db,1,state,&native,nullptr,0,nullptr);
            if(std::wstring((wchar_t*)state)!=L"IM002" && std::wstring((wchar_t*)state)!=L"IM003") return false;
        }
        return false;
    }
};
bool text_column(SQLHSTMT stmt, unsigned col, std::string& out) {
    std::array<wchar_t,2049> data{}; SQLLEN len=0;
    SQLRETURN rc=SQLGetData(stmt,SQLUSMALLINT(col),SQL_C_WCHAR,data.data(),sizeof(data),&len);
    if(rc!=SQL_SUCCESS) return false;
    if(len==SQL_NULL_DATA) { out.clear(); return true; }
    if(len<0 || len>=SQLLEN(sizeof(data))) return false;
    out=utf8(std::wstring(data.data(),size_t(len)/sizeof(wchar_t))); return true;
}
struct Watch {
    std::mutex mutex; std::condition_variable cv; bool stopped=false;
    std::atomic<bool> aborted{false}; std::thread thread;
    Watch(SQLHSTMT stmt, std::function<bool()> cancel) : thread([this,stmt,cancel] {
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
        std::unique_lock<std::mutex> lock(mutex);
        while(!cv.wait_for(lock,std::chrono::milliseconds(100),[this]{return stopped;})) {
            if((cancel && cancel()) || std::chrono::steady_clock::now()>=deadline) {
                aborted=true; SQLCancel(stmt); return;
            }
        }
    }) {}
    ~Watch() { {std::lock_guard<std::mutex> lock(mutex); stopped=true;} cv.notify_all(); thread.join(); }
};
}
#endif

namespace lis_update {
DatabaseUpdateSource::DatabaseUpdateSource(search::DbSettings cfg,bool trust,std::function<bool()> cancel)
    :settings_(std::move(cfg)),trust_(trust),cancelled_(std::move(cancel)) {}
bool DatabaseUpdateSource::fetch_manifest(UpdateManifest& manifest,std::string& error) {
#ifndef _WIN32
    (void)manifest; error="LIS updates are only available on Windows"; return false;
#else
    if(cancelled_ && cancelled_()) {error="update cancelled"; return false;}
    Connection c;
    if(!c.open(settings_,trust_,10,error)) return false;
    const wchar_t* query=L"SELECT PackageId,AppId,Channel,Version,MinUpdaterVersion,PackageFileName,PackageSize,PackageSha256,ReleaseNotes,CONVERT(varchar(33),PublishedAtUtc,126) FROM dbo.LS_AS_LISWORK WHERE AppId='lis-workbench' AND Channel='stable' AND State='published' AND IsCurrent=1 AND VerifiedAtUtc IS NOT NULL";
    error="cannot read LIS update metadata; check the update table and SELECT permission";
    Watch watch(c.stmt,cancelled_);
    if(!SQL_SUCCEEDED(SQLExecDirectW(c.stmt,(SQLWCHAR*)query,SQL_NTS))) return false;
    SQLRETURN rc=SQLFetch(c.stmt);
    if(rc==SQL_NO_DATA) {error="no LIS version has been published"; return false;}
    if(!SQL_SUCCEEDED(rc)) return false;
    UpdateManifest m; SQLLEN indicator=0; SQLBIGINT id=0,size=0;
    if(SQLGetData(c.stmt,1,SQL_C_SBIGINT,&id,sizeof(id),&indicator)!=SQL_SUCCESS || indicator==SQL_NULL_DATA || id<=0) return false;
    if(!text_column(c.stmt,2,m.app_id) || !text_column(c.stmt,3,m.channel) || !text_column(c.stmt,4,m.version) ||
       !text_column(c.stmt,5,m.min_updater_version) || !text_column(c.stmt,6,m.package.file)) return false;
    if(SQLGetData(c.stmt,7,SQL_C_SBIGINT,&size,sizeof(size),&indicator)!=SQL_SUCCESS || indicator==SQL_NULL_DATA || size<=0) return false;
    if(!text_column(c.stmt,8,m.package.sha256) || !text_column(c.stmt,9,m.release_notes) || !text_column(c.stmt,10,m.published_at)) return false;
    m.database_package_id=id; m.package.size=std::uint64_t(size);
    if(SQLFetch(c.stmt)!=SQL_NO_DATA || watch.aborted) {error="invalid or cancelled LIS metadata result"; return false;}
    if(m.package.file != "LISWorkbench-" + m.version + "-win7-win11.zip") {
        error="invalid LIS package filename"; return false;
    }
    if(!is_supported_manifest(m,error)) return false;
    manifest=std::move(m); return true;
#endif
}
bool DatabaseUpdateSource::fetch_package(const UpdateManifest& m,const std::wstring& target,std::string& error) {
#ifndef _WIN32
    (void)m;(void)target; error="LIS updates are only available on Windows"; return false;
#else
    if(m.database_package_id<=0 || !is_supported_manifest(m,error)) {error="invalid LIS package snapshot"; return false;}
    if(cancelled_ && cancelled_()) {error="update cancelled"; return false;}
    Connection c; if(!c.open(settings_,trust_,120,error)) return false;
    error="cannot read LIS package; it may have been cleaned; check updates again";
    const wchar_t* query=L"SELECT PackageData FROM dbo.LS_AS_LISWORK WHERE PackageId=? AND AppId='lis-workbench' AND Channel='stable' AND Version=? AND PackageSha256=? AND PackageSize=? AND PackageFileName=? AND MinUpdaterVersion=? AND VerifiedAtUtc IS NOT NULL AND State IN ('published','retired')";
    SQLBIGINT id=m.database_package_id,size=SQLBIGINT(m.package.size);
    if(!SQL_SUCCEEDED(SQLPrepareW(c.stmt,(SQLWCHAR*)query,SQL_NTS))) return false;
    SQLLEN lengths[7]={0};
    auto number=[&](int col,SQLBIGINT& value){return SQL_SUCCEEDED(SQLBindParameter(c.stmt,SQLUSMALLINT(col),SQL_PARAM_INPUT,SQL_C_SBIGINT,SQL_BIGINT,0,0,&value,0,&lengths[col-1]));};
    auto string=[&](int col,const std::string& value){lengths[col-1]=SQL_NTS;return SQL_SUCCEEDED(SQLBindParameter(c.stmt,SQLUSMALLINT(col),SQL_PARAM_INPUT,SQL_C_CHAR,SQL_VARCHAR,value.size(),0,(SQLPOINTER)value.c_str(),SQLLEN(value.size()+1),&lengths[col-1]));};
    // Package filenames are generated ASCII identifiers; other text remains UTF-8 metadata.
    if(!number(1,id)||!string(2,m.version)||!string(3,m.package.sha256)||!number(4,size)||!string(5,m.package.file)||!string(6,m.min_updater_version)) return false;
    Watch watch(c.stmt,cancelled_);
    if(!SQL_SUCCEEDED(SQLExecute(c.stmt)) || !SQL_SUCCEEDED(SQLFetch(c.stmt))) return false;
    const std::wstring part=target+L".part";
    HANDLE file=CreateFileW(part.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){error="cannot create temporary update file"; return false;}
    bool ok=true; std::uint64_t total=0; unsigned char buffer[65536];
    while(ok) {
        if(watch.aborted || (cancelled_ && cancelled_())) {ok=false; break;}
        SQLLEN len=0; SQLRETURN rc=SQLGetData(c.stmt,1,SQL_C_BINARY,buffer,sizeof(buffer),&len);
        if(rc==SQL_NO_DATA) break;
        if((rc!=SQL_SUCCESS && rc!=SQL_SUCCESS_WITH_INFO) || len==SQL_NULL_DATA) {ok=false; break;}
        size_t bytes=0;
        if(rc==SQL_SUCCESS_WITH_INFO) {
            SQLCHAR state[6]={}; SQLINTEGER native=0;
            SQLGetDiagRecA(SQL_HANDLE_STMT,c.stmt,1,state,&native,nullptr,0,nullptr);
            if(std::string((char*)state)!="01004") {ok=false;break;}
            bytes=sizeof(buffer);
        } else if(len>=0 && len<=SQLLEN(sizeof(buffer))) bytes=size_t(len);
        else {ok=false;break;}
        total+=bytes;
        DWORD written=0;
        if(total>m.package.size || !WriteFile(file,buffer,DWORD(bytes),&written,nullptr) || written!=bytes) {ok=false;break;}
        if(rc==SQL_SUCCESS) break;
    }
    if(!FlushFileBuffers(file)) ok=false;
    CloseHandle(file);
    UpdateZipInfo zip;
    if(!ok || watch.aborted || total!=m.package.size || !verify_update_package(part,m,error) || !validate_update_zip(part,zip,error)) {
        DeleteFileW(part.c_str()); if(error.empty()) error="LIS package incomplete or cancelled"; return false;
    }
    if(!MoveFileExW(part.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(part.c_str());error="cannot finalize LIS package cache";return false;
    }
    error.clear(); return true;
#endif
}
}
