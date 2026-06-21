#include "quality_control_import.h"

#ifdef _WIN32

#include "quality_control_store.h"
#include "search_core.h"
#include "search_text.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <set>

namespace qc {
namespace {

std::string now_text() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buffer;
}

bool parse_number(const std::string& text, double& value) {
    const std::string trimmed = search::trim(text);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    value = std::strtod(trimmed.c_str(), &end);
    return end && *end == '\0';
}

bool config_matches_item(const Config& cfg, const search::QualityControlLisRow& item) {
    const std::string configured = search::trim(cfg.item_code);
    return configured.empty() || configured == search::trim(item.item_code);
}

}  // namespace

bool import_from_lis(const ImportRequest& request, ImportResult& result, std::string& error) {
    result = {};
    std::vector<Config> configs;
    if (!load_configs(configs, error)) return false;

    std::vector<Config> active;
    for (const auto& cfg : configs) {
        if (!cfg.enabled) continue;
        if (search::trim(cfg.mach_code).empty() || search::trim(cfg.sample_no).empty()) continue;
        if (!search::trim(request.mach_code).empty() &&
            search::trim(cfg.mach_code) != search::trim(request.mach_code)) {
            continue;
        }
        active.push_back(cfg);
    }

    const std::string started = now_text();
    ImportLog log;
    log.started_at = started;
    log.start_date = request.start_date;
    log.end_date = request.end_date;
    log.mach_code = request.mach_code;

    if (active.empty()) {
        log.finished_at = now_text();
        log.status = "ok";
        log.error = "no enabled quality control configs";
        insert_import_log(log, error);
        return true;
    }

    const std::wstring connection_w = search::build_connection_string_w(request.db_settings);
    const std::string connection = search::wide_to_utf8(connection_w);
    if (search::trim(connection).empty()) {
        error = "missing LIS database connection";
        log.finished_at = now_text();
        log.status = "failed";
        log.error = error;
        std::string ignored;
        insert_import_log(log, ignored);
        return false;
    }

    std::map<std::string, std::vector<Config>> configs_by_machine_sample;
    for (const auto& cfg : active) {
        configs_by_machine_sample[search::trim(cfg.mach_code) + "\x1f" + search::trim(cfg.sample_no)].push_back(cfg);
    }

    std::set<std::string> queried_keys;
    for (const auto& cfg : active) {
        const std::string key = search::trim(cfg.mach_code) + "\x1f" + search::trim(cfg.sample_no);
        if (!queried_keys.insert(key).second) continue;

        search::QualityControlLisQuery query;
        query.connection_string = connection;
        query.start_date = request.start_date;
        query.end_date = request.end_date;
        query.mach_code = search::trim(cfg.mach_code);
        query.sample_no = search::trim(cfg.sample_no);

        std::vector<search::QualityControlLisRow> items;
        if (!search::query_quality_control_lis_results(query, items, error)) {
            log.finished_at = now_text();
            log.status = "failed";
            log.error = error;
            std::string ignored;
            insert_import_log(log, ignored);
            return false;
        }

        const auto config_it = configs_by_machine_sample.find(key);
        if (config_it == configs_by_machine_sample.end()) continue;
        for (const auto& item : items) {
            bool matched_any = false;
            for (const auto& item_cfg : config_it->second) {
                if (!config_matches_item(item_cfg, item)) continue;
                matched_any = true;

                Result row;
                row.source_rep_no = item.rep_no;
                row.source_entry_key = item.entry_id;
                row.room_code = item.room_code;
                row.mach_code = item.mach_code;
                row.mach_name = item.mach_name;
                row.sample_no = item.sample_no;
                row.barcode_no = item.barcode_no;
                row.report_date = item.report_date;
                row.inspect_date = item.inspect_date;
                row.report_time = item.report_time;
                row.effective_time = item.effective_time;
                row.chk_flag = item.chk_flag;
                row.conf = item.conf;
                row.item_code = item.item_code;
                row.item_name = item.item_name;
                row.result_text = item.result;
                row.has_numeric_value = parse_number(item.result, row.result_value);
                row.unit = item.unit;
                row.normal = item.normal;
                row.qc_name = item_cfg.qc_name;
                row.level = item_cfg.level;
                row.target_mean = item_cfg.target_mean;
                row.target_sd = item_cfg.target_sd;
                bool inserted = false;
                if (!upsert_result(row, inserted, error)) {
                    log.finished_at = now_text();
                    log.status = "failed";
                    log.error = error;
                    std::string ignored;
                    insert_import_log(log, ignored);
                    return false;
                }
                if (inserted) ++result.imported_count;
                else ++result.updated_count;
            }
            if (!matched_any) ++result.skipped_count;
        }
    }

    log.finished_at = now_text();
    log.imported_count = result.imported_count;
    log.updated_count = result.updated_count;
    log.skipped_count = result.skipped_count;
    log.status = "ok";
    if (!insert_import_log(log, error)) return false;
    return true;
}

}  // namespace qc

#endif
