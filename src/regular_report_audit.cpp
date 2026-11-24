#include "regular_report_audit.h"

#include "search_text.h"

#include <algorithm>
#include <cstdlib>

namespace search {
namespace {

bool flag_is_one(const std::string& value) {
    return trim(value) == "1";
}

std::string trim_copy(const std::string& value) {
    return trim(value);
}

}  // namespace

bool AuditConfig::tat_enabled() const { return flag_is_one(v4147); }
bool AuditConfig::initial_review_required() const { return flag_is_one(v4245); }
bool AuditConfig::barcode_empty_check() const { return flag_is_one(v4460); }
bool AuditConfig::sex_age_required() const { return flag_is_one(v4778); }
bool AuditConfig::clear_tj_read_flag() const {
    const std::string value = trim(v4850);
    return !value.empty() && value.front() == '1';
}
bool AuditConfig::reviewer_mapping_enabled() const { return flag_is_one(v4149); }
bool AuditConfig::custom_rules_enabled() const { return trim(v4141) == "1"; }
bool AuditConfig::custom_rules_hard_block() const { return trim(v4453) == "1"; }
bool AuditConfig::item_audit_rules_enabled() const { return flag_is_one(v4246); }
bool AuditConfig::negative_result_confirm() const { return flag_is_one(v4405); }
bool AuditConfig::audit_confirm() const { return flag_is_one(v4507); }
bool AuditConfig::item_count_check_enabled() const { return flag_is_one(v4527); }
bool AuditConfig::crisis_message_required() const { return flag_is_one(v4222); }
bool AuditConfig::critical_value_check_enabled() const {
    return flag_is_one(v4804) || flag_is_one(v4242);
}
int AuditConfig::string_critical_mode() const {
    const std::string value = trim(v4247);
    if (value == "1") return 1;
    if (value == "2") return 2;
    if (value == "4") return 4;
    return 0;
}

bool parse_audit_number(const std::string& text, double& out) {
    const std::string value = trim_copy(text);
    if (value.empty()) return false;
    char* end = nullptr;
    out = std::strtod(value.c_str(), &end);
    return end && *end == '\0';
}

std::string evaluate_custom_audit_rules(const CustomAuditRules& rules,
                                        const CustomAuditHeader& header,
                                        const std::vector<CustomAuditEntry>& entries) {
    std::string message;
    const std::string prefix = "样本号【" + trim_copy(header.oper_no) + "】";
    auto append = [&](const std::string& text) {
        if (message.empty()) message = prefix + text;
        else message += text;
    };

    if (rules.is_name && trim_copy(header.name).empty()) append("姓名为空!");
    if (rules.is_sex &&
        (trim_copy(header.sex).empty() || trim_copy(header.sex) == "0"))
        append("性别为空!");
    if (rules.is_age && trim_copy(header.age).empty()) append("年龄为空!");
    if (rules.is_ks &&
        (trim_copy(header.dept_code).empty() || trim_copy(header.dept_code) == "0"))
        append("申请科室为空!");
    if (rules.is_reg && trim_copy(header.reg_no).empty()) append("病人号为空!");
    if (rules.is_txm && trim_copy(header.txm_no).empty()) append("条码为空!");

    bool all_empty = rules.all_r_k;
    bool one_empty = false;
    bool negative = false;
    bool zero = false;
    bool custom = false;
    for (const auto& entry : entries) {
        const std::string result = trim_copy(entry.result);
        if (rules.all_r_k && !result.empty()) all_empty = false;
        if (rules.one_r_k && result.empty()) one_empty = true;
        double value = 0.0;
        if (rules.r_0 && parse_audit_number(result, value) && value < 0.0) negative = true;
        if (rules.is_zero) {
            bool zero_text = result == "0";
            if (!zero_text && result.size() >= 3 && result[0] == '0' && result[1] == '.') {
                zero_text = result.find_first_not_of('0', 2) == std::string::npos;
            }
            if (zero_text && parse_audit_number(result, value) && value == 0.0) zero = true;
        }
        if (!rules.custom_value.empty() && result == rules.custom_value) custom = true;
    }
    if (rules.all_r_k && all_empty) append("结果为空!");
    if (rules.one_r_k && one_empty) append("存在结果为空!");
    if (rules.r_0 && negative) append("存在结果为负数!");
    if (rules.is_zero && zero) append("存在结果为0!");
    if (!rules.custom_value.empty() && custom)
        append("存在结果为指定值：" + rules.custom_value + "!");
    return message;
}

int AuditConfig::same_person_mode() const {
    const std::string value = trim(v4443);
    if (value == "2") return 2;
    if (value == "1") return 1;
    return 0;
}

bool audit_same_person(const AuditReportState& state, const std::string& auditor_id) {
    const std::string oper = trim_copy(state.oper_code);
    const std::string auditor = trim_copy(auditor_id);
    return !oper.empty() && !auditor.empty() && oper == auditor;
}

const wchar_t* audit_blocker_message(AuditBlocker blocker) {
    switch (blocker) {
        case AuditBlocker::none: return L"";
        case AuditBlocker::no_report: return L"未找到目标报告，请刷新后重试。";
        case AuditBlocker::invalid_rep_no: return L"缺少有效的报告号，无法审核。";
        case AuditBlocker::already_audited:
            return L"对不起，该报告已审核或已发送，不能继续审核，请刷新后重试！";
        case AuditBlocker::need_inspect: return L"当前检验单结果需要复查，无法审核。";
        case AuditBlocker::not_initial_review: return L"报告结果未进行初审，请保存后再审核！";
        case AuditBlocker::missing_name: return L"检验单姓名不能为空或病人信息未保存。";
        case AuditBlocker::empty_results: return L"空白检验单不能审核。";
        case AuditBlocker::no_permission: return L"对不起！您没有审核权限。";
        case AuditBlocker::barcode_forbidden: return L"当前设置不允许审核条码为空的报告。";
        case AuditBlocker::sex_age_missing: return L"审核时性别、年龄不能为空。";
        case AuditBlocker::same_person_blocked: return L"检验者与审核者相同，请修改审核者。";
    }
    return L"审核条件不满足。";
}

AuditBlocker evaluate_audit(const AuditConfig& config, const AuditReportState& state,
                            bool has_review_permission, const std::string& auditor_id,
                            const std::string& current_user_id) {
    if (trim_copy(state.rep_no).empty()) return AuditBlocker::invalid_rep_no;
    if (trim_copy(state.conf) != "F") return AuditBlocker::already_audited;
    if (trim_copy(state.need_inspect) == "1") return AuditBlocker::need_inspect;
    if (config.initial_review_required() && trim_copy(state.chk_flag) == "C")
        return AuditBlocker::not_initial_review;
    if (trim_copy(state.name).empty()) return AuditBlocker::missing_name;
    if (state.entry_count <= 0) return AuditBlocker::empty_results;
    if (!has_review_permission) return AuditBlocker::no_permission;
    if (config.barcode_empty_check() && trim_copy(state.txm_no).empty())
        return AuditBlocker::barcode_forbidden;
    if (config.sex_age_required() &&
        (trim_copy(state.sex).empty() || trim_copy(state.age).empty()))
        return AuditBlocker::sex_age_missing;
    // 同人检查（4443）由审核入口在“审核前规则”之后按 1=确认 / 2=拦截处理，
    // 以匹配原程序 IsPossibleToReport → 4141/4405/4507/4246/4527 → 4443 的顺序。
    (void)auditor_id;
    (void)current_user_id;
    return AuditBlocker::none;
}

bool audit_same_person_effective(const AuditReportState& state, const std::string& auditor_id,
                                 const std::string& current_user_id) {
    // 原程序在比较前会先把空的检验者回填为“当前登录人”（不是审核人），再与审核人比较。
    const std::string oper = trim_copy(state.oper_code);
    const bool tester_missing = oper.empty() || oper == "0";
    const std::string effective_oper = tester_missing ? trim_copy(current_user_id) : oper;
    const std::string auditor = trim_copy(auditor_id);
    return !effective_oper.empty() && !auditor.empty() && effective_oper == auditor;
}

std::vector<std::pair<std::string, std::string>> negative_results(
    const std::vector<AuditEntry>& entries) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& entry : entries) {
        if (entry.delete_bit != 0 && entry.delete_bit != 3) continue;
        double value = 0.0;
        if (parse_audit_number(entry.result, value) && value < 0.0)
            out.emplace_back(entry.item_name, trim_copy(entry.result));
    }
    return out;
}

namespace {

std::vector<std::string> split_audit_text(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(current);
    return parts;
}

bool parse_audit_int(const std::string& text, int& out) {
    const std::string value = trim_copy(text);
    if (value.empty()) return false;
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<int>(parsed);
    return true;
}

}  // namespace

std::string evaluate_item_audit_rules(
    const std::string& oper_no, const std::vector<ItemAuditRule>& rules,
    const std::vector<AuditEntry>& entries,
    const std::function<PreviousAuditResult(const std::string&)>& previous) {
    std::string message;
    const std::string sample = trim_copy(oper_no);
    for (const auto& entry : entries) {
        if (entry.delete_bit != 0) continue;
        const std::string result = trim_copy(entry.result);
        if (result.empty()) continue;
        const ItemAuditRule* rule = nullptr;
        for (const auto& candidate : rules) {
            if (trim_copy(candidate.item_code) == trim_copy(entry.item_code)) {
                rule = &candidate;
                break;
            }
        }
        if (!rule) continue;
        double value = 0.0;
        const bool result_numeric = parse_audit_number(result, value);

        const std::string gt = trim_copy(rule->greater_than);
        if (!gt.empty() && gt.find('[') == std::string::npos) {
            const bool inclusive = gt.find('=') != std::string::npos;
            std::string threshold_text = gt;
            threshold_text.erase(
                std::remove(threshold_text.begin(), threshold_text.end(), '='),
                threshold_text.end());
            double threshold = 0.0;
            if (result_numeric && parse_audit_number(threshold_text, threshold) &&
                (inclusive ? value >= threshold : value > threshold)) {
                message += "样本号：" + sample + " 【" + entry.item_name + "】结果" +
                           (inclusive ? "大于等于" : "大于") + gt + ";";
            }
        }

        const std::string lt = trim_copy(rule->less_than);
        if (!lt.empty() && lt.find('[') == std::string::npos) {
            const bool inclusive = lt.find('=') != std::string::npos;
            std::string threshold_text = lt;
            threshold_text.erase(
                std::remove(threshold_text.begin(), threshold_text.end(), '='),
                threshold_text.end());
            double threshold = 0.0;
            if (result_numeric && parse_audit_number(threshold_text, threshold) &&
                (inclusive ? value <= threshold : value < threshold)) {
                message += "样本号：" + sample + " 【" + entry.item_name + "】结果" +
                           (inclusive ? "小于等于" : "小于") + lt + ";";
            }
        }

        const std::string differ = trim_copy(rule->differ);
        if (!differ.empty()) {
            const auto parts = split_audit_text(differ, '|');
            int days = 0;
            double threshold = 0.0;
            if (parts.size() >= 2 && parse_audit_int(parts[0], days) &&
                parse_audit_number(parts[1], threshold)) {
                const auto prev = previous(entry.item_code);
                double prev_value = 0.0;
                if (prev.found && std::abs(prev.days_ago) > days && result_numeric &&
                    parse_audit_number(prev.result, prev_value) &&
                    value - prev_value > threshold) {
                    message += "样本号：" + sample + " 【" + entry.item_name + "】前后两次结果" +
                               std::to_string(days) + "天相差大于" + parts[1] + ";";
                }
            }
        }

        const std::string equally = trim_copy(rule->equally);
        if (!equally.empty()) {
            const auto prev = previous(entry.item_code);
            if (prev.found && trim_copy(prev.result) == result && result == equally) {
                message += "样本号：" + sample + " 【" + entry.item_name +
                           "】前后两次结果结果一致;";
            }
        }
    }
    return message;
}

AuditPlan build_audit_plan(const AuditConfig& config, const AuditReportState& state,
                           const std::string& auditor_id, const std::string& result_chk_name,
                           const std::string& current_user_id) {
    AuditPlan plan;
    const std::string rep_no = trim_copy(state.rep_no);
    const std::string txm_no = trim_copy(state.txm_no);
    const std::string mach_code = trim_copy(state.mach_code);
    const std::string type = trim_copy(state.type);

    // 1) 核心写回：CHK_FLAG='T'、CONF='T'、REP_TIME=服务器时间、DEAN_OPER/REP_OPER=审核人。
    //    在同一条 UPDATE 中复核审核状态及复查标记。
    plan.statements.push_back({
        "UPDATE LS_AS_REPORT SET CHK_FLAG='T', CONF='T', REP_TIME=GETDATE(), "
        "DEAN_OPER=?, REP_OPER=? WHERE REP_NO=? AND CONF='F' AND ISNULL(NeedInspect,0)<>1",
        {auditor_id, auditor_id, rep_no},
        true,
        false});

    // 2) 原单条路径：检验者姓名为空时回填“当前登录人”（不是审核人）。
    if (trim_copy(state.oper_code).empty() || trim_copy(state.oper_code) == "0") {
        plan.statements.push_back({
            "UPDATE LS_AS_REPORT SET OPER_CODE=? WHERE REP_NO=? "
            "AND (OPER_CODE IS NULL OR OPER_CODE=0)",
            {current_user_id, rep_no},
            false,
            false});
    }

    // 3) 结果表审核人姓名。
    plan.statements.push_back({
        "UPDATE LS_AS_RESULTP SET CHKNAME=? WHERE REP_NO=?",
        {result_chk_name, rep_no},
        false,
        false});

    // 4) 条码状态：审核未发送=2。按原程序区分空仪器 / 普通 / 体检分支。
    if (!txm_no.empty()) {
        if (mach_code.empty()) {
            plan.statements.push_back({
                "UPDATE ls_as_barcode SET oper_state=2 WHERE barcode=?",
                {txm_no},
                false,
                false});
        } else if (type != "3") {
            plan.statements.push_back({
                "UPDATE ls_as_barcode SET oper_state=2 WHERE barcode=? AND HOITEM_ID IN ("
                "SELECT ORDER_CODE FROM LS_AS_LABMATCH WHERE delete_bit=0 AND GROUP_CODE IN ("
                "SELECT GROUP_CODE FROM LS_AS_group WHERE mach_code=? AND DELETE_BIT=0 "
                "AND GROUP_CODE IN (SELECT DISTINCT GROUP_CODE FROM LS_AS_REPENTRY "
                "WHERE REP_NO=? AND DELETE_BIT=0)))",
                {txm_no, mach_code, rep_no},
                false,
                false});
        } else {
            plan.statements.push_back({
                "UPDATE ls_as_barcode SET oper_state=2 WHERE barcode=? AND (HOITEM_ID IN ("
                "SELECT GROUP_CODE FROM LS_AS_LABMATCH WHERE delete_bit=0 AND GROUP_CODE IN ("
                "SELECT GROUP_CODE FROM LS_AS_group WHERE mach_code=? AND DELETE_BIT=0 "
                "AND GROUP_CODE IN (SELECT DISTINCT GROUP_CODE FROM LS_AS_REPENTRY "
                "WHERE REP_NO=? AND DELETE_BIT=0))) OR ISNULL(HOITEM_ID,'')='')",
                {txm_no, mach_code, rep_no},
                false,
                false});
            plan.statements.push_back({
                "UPDATE ls_as_barcode SET oper_state=2 WHERE barcode=? AND delete_bit=0",
                {txm_no},
                false,
                true});
        }
    }

    // 5) TAT 审核时间（受 4147 控制）。
    if (config.tat_enabled() && !txm_no.empty()) {
        plan.statements.push_back({
            "UPDATE T_Lis_As_TATlog SET Auditing_Time=GETDATE(), State='审核' WHERE TXM_NO=?",
            {txm_no},
            false,
            false});
    }

    // 6) 4850：清理 TJ_Read_Flag（受参数控制）。
    if (config.clear_tj_read_flag()) {
        plan.statements.push_back({
            "UPDATE LS_AS_REPORT SET TJ_Read_Flag=0 WHERE DELETE_BIT=0 AND REP_NO=?",
            {rep_no},
            false,
            false});
        plan.statements.push_back({
            "UPDATE LS_AS_REPENTRY SET TJ_Read_Flag=0 WHERE DELETE_BIT=0 AND REP_NO=?",
            {rep_no},
            false,
            false});
    }

    return plan;
}

std::vector<CriticalValueHit> evaluate_critical_values(
    const std::vector<CriticalResultItem>& items,
    const std::vector<CriticalBoundCandidate>& bounds,
    const std::string& samp_code, const std::string& sex, const std::string& age,
    const std::string& age_unit, const std::string& dept_id, int string_mode) {
    std::vector<CriticalValueHit> hits;
    const std::string sample = trim_copy(samp_code);
    const std::string report_sex = trim_copy(sex);
    const std::string report_age = trim_copy(age);
    const std::string report_unit = trim_copy(age_unit);
    const std::string report_dept = trim_copy(dept_id);

    // 近似原 Get_Bound：项目/房间/样本过滤，科室优先，性别优先，年龄区间优先，其次“通用”。
    auto choose = [&](const CriticalResultItem& item) -> const CriticalBoundCandidate* {
        const CriticalBoundCandidate* best = nullptr;
        int best_rank = 99;
        for (const auto& bound : bounds) {
            if (trim_copy(bound.item_code) != trim_copy(item.item_code)) continue;
            const std::string bound_room = trim_copy(bound.room_code);
            const std::string item_room = trim_copy(item.room_code);
            if (!bound_room.empty() && !item_room.empty() && bound_room != item_room) continue;
            const std::string bound_samp = trim_copy(bound.samp_code);
            if (!bound_samp.empty() && !sample.empty() && bound_samp != sample) continue;

            const std::string bound_dept = trim_copy(bound.dept_id);
            int rank = 0;
            if (!report_dept.empty() && bound_dept == report_dept) rank += 0;
            else if (bound_dept.empty() || bound_dept == "0") rank += 10;
            else continue;

            const std::string bound_sex = trim_copy(bound.sex);
            if (!bound_sex.empty() && !report_sex.empty() && bound_sex == report_sex) rank += 0;
            else if (bound_sex == "通用" || bound_sex.empty()) rank += 20;
            else continue;

            const std::string bound_age_name = trim_copy(bound.age_name);
            bool age_match = false;
            if (report_unit.empty() || trim_copy(bound.age_unit) == report_unit ||
                trim_copy(bound.age_unit).empty()) {
                double age_value = 0.0, down = 0.0, up = 0.0;
                if (parse_audit_number(report_age, age_value) &&
                    parse_audit_number(bound.age_down, down) &&
                    parse_audit_number(bound.age_up, up) && age_value >= down && age_value <= up) {
                    age_match = true;
                }
            }
            if (age_match) rank += 0;
            else if (bound_age_name == "通用" || bound_age_name.empty()) rank += 30;
            else rank += 40;

            if (rank < best_rank) {
                best_rank = rank;
                best = &bound;
            }
        }
        return best;
    };

    for (const auto& item : items) {
        const std::string result = trim_copy(item.result);
        if (result.empty()) continue;
        const std::string type = trim_copy(item.type);
        // 原程序：type 1/2/3/5 检查；type 4 仅在 4247 != 0 时检查；其他跳过。
        if (!(type == "1" || type == "2" || type == "3" || type == "5" ||
              (type == "4" && string_mode != 0))) {
            continue;
        }
        const CriticalBoundCandidate* bound = choose(item);
        if (!bound) continue;
        const std::string lower = trim_copy(bound->lower);
        const std::string upper = trim_copy(bound->upper);
        if (lower.empty() && upper.empty()) continue;

        bool hit = false;
        double value = 0.0;
        if (parse_audit_number(result, value)) {
            double low = 0.0;
            double high = 0.0;
            if (!lower.empty() && lower != "0" && parse_audit_number(lower, low) && value <= low)
                hit = true;
            if (!upper.empty() && upper != "0" && parse_audit_number(upper, high) && value >= high)
                hit = true;
        }
        if (!hit && string_mode != 0) {
            auto matches = [&](const std::string& list) {
                for (const auto& token : split_audit_text(list, ',')) {
                    const std::string text = trim_copy(token);
                    if (text.empty()) continue;
                    if (string_mode == 1 && result == text) return true;
                    if (string_mode == 2 && result.find(text) != std::string::npos) return true;
                    if (string_mode == 4 && result != text) return true;
                }
                return false;
            };
            hit = matches(lower) || matches(upper);
        }
        if (hit) hits.push_back({item.item_name, result, lower, upper});
    }
    return hits;
}

}  // namespace search
