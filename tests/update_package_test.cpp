#include "update_manifest.h"
#include "update_package.h"
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::wstring path;
    for (const char* p=argv[1]; *p; ++p) path += wchar_t(*p);
    lis_update::UpdateZipInfo zip; std::string error;
    const bool result=lis_update::validate_update_zip(path,zip,error);
    if (result != (std::string(argv[2])=="valid")) { std::cerr<<error;return 1; }
    if (result && zip.files<2) return 1;
    lis_update::UpdateManifest m;
    m.app_id="lis-workbench"; m.channel="stable"; m.version="v2026.10.12";
    m.min_updater_version="1.0.0"; m.package.file="LISWorkbench-v2026.10.12-win7-win11.zip";
    m.package.size=1; m.package.sha256=std::string(64,'a');
    lis_update::UpdateManifest roundtrip;
    if(!lis_update::parse_update_manifest_json(lis_update::serialize_update_manifest(m),roundtrip,error)) return 1;
    m.min_updater_version="2.0.0";
    if(lis_update::is_supported_manifest(m,error)) return 1;
    return 0;
}
