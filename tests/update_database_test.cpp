#include "update_database.h"
#include <iostream>
#include <string>

int main() {
    if (!lis_update::kDefaultTrustServerCertificate) return 1;
    struct Scenario { const char* state; int native; const char* provider; bool trust; const char* expected; };
    const Scenario cases[] = {
        {"08001",0,"certificate verify failed; PRIVATE_CONNECTION",false,"TrustServerCertificate=1"},
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
           result.find("Encrypt=Yes")==std::string::npos ||
           result.find(c.trust?"TrustServerCertificate=Yes":"TrustServerCertificate=No")==std::string::npos) {
            std::cerr<<"diagnostic classification/redaction failed"; return 1;
        }
    }
    std::cout<<"13 connection diagnostic scenarios passed without a database connection\n";
}
