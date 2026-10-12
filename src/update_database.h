#pragma once
#include "app_settings.h"
#include "update_source.h"
#include <functional>

namespace lis_update {
class DatabaseUpdateSource final : public IUpdateSource {
public:
    explicit DatabaseUpdateSource(search::DbSettings settings,
                                  bool trust_server_certificate = false,
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
