#pragma once

#ifdef _WIN32

#include "app_settings.h"

#include <string>

namespace qc {

struct ImportRequest {
    search::DbSettings db_settings;
    std::string start_date;
    std::string end_date;
    std::string mach_code;
};

struct ImportResult {
    int imported_count = 0;
    int updated_count = 0;
    int skipped_count = 0;
};

bool import_from_lis(const ImportRequest& request, ImportResult& result, std::string& error);

}  // namespace qc

#endif
