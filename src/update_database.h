#pragma once
#include "app_settings.h"
#include "update_source.h"
#include "update_config.h"
#include <functional>
#include <array>
#include <vector>

namespace lis_update {
// Legacy Windows "SQL Server" cannot reliably honor the modern TLS trust policy.
inline constexpr std::array<const wchar_t*, 2> kDatabaseUpdateDrivers = {
    L"ODBC Driver 18 for SQL Server", L"ODBC Driver 17 for SQL Server"
};
struct DatabaseConnectionDiagnostic {
    std::string state;
    int native_error = 0;
    std::string provider_text;
};
std::string describe_database_connection_failures(const std::string& driver,
    const std::vector<DatabaseConnectionDiagnostic>& diagnostics,
    bool trust_server_certificate);
// Provider text is used for classification only; it is never included in the result.
std::string describe_database_connection_failure(const std::string& driver,
    const std::string& state, int native_error, const std::string& provider_text,
    bool trust_server_certificate);
class DatabaseUpdateSource final : public IUpdateSource {
public:
    explicit DatabaseUpdateSource(search::DbSettings settings,
                                  bool trust_server_certificate = kDefaultTrustServerCertificate,
                                  std::function<bool()> cancelled = {});
    bool fetch_manifest(UpdateManifest& manifest, std::string& error) override;
    bool fetch_package(const UpdateManifest& manifest, const std::wstring& target_path,
                       std::string& error) override;
private:
    search::DbSettings settings_;
    bool trust_;
    std::function<bool()> cancelled_;
};
}
