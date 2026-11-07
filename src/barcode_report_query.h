#pragma once
#include "barcode_report_matching.h"
#ifdef _WIN32
#include <windows.h>
#include <sqlext.h>
namespace search {
bool read_barcode_columns(SQLHSTMT statement, unsigned count, std::vector<std::string>& columns, std::string& error);
bool supplement_barcode_reports(SQLHDBC connection, const BarcodeQueryFilters& filters,
    const std::vector<BarcodeMatchSource>& sources, std::vector<BarcodeQueryRow>& rows,
    std::string& error, const std::function<bool()>& cancelled);
}
#endif
