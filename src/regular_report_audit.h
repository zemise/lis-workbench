#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace search {

// 常规报告单条“审核”的纯逻辑层：配置口径、前置判定、写回 SQL 计划。
// 不依赖 Win32/ODBC，便于单元测试；实际执行在 search_core.cpp 的 execute_audit。
//
// 首版范围（2026-11-20 与用户确认）：
// - 只做单条审核，CONF 固定 'T'（审核未发送），不做 'S' 发送链路；
// - 严格复刻原程序的“审核即改写检验者”：REP_OPER 一律=审核人，
//   OPER_CODE 仅在检验者为空时回填（原 Frmjybg.cs btconfirm_Click / Frm_adituing.cs CheckData）；
// - 保留原程序 T 路径的三项副作用：LS_AS_RESULTP.CHKNAME、LS_AS_BARCODE.OPER_STATE=2、
//   T_Lis_As_TATlog 审核时间（受 4147 控制），另含 4850 的 TJ_Read_Flag 清理；
// - 审核权限 isreview、同人规则 4443、初审 4245、条码为空 4460、性别年龄 4778 按原程序的开关执行；
// - 外部联动（体检接口、CA、费用、迪图、打印/PDF、Oracle 回写 4415、医生消息）不纳入首版。

struct AuditConfig {
    std::string v4147;  // 检验时间(TAT)日志开关
    std::string v4443;  // 同人规则：1=确认，2=拦截
    std::string v4245;  // 初审：CHK_FLAG='C' 时是否拦截
    std::string v4460;  // 条码为空时的审核限制
    std::string v4778;  // 审核时性别/年龄必填
    std::string v4850;  // 审核后清理 TJ_Read_Flag
    std::string v4149;  // 审核人替换开关（T_LIS_AS_AuditMagMan）
    std::string v4110;  // 默认审核人（"0"=当前登录人）
    std::string v4141;  // 自定义审核规则开关（T_ts_ls_CustomAuditRules）
    std::string v4453;  // 自定义规则命中：1=直接拦截，否则确认
    std::string v4246;  // 项目阈值规则（T_LIS_ITEM_AUDIT_RULE）
    std::string v4405;  // 负数结果确认
    std::string v4507;  // 审核二次确认
    std::string v4527;  // 医嘱项目数核对
    std::string v4222;  // 危急值报告必须已发送危机值消息
    std::string v4242;  // 危急值登记（阶段三）
    std::string v4247;  // 字符串危急值匹配模式
    std::string v4804;  // 单条审核前危急值检查

    bool tat_enabled() const;
    bool initial_review_required() const;
    bool barcode_empty_check() const;
    bool sex_age_required() const;
    bool clear_tj_read_flag() const;
    bool reviewer_mapping_enabled() const;  // 4149=1
    bool custom_rules_enabled() const;      // 4141 首字符=1
    bool custom_rules_hard_block() const;   // 4453=1
    bool item_audit_rules_enabled() const;  // 4246=1
    bool negative_result_confirm() const;   // 4405=1
    bool audit_confirm() const;             // 4507=1
    bool item_count_check_enabled() const;  // 4527=1
    bool crisis_message_required() const;   // 4222=1
    bool critical_value_check_enabled() const;  // 4804=1 或 4242=1
    int string_critical_mode() const;           // 4247: 0/1/2/4
    int same_person_mode() const;           // 0=不检查，1=确认，2=拦截
};

struct AuditReportState {
    std::string rep_no;
    std::string txm_no;
    std::string oper_no;
    std::string name;
    std::string sex;
    std::string age;
    std::string conf;
    std::string chk_flag;
    std::string need_inspect;
    std::string oper_code;
    std::string mach_code;
    std::string group_code;
    std::string type;
    std::string reg_no;
    std::string samp_code;
    std::string dept_code;
    int entry_count = 0;
    int result_count = 0;
};

enum class AuditBlocker {
    none = 0,
    no_report,
    invalid_rep_no,
    already_audited,
    need_inspect,
    not_initial_review,
    missing_name,
    empty_results,
    no_permission,
    barcode_forbidden,
    sex_age_missing,
    same_person_blocked,
};

const wchar_t* audit_blocker_message(AuditBlocker blocker);

bool audit_same_person(const AuditReportState& state, const std::string& auditor_id);

struct AuditReviewer {
    std::string employee_id;
    std::string name;
};

// 返回 none 表示可以进入写回；same_person_mode()==1 时按“无二次确认”直接放行，
// 仅记录日志（用户已确认不做二次确认弹窗）。
// current_user_id 用于复刻原程序“检验者缺失时先回填当前人、再与审核人比较”的语义。
AuditBlocker evaluate_audit(const AuditConfig& config, const AuditReportState& state,
                            bool has_review_permission, const std::string& auditor_id,
                            const std::string& current_user_id);

// 自定义审核规则（原 Form_CustomAuditRules.AutoRules 的等价实现，按仪器配置）。
struct CustomAuditRules {
    bool is_rule = false;
    bool all_r_k = false;   // 结果全部为空
    bool one_r_k = false;   // 存在结果为空
    bool r_0 = false;       // 存在负结果
    bool r_js = false;      // 包含 TYPE=2 的项目（否则查询时排除）
    bool is_zero = false;   // 存在结果为 0
    std::string custom_value;  // 存在指定结果值
    bool is_name = false;
    bool is_sex = false;
    bool is_age = false;
    bool is_ks = false;
    bool is_reg = false;
    bool is_txm = false;
};

struct CustomAuditHeader {
    std::string name;
    std::string sex;
    std::string age;
    std::string dept_code;
    std::string reg_no;
    std::string txm_no;
    std::string oper_no;
};

struct CustomAuditEntry {
    std::string result;
    std::string item_name;
    std::string type;
};

// 返回空串表示无规则命中；命中时返回与原程序一致的提示文本。
std::string evaluate_custom_audit_rules(const CustomAuditRules& rules,
                                        const CustomAuditHeader& header,
                                        const std::vector<CustomAuditEntry>& entries);

// 单条审核前检查用到的结果项。
struct AuditEntry {
    std::string result;
    std::string item_name;
    std::string item_code;
    int delete_bit = 0;
};

struct ItemAuditRule {
    std::string item_code;
    std::string greater_than;
    std::string less_than;
    std::string differ;
    std::string equally;
};

struct PreviousAuditResult {
    bool found = false;
    std::string result;
    int days_ago = 0;
};

// 4405：返回存在负数结果的项目（item_name, result）。
std::vector<std::pair<std::string, std::string>> negative_results(
    const std::vector<AuditEntry>& entries);

// 4443：有效检验者（缺失时按当前登录人）与审核人是否相同。
bool audit_same_person_effective(const AuditReportState& state,
                                 const std::string& auditor_id,
                                 const std::string& current_user_id);

// 4246：按项目阈值规则生成提示文本；previous 用于 DIFFER/EQUALLY 查询上次结果。
std::string evaluate_item_audit_rules(
    const std::string& oper_no, const std::vector<ItemAuditRule>& rules,
    const std::vector<AuditEntry>& entries,
    const std::function<PreviousAuditResult(const std::string& item_code)>& previous);

// 阶段一：危急值检测（只读）。lower=upbound1（原程序称危急下限），upper=dnbound1（危急上限）。
struct CriticalBoundCandidate {
    std::string item_code;
    std::string room_code;
    std::string samp_code;
    std::string sex;
    std::string age_unit;
    std::string age_down;
    std::string age_up;
    std::string age_name;
    std::string dept_id;
    std::string lower;
    std::string upper;
};

struct CriticalResultItem {
    std::string item_code;
    std::string item_name;
    std::string result;
    std::string type;
    std::string room_code;
};

struct CriticalValueHit {
    std::string item_name;
    std::string result;
    std::string lower;
    std::string upper;
};

std::vector<CriticalValueHit> evaluate_critical_values(
    const std::vector<CriticalResultItem>& items,
    const std::vector<CriticalBoundCandidate>& bounds,
    const std::string& samp_code, const std::string& sex, const std::string& age,
    const std::string& age_unit, const std::string& dept_id, int string_mode);

struct AuditStatement {
    std::string sql;
    std::vector<std::string> params;
    bool core = false;             // 核心 UPDATE：影响行数=0 视为已被他人审核
    bool fallback_if_zero = false; // 仅当前一条语句影响行数为 0 时执行（体检条码兜底）
};

struct AuditPlan {
    std::vector<AuditStatement> statements;
};

// result_chk_name 对应原程序 ReportService.UpdateResultTp(text2, _currentUser.Name)：
// 结果表 CHKNAME 用“当前登录人姓名”，不是映射后的审核人姓名。
AuditPlan build_audit_plan(const AuditConfig& config, const AuditReportState& state,
                           const std::string& auditor_id, const std::string& result_chk_name,
                           const std::string& current_user_id);

}  // namespace search
