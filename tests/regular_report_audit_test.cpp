#include "regular_report_audit.h"

#include <iostream>
#include <string>

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            std::cerr << "check failed line " << __LINE__ << ": " #x "\n";    \
            return 1;                                                          \
        }                                                                      \
    } while (false)

namespace {

search::AuditReportState base_state() {
    search::AuditReportState state;
    state.rep_no = "1001";
    state.txm_no = "B001";
    state.oper_no = "012";
    state.name = "患者";
    state.sex = "男";
    state.age = "30";
    state.conf = "F";
    state.chk_flag = "F";
    state.need_inspect = "0";
    state.oper_code = "101";
    state.mach_code = "626";
    state.group_code = "1";
    state.type = "1";
    state.entry_count = 3;
    state.result_count = 3;
    return state;
}

search::AuditConfig base_config() {
    search::AuditConfig config;
    config.v4147 = "1";
    config.v4443 = "2";
    config.v4245 = "1";
    config.v4460 = "1";
    config.v4778 = "1";
    config.v4850 = "1";
    return config;
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    using search::AuditBlocker;
    using search::AuditConfig;
    using search::AuditReportState;

    // 配置口径
    {
        AuditConfig config = base_config();
        CHECK(config.tat_enabled());
        CHECK(config.initial_review_required());
        CHECK(config.barcode_empty_check());
        CHECK(config.sex_age_required());
        CHECK(config.clear_tj_read_flag());
        CHECK(config.same_person_mode() == 2);
        config.v4443 = "1";
        CHECK(config.same_person_mode() == 1);
        config.v4443 = "0";
        CHECK(config.same_person_mode() == 0);
        config.v4850 = "0";
        CHECK(!config.clear_tj_read_flag());
        config.v4850 = "1|备用";
        CHECK(config.clear_tj_read_flag());
    }

    // 同人判断
    {
        AuditReportState state = base_state();
        CHECK(search::audit_same_person(state, "101"));
        CHECK(search::audit_same_person(state, " 101 "));
        CHECK(!search::audit_same_person(state, "102"));
        state.oper_code.clear();
        CHECK(!search::audit_same_person(state, "101"));
    }

    // 前置判定
    {
        AuditConfig config = base_config();
        AuditReportState state = base_state();
        CHECK(search::evaluate_audit(config, state, true, "200", "101") == AuditBlocker::none);

        state.conf = "T";
        CHECK(search::evaluate_audit(config, state, true, "200", "101") == AuditBlocker::already_audited);
        state.conf = "F";

        state.need_inspect = "1";
        CHECK(search::evaluate_audit(config, state, true, "200", "101") == AuditBlocker::need_inspect);
        state.need_inspect = "0";

        state.chk_flag = "C";
        CHECK(search::evaluate_audit(config, state, true, "200", "101") == AuditBlocker::not_initial_review);
        state.chk_flag = "F";

        state.name.clear();
        CHECK(search::evaluate_audit(config, state, true, "200", "101") == AuditBlocker::missing_name);
        state.name = "患者";

        state.entry_count = 0;
        CHECK(search::evaluate_audit(config, state, true, "200", "101") == AuditBlocker::empty_results);
        state.entry_count = 3;

        CHECK(search::evaluate_audit(config, state, false, "200", "101") == AuditBlocker::no_permission);

        state.txm_no.clear();
        CHECK(search::evaluate_audit(config, state, true, "200", "101") == AuditBlocker::barcode_forbidden);
        state.txm_no = "B001";

        state.sex.clear();
        CHECK(search::evaluate_audit(config, state, true, "200", "101") == AuditBlocker::sex_age_missing);
        state.sex = "男";

        // 同人判断在审核入口按 4443 处理（2=拦截，1=确认），evaluate_audit 只做基础校验
        CHECK(search::audit_same_person_effective(state, "101", "101"));
        CHECK(!search::audit_same_person_effective(state, "200", "101"));
        AuditReportState missing_oper = state;
        missing_oper.oper_code.clear();
        CHECK(search::audit_same_person_effective(missing_oper, "101", "101"));
        CHECK(!search::audit_same_person_effective(missing_oper, "200", "101"));
        CHECK(search::evaluate_audit(config, state, true, "101", "101") == AuditBlocker::none);
    }

    // 参数构造：核心写回 + 副作用（检验者为空 -> 带 OPER_CODE 回填）
    {
        AuditConfig config = base_config();
        AuditReportState state = base_state();
        state.oper_code.clear();
        const search::AuditPlan plan =
            search::build_audit_plan(config, state, "200", "审核员", "101");

        CHECK(plan.statements.size() == 7);  // core, oper, result, barcode, tat, tj*2
        const auto& core = plan.statements[0];
        CHECK(core.core);
        CHECK(contains(core.sql, "CHK_FLAG='T'"));
        CHECK(contains(core.sql, "CONF='T'"));
        CHECK(contains(core.sql, "REP_TIME=GETDATE()"));
        CHECK(contains(core.sql, "DEAN_OPER=?"));
        CHECK(contains(core.sql, "REP_OPER=?"));
        CHECK(contains(core.sql, "WHERE REP_NO=? AND CONF='F' AND ISNULL(NeedInspect,0)<>1"));
        CHECK(core.params.size() == 3);
        CHECK(core.params[0] == "200");
        CHECK(core.params[1] == "200");
        CHECK(core.params[2] == "1001");

        CHECK(contains(plan.statements[1].sql, "SET OPER_CODE=?"));
        CHECK(plan.statements[1].params[0] == "101");
        CHECK(contains(plan.statements[2].sql, "SET CHKNAME=?"));
        CHECK(plan.statements[2].params[0] == "审核员");

        CHECK(contains(plan.statements[3].sql, "oper_state=2"));
        CHECK(contains(plan.statements[3].sql, "HOITEM_ID IN"));
        CHECK(plan.statements[3].params.size() == 3);
        CHECK(plan.statements[3].params[2] == "1001");

        CHECK(contains(plan.statements[4].sql, "T_Lis_As_TATlog"));
        CHECK(contains(plan.statements[4].sql, "State='审核'"));
        CHECK(plan.statements[4].params[0] == "B001");

        CHECK(contains(plan.statements[5].sql, "TJ_Read_Flag=0"));
        CHECK(plan.statements[6].sql.find("LS_AS_REPENTRY") != std::string::npos);
    }

    // 检验者已存在时不回填 OPER_CODE
    {
        AuditConfig config = base_config();
        AuditReportState state = base_state();
        state.oper_code = "101";
        const auto plan = search::build_audit_plan(config, state, "200", "审核员", "101");
        CHECK(plan.statements.size() == 6);
        CHECK(plan.statements[0].core);
        CHECK(contains(plan.statements[1].sql, "SET CHKNAME=?"));
    }

    // 空仪器：条码兜底按 barcode 直接更新
    {
        AuditConfig config = base_config();
        AuditReportState state = base_state();
        state.mach_code.clear();
        const auto plan = search::build_audit_plan(config, state, "200", "审核员", "101");
        bool found = false;
        for (const auto& st : plan.statements) {
            if (contains(st.sql, "oper_state=2") && contains(st.sql, "WHERE barcode=?")) {
                found = true;
                CHECK(st.params.size() == 1);
                CHECK(st.params[0] == "B001");
            }
        }
        CHECK(found);
    }

    // 体检(type=3)：先按组更新，再兜底
    {
        AuditConfig config = base_config();
        AuditReportState state = base_state();
        state.type = "3";
        const auto plan = search::build_audit_plan(config, state, "200", "审核员", "101");
        bool fallback = false;
        for (const auto& st : plan.statements) {
            if (st.fallback_if_zero) {
                fallback = true;
                CHECK(contains(st.sql, "delete_bit=0"));
                CHECK(st.params.size() == 1);
            }
        }
        CHECK(fallback);
    }

    // 4147=0：不写 TAT；4850=0：不写 TJ_Read_Flag
    {
        AuditConfig config = base_config();
        config.v4147 = "0";
        config.v4850 = "0";
        AuditReportState state = base_state();
        const auto plan = search::build_audit_plan(config, state, "200", "审核员", "101");
        CHECK(plan.statements.size() == 3);  // core, result, barcode
        for (const auto& st : plan.statements) {
            CHECK(!contains(st.sql, "T_Lis_As_TATlog"));
            CHECK(!contains(st.sql, "TJ_Read_Flag"));
        }
    }

    // 提示文案不为空
    {
        for (AuditBlocker blocker :
             {AuditBlocker::already_audited, AuditBlocker::no_permission,
              AuditBlocker::same_person_blocked, AuditBlocker::empty_results}) {
            const wchar_t* message = search::audit_blocker_message(blocker);
            CHECK(message != nullptr && message[0] != L'\0');
        }
    }

    // 自定义审核规则（4141 + T_ts_ls_CustomAuditRules）
    {
        search::CustomAuditHeader header;
        header.oper_no = "901";
        header.name = "患者";
        header.sex = "1";
        header.age = "30";
        header.dept_code = "102";
        header.reg_no = "R1";
        header.txm_no = "B1";

        search::CustomAuditEntry empty_entry;
        search::CustomAuditEntry value_entry;
        value_entry.result = "12";
        search::CustomAuditEntry negative_entry;
        negative_entry.result = "-1";
        search::CustomAuditEntry zero_entry;
        zero_entry.result = "0";

        search::CustomAuditRules rules;
        rules.is_rule = true;
        rules.one_r_k = true;
        CHECK(!search::evaluate_custom_audit_rules(rules, header, {empty_entry}).empty());
        CHECK(search::evaluate_custom_audit_rules(rules, header, {value_entry}).empty());
        CHECK(search::evaluate_custom_audit_rules(rules, header, {value_entry, empty_entry})
                  .find("存在结果为空!") != std::string::npos);

        search::CustomAuditRules all_rules = rules;
        all_rules.one_r_k = false;
        all_rules.all_r_k = true;
        CHECK(!search::evaluate_custom_audit_rules(
                   all_rules, header, {empty_entry, empty_entry}).empty());
        CHECK(search::evaluate_custom_audit_rules(all_rules, header, {value_entry}).empty());

        search::CustomAuditRules negative_rules;
        negative_rules.is_rule = true;
        negative_rules.r_0 = true;
        CHECK(search::evaluate_custom_audit_rules(negative_rules, header, {negative_entry})
                  .find("存在结果为负数!") != std::string::npos);

        search::CustomAuditRules zero_rules;
        zero_rules.is_rule = true;
        zero_rules.is_zero = true;
        CHECK(search::evaluate_custom_audit_rules(zero_rules, header, {zero_entry})
                  .find("存在结果为0!") != std::string::npos);

        search::CustomAuditRules custom_rules;
        custom_rules.is_rule = true;
        custom_rules.custom_value = "ALB<25";
        search::CustomAuditEntry custom_entry;
        custom_entry.result = "ALB<25";
        CHECK(search::evaluate_custom_audit_rules(custom_rules, header, {custom_entry})
                  .find("指定值") != std::string::npos);

        search::CustomAuditRules field_rules;
        field_rules.is_rule = true;
        field_rules.is_name = true;
        search::CustomAuditHeader empty_header = header;
        empty_header.name.clear();
        CHECK(!search::evaluate_custom_audit_rules(field_rules, empty_header, {}).empty());

        search::CustomAuditRules none;
        CHECK(search::evaluate_custom_audit_rules(none, header, {empty_entry}).empty());
    }

    // 4405 负数结果
    {
        std::vector<search::AuditEntry> entries(3);
        entries[0].result = "-1";
        entries[0].item_name = "ALT";
        entries[0].delete_bit = 0;
        entries[1].result = "5";
        entries[1].item_name = "AST";
        entries[1].delete_bit = 0;
        entries[2].result = "-2";
        entries[2].item_name = "ALP";
        entries[2].delete_bit = 1;
        const auto negatives = search::negative_results(entries);
        CHECK(negatives.size() == 1);
        CHECK(negatives[0].first == "ALT");
        CHECK(negatives[0].second == "-1");
    }

    // 4246 项目阈值规则
    {
        search::AuditEntry entry;
        entry.result = "12";
        entry.item_name = "ALT";
        entry.item_code = "1";
        entry.delete_bit = 0;

        search::ItemAuditRule rule;
        rule.item_code = "1";
        rule.greater_than = "10";
        const auto no_prev = [](const std::string&) { return search::PreviousAuditResult{}; };
        CHECK(search::evaluate_item_audit_rules("901", {rule}, {entry}, no_prev)
                  .find("结果大于10") != std::string::npos);

        search::AuditEntry safe = entry;
        safe.result = "5";
        CHECK(search::evaluate_item_audit_rules("901", {rule}, {safe}, no_prev).empty());

        search::ItemAuditRule less_rule;
        less_rule.item_code = "1";
        less_rule.less_than = "3";
        CHECK(search::evaluate_item_audit_rules("901", {less_rule}, {safe}, no_prev).empty());

        search::ItemAuditRule gt_equal;
        gt_equal.item_code = "1";
        gt_equal.greater_than = "=10";
        CHECK(search::evaluate_item_audit_rules("901", {gt_equal}, {entry}, no_prev)
                  .find("结果大于等于") != std::string::npos);

        search::ItemAuditRule differ_rule;
        differ_rule.item_code = "1";
        differ_rule.differ = "3|5";
        const auto prev_found = [](const std::string&) {
            search::PreviousAuditResult prev;
            prev.found = true;
            prev.result = "2";
            prev.days_ago = 5;
            return prev;
        };
        CHECK(search::evaluate_item_audit_rules("901", {differ_rule}, {entry}, prev_found)
                  .find("前后两次结果3天相差大于5") != std::string::npos);

        search::ItemAuditRule equally_rule;
        equally_rule.item_code = "1";
        equally_rule.equally = "12";
        const auto prev_same = [](const std::string&) {
            search::PreviousAuditResult prev;
            prev.found = true;
            prev.result = "12";
            return prev;
        };
        CHECK(search::evaluate_item_audit_rules("901", {equally_rule}, {entry}, prev_same)
                  .find("前后两次结果结果一致") != std::string::npos);
    }

    // 阶段一：危急值检测（lower=upbound1，upper=dnbound1）
    {
        search::CriticalResultItem item;
        item.item_code = "1";
        item.item_name = "K";
        item.result = "7.5";
        item.type = "1";
        item.room_code = "1";

        search::CriticalBoundCandidate bound;
        bound.item_code = "1";
        bound.room_code = "1";
        bound.samp_code = "1";
        bound.sex = "通用";
        bound.age_unit = "岁";
        bound.age_down = "0";
        bound.age_up = "0";
        bound.age_name = "通用";
        bound.dept_id = "0";
        bound.lower = "3.5";
        bound.upper = "6.0";

        const auto hits = search::evaluate_critical_values(
            {item}, {bound}, "1", "男", "30", "岁", "102", 2);
        CHECK(hits.size() == 1);
        CHECK(hits[0].item_name == "K");
        CHECK(hits[0].lower == "3.5");
        CHECK(hits[0].upper == "6.0");

        item.result = "5.0";
        CHECK(search::evaluate_critical_values({item}, {bound}, "1", "男", "30", "岁", "102", 2)
                  .empty());

        search::CriticalResultItem str_item;
        str_item.item_code = "2";
        str_item.item_name = "HBV";
        str_item.result = "阳性";
        str_item.type = "3";
        search::CriticalBoundCandidate str_bound;
        str_bound.item_code = "2";
        str_bound.samp_code = "1";
        str_bound.sex = "通用";
        str_bound.age_name = "通用";
        str_bound.dept_id = "0";
        str_bound.lower = "阳性";
        CHECK(search::evaluate_critical_values({str_item}, {str_bound}, "1", "男", "30", "岁",
                                               "102", 2)
                  .size() == 1);
        CHECK(search::evaluate_critical_values({str_item}, {str_bound}, "1", "男", "30", "岁",
                                               "102", 0)
                  .empty());
    }

    std::cout << "regular report audit logic passed\n";
    return 0;
}
