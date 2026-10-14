#include "update_database.h"
#include "odbc_connection_policy.h"
#include "update_package.h"
#include <utility>
#include <algorithm>
#include <cctype>

namespace lis_update {
std::string describe_database_connection_failure(const std::string& driver,
    const std::string& state, int native, const std::string& provider_text, bool trust) {
    std::string text=provider_text;
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});
    const auto has=[&](const char* token){return text.find(token)!=std::string::npos;};
    std::string cause;
    if(state=="IM002" || state=="IM003") {
        cause="未找到或无法加载可用的 ODBC 驱动，请检查普通查询使用的驱动及程序位数。";
    } else if(native==4060 || native==911) {
        cause="目标 LIS 数据库不存在或账号无权访问，请检查数据库名称及权限。";
    } else if(state=="28000" || native==18456 || native==18452) {
        cause="SQL 登录失败，请检查系统设置中的数据库账号和密码。";
    } else if(state=="HYT00" || state=="HYT01") {
        cause="连接超时，请检查服务器地址、端口和局域网连通性。";
    } else if(driver=="SQL Server" && (native==18 || has("ssl") || has("tls") || has("certificate") || has("证书"))) {
        cause="旧版 SQL Server 驱动发生 TLS／SSL 协商失败；当前沿用普通查询策略，请核对普通查询是否正常及服务器强制加密、驱动和系统 TLS 支持。";
    } else if(native==-2146893007) {
        cause="TLS 协议或密码算法不兼容；服务器证书信任开关不能解决此问题，请检查驱动及客户端系统 TLS 支持。";
    } else if(has("certificate") || has("证书") || native==-2146893019 || native==-2146893022) {
        cause=trust ? "证书或 TLS 协商失败；已启用服务器证书信任，请检查驱动、服务器和系统 TLS 兼容性。"
                    : "服务器证书验证失败；当前沿用普通查询策略，请检查驱动与服务器证书配置。";
    } else if(has("ssl") || has("tls")) {
        cause="TLS 加密连接失败，请检查 SQL Server ODBC 驱动及系统 TLS 兼容性。";
    } else {
        cause="LIS 更新连接失败，请检查服务器地址、数据库配置及连接驱动。";
    }
    const bool valid_state=state.size()==5 && std::all_of(state.begin(),state.end(),[](unsigned char c){
        return (c>='A'&&c<='Z') || (c>='0'&&c<='9');
    });
    return cause+" [driver="+driver+", SQLSTATE="+(valid_state?state:"UNKNOWN")+
        ", native="+std::to_string(native)+", Encrypt=DriverDefault, TrustServerCertificate="+
        (driver=="SQL Server" || driver=="ODBC manager" ? "DriverDefault" : trust?"Yes":"No")+"]";
}

std::string describe_database_connection_failures(const std::string& driver,
    const std::vector<DatabaseConnectionDiagnostic>& diagnostics, bool trust) {
    if (diagnostics.empty()) return describe_database_connection_failure(driver,"",0,"",trust);
    const auto priority=[](const DatabaseConnectionDiagnostic& d) {
        std::string text=d.provider_text;
        std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(d.native_error==4060 || d.native_error==911 || d.state=="28000" ||
           d.native_error==18456 || d.native_error==18452) return 100;
        if(d.native_error==-2146893007) return 95;
        if(text.find("certificate")!=std::string::npos || text.find("证书")!=std::string::npos ||
           d.native_error==-2146893019 || d.native_error==-2146893022) return 90;
        if(d.state=="IM002" || d.state=="IM003" || d.state=="HYT00" || d.state=="HYT01") return 80;
        if(text.find("ssl")!=std::string::npos || text.find("tls")!=std::string::npos) return 60;
        return d.state.rfind("01",0)==0 ? 0 : 10;
    };
    const auto best=std::max_element(diagnostics.begin(),diagnostics.end(),
        [&](const auto& a,const auto& b){return priority(a)<priority(b);});
    return describe_database_connection_failure(driver,best->state,best->native_error,best->provider_text,trust);
}

}

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
std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int len=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(size_t(len),'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),len,nullptr,nullptr);
    return out;
}
std::vector<lis_update::DatabaseConnectionDiagnostic> connection_diagnostics(SQLSMALLINT type, SQLHANDLE handle) {
    std::vector<lis_update::DatabaseConnectionDiagnostic> records;
    for (SQLSMALLINT index=1; index<=16; ++index) {
        SQLWCHAR state[6]={}, message[2048]={}; SQLINTEGER native=0; SQLSMALLINT length=0;
        const SQLRETURN rc=SQLGetDiagRecW(type,handle,index,state,&native,message,2048,&length);
        if(!SQL_SUCCEEDED(rc)) break;
        records.push_back({utf8(std::wstring((wchar_t*)state)),native,utf8(std::wstring((wchar_t*)message))});
    }
    return records;
}
std::string odbc_connection_error(SQLSMALLINT type, SQLHANDLE handle, const wchar_t* driver, bool trust) {
    return lis_update::describe_database_connection_failures(utf8(driver),connection_diagnostics(type,handle),trust);
}
struct Connection {
    SQLHENV env=SQL_NULL_HENV; SQLHDBC db=SQL_NULL_HDBC; SQLHSTMT stmt=SQL_NULL_HSTMT;
    ~Connection() {
        if(stmt!=SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT,stmt);
        if(db!=SQL_NULL_HDBC) { SQLDisconnect(db); SQLFreeHandle(SQL_HANDLE_DBC,db); }
        if(env!=SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV,env);
    }
    bool open(const search::DbSettings& cfg, unsigned timeout, std::string& error) {
        const bool trust=false; // No driver has been selected yet.
        error="无法初始化 LIS 更新 ODBC 环境，请检查对应位数的 SQL Server 驱动。";
        if(cfg.server.empty() || cfg.initial_database.empty() || cfg.user.empty()) {
            error="configure the LIS database connection before checking updates"; return false;
        }
        if(!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV,SQL_NULL_HANDLE,&env)) ||
           !SQL_SUCCEEDED(SQLSetEnvAttr(env,SQL_ATTR_ODBC_VERSION,(SQLPOINTER)SQL_OV_ODBC3,0)) ||
           !SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC,env,&db))) return false;
        if(!SQL_SUCCEEDED(SQLSetConnectAttr(db,SQL_LOGIN_TIMEOUT,(SQLPOINTER)5,0))) {
            error="设置连接超时失败："+odbc_connection_error(SQL_HANDLE_DBC,db,L"ODBC manager",trust);
            return false;
        }
        // Same catalog and credentials, dedicated read-only connection; never executes DDL/DML.
        if (!SQL_SUCCEEDED(SQLSetConnectAttr(db,SQL_ATTR_ACCESS_MODE,(SQLPOINTER)SQL_MODE_READ_ONLY,0))) {
            error="设置只读连接失败："+odbc_connection_error(SQL_HANDLE_DBC,db,L"ODBC manager",trust);
            return false;
        }
        const auto connection_key=utf8(search::odbc::query_connection_string(cfg));
        std::string cached_candidate;
        const auto candidates=search::odbc::prioritized_odbc_candidates(connection_key,cached_candidate);
        std::string attempts;
        for (const auto& candidate:candidates) {
            const auto driver_name=search::odbc::candidate_driver(candidate);
            const auto driver=lis_update::utf8_to_wide_update(driver_name);
            const bool trust=driver_name!="SQL Server";
            const auto cs=lis_update::utf8_to_wide_update(candidate);
            SQLRETURN rc=SQLDriverConnectW(db,nullptr,(SQLWCHAR*)cs.c_str(),SQL_NTS,nullptr,0,nullptr,SQL_DRIVER_NOPROMPT);
            if(SQL_SUCCEEDED(rc)) {
                search::odbc::remember_odbc_candidate(connection_key,candidate);
                if(!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT,db,&stmt))) {
                    error="创建更新查询失败："+odbc_connection_error(SQL_HANDLE_DBC,db,driver.c_str(),trust);
                    return false;
                }
                if(!SQL_SUCCEEDED(SQLSetStmtAttr(stmt,SQL_ATTR_QUERY_TIMEOUT,(SQLPOINTER)(uintptr_t)timeout,0))) {
                    error="设置查询超时失败："+odbc_connection_error(SQL_HANDLE_STMT,stmt,driver.c_str(),trust);
                    return false;
                }
                error.clear(); return true;
            }
            // The user selected the same fallback policy as normal queries.
            // Each attempt keeps its original driver parameters; no forced Encrypt flag.
            const auto diagnostics=connection_diagnostics(SQL_HANDLE_DBC,db);
            if(!attempts.empty()) attempts+="\n";
            attempts+=lis_update::describe_database_connection_failures(driver_name,diagnostics,trust);
        }
        error="LIS 更新沿用普通查询连接策略，但所有候选连接均失败：\n"+attempts;
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
DatabaseUpdateSource::DatabaseUpdateSource(search::DbSettings cfg,std::function<bool()> cancel)
    :settings_(std::move(cfg)),cancelled_(std::move(cancel)) {}
bool DatabaseUpdateSource::fetch_manifest(UpdateManifest& manifest,std::string& error) {
#ifndef _WIN32
    (void)manifest; error="LIS updates are only available on Windows"; return false;
#else
    if(cancelled_ && cancelled_()) {error="update cancelled"; return false;}
    Connection c;
    if(!c.open(settings_,10,error)) return false;
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
    Connection c; if(!c.open(settings_,120,error)) return false;
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
