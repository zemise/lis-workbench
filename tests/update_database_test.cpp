#include "update_database.h"
#include "odbc_connection_policy.h"
#include <iostream>
#include <string>

int main() {
    // Match normal-query parameters, fallback order, and shared successful-candidate cache.
    const std::string key="packet size=4096;user id=synthetic;password=PRIVATE_PASSWORD;data source=synthetic-server;persist security info=True;initial catalog=synthetic-db";
    const auto candidates=search::odbc::odbc_candidates(key);
    if(candidates.size()!=3 || search::odbc::candidate_driver(candidates[0])!="ODBC Driver 18 for SQL Server" ||
       search::odbc::candidate_driver(candidates[1])!="ODBC Driver 17 for SQL Server" ||
       search::odbc::candidate_driver(candidates[2])!="SQL Server") return 1;
    for(const auto& candidate:candidates) {
        if(candidate.find("Encrypt=")!=std::string::npos || candidate.find("SERVER=synthetic-server;")==std::string::npos ||
           candidate.find("DATABASE=synthetic-db;")==std::string::npos) return 1;
    }
    if(candidates[0].find("TrustServerCertificate=Yes;")==std::string::npos ||
       candidates[1].find("TrustServerCertificate=Yes;")==std::string::npos ||
       candidates[2].find("TrustServerCertificate=")!=std::string::npos) return 1;
    search::odbc::remember_odbc_candidate(key,candidates[2]);
    std::string cached;
    const auto prioritized=search::odbc::prioritized_odbc_candidates(key,cached);
    if(prioritized.size()!=3 || prioritized.front()!=candidates[2] || cached!=candidates[2]) return 1;
    const auto other=search::odbc::prioritized_odbc_candidates(key+"-other",cached);
    if(!cached.empty() || search::odbc::candidate_driver(other.front())!="ODBC Driver 18 for SQL Server") return 1;
    search::DbSettings settings{L"synthetic-server",L"synthetic-db",L"synthetic",L"PRIVATE_PASSWORD"};
    const auto wkey=search::odbc::query_connection_string(settings);
    if(std::string(wkey.begin(),wkey.end())!=key) return 1;
    struct Scenario { const char* state; int native; const char* provider; bool trust; const char* expected; };
    const Scenario cases[] = {
        {"08001",0,"certificate verify failed; PRIVATE_CONNECTION",false,"服务器证书验证失败"},
        {"08001",0,"证书链不受信任 PRIVATE_CONNECTION",false,"服务器证书验证失败"},
        {"08001",-2146893019,"PRIVATE_CONNECTION",false,"服务器证书验证失败"},
        {"08001",0,"certificate verify failed PRIVATE_CONNECTION",true,"已启用服务器证书信任"},
        {"08001",0,"SSL handshake failed PRIVATE_CONNECTION",true,"TLS 加密连接失败"},
        {"28000",18456,"PRIVATE_CONNECTION",true,"SQL 登录失败"},
        {"42000",4060,"PRIVATE_CONNECTION",true,"目标 LIS 数据库"},
        {"28000",4060,"PRIVATE_CONNECTION",true,"目标 LIS 数据库"},
        {"HYT00",0,"PRIVATE_CONNECTION",true,"连接超时"},
        {"IM002",0,"PRIVATE_CONNECTION",true,"ODBC 驱动"},
        {"IM003",0,"PRIVATE_CONNECTION",true,"ODBC 驱动"},
        {"08001",0,"PRIVATE_CONNECTION",true,"检查服务器地址"},
        {"private",0,"PRIVATE_CONNECTION",true,"SQLSTATE=UNKNOWN"},
    };
    for(const auto& c:cases) {
        const auto result=lis_update::describe_database_connection_failure("ODBC Driver 18 for SQL Server",c.state,c.native,c.provider,c.trust);
        if(result.find(c.expected)==std::string::npos || result.find("PRIVATE_CONNECTION")!=std::string::npos ||
           result.find("Encrypt=DriverDefault")==std::string::npos ||
           result.find(c.trust?"TrustServerCertificate=Yes":"TrustServerCertificate=No")==std::string::npos) {
            std::cerr<<"diagnostic classification/redaction failed"; return 1;
        }
    }
    const auto legacy=lis_update::describe_database_connection_failure("SQL Server","08001",18,"SSL Security error PRIVATE_CONNECTION",true);
    if(legacy.find("旧版 SQL Server 驱动")==std::string::npos ||
       legacy.find("当前沿用普通查询策略")==std::string::npos || legacy.find("PRIVATE_CONNECTION")!=std::string::npos) return 1;
    const auto detail=lis_update::describe_database_connection_failures("ODBC Driver 18 for SQL Server",{
        {"08001",18,"SSL failure PRIVATE_CONNECTION"},
        {"08001",-2146893019,"PRIVATE_CONNECTION"}},false);
    if(detail.find("服务器证书验证失败")==std::string::npos || detail.find("PRIVATE_CONNECTION")!=std::string::npos) return 1;
    const auto algorithms=lis_update::describe_database_connection_failures("ODBC Driver 17 for SQL Server",{
        {"01000",0,"warning PRIVATE_CONNECTION"},
        {"08001",-2146893007,"PRIVATE_CONNECTION"}},true);
    if(algorithms.find("TLS 协议或密码算法不兼容")==std::string::npos ||
       algorithms.find("native=-2146893007")==std::string::npos || algorithms.find("PRIVATE_CONNECTION")!=std::string::npos) return 1;
    const auto empty=lis_update::describe_database_connection_failures("ODBC Driver 17 for SQL Server",{},true);
    if(empty.find("SQLSTATE=UNKNOWN")==std::string::npos) return 1;
    std::cout<<"17 diagnostic scenarios and shared query connection policy passed without a database connection\n";
}
