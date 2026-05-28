#pragma once

#include "update_manifest.h"

#include <string>

namespace lis_update {

struct UpdateCheckResult {
    bool update_available = false;
    UpdateManifest manifest;
    std::wstring package_path;
};

class IUpdateSource {
public:
    virtual ~IUpdateSource() = default;
    virtual bool fetch_manifest(UpdateManifest& manifest, std::string& error) = 0;
    virtual bool fetch_package(const UpdateManifest& manifest,
                               const std::wstring& target_path,
                               std::string& error) = 0;
};

class FolderUpdateSource final : public IUpdateSource {
public:
    explicit FolderUpdateSource(std::wstring root_path);

    bool fetch_manifest(UpdateManifest& manifest, std::string& error) override;
    bool fetch_package(const UpdateManifest& manifest,
                       const std::wstring& target_path,
                       std::string& error) override;

private:
    std::wstring root_path_;
};

class HttpUpdateSource final : public IUpdateSource {
public:
    explicit HttpUpdateSource(std::wstring manifest_url);

    bool fetch_manifest(UpdateManifest& manifest, std::string& error) override;
    bool fetch_package(const UpdateManifest& manifest,
                       const std::wstring& target_path,
                       std::string& error) override;

private:
    std::wstring manifest_url_;
};

bool verify_update_package(const std::wstring& path, const UpdateManifest& manifest,
                           std::string& error);

bool check_and_fetch_update(IUpdateSource& source,
                            const std::string& current_version,
                            const std::wstring& cache_dir,
                            UpdateCheckResult& result,
                            std::string& error);

std::wstring join_update_path(const std::wstring& left, const std::wstring& right);
std::wstring utf8_to_wide_update(const std::string& value);

}  // namespace lis_update
