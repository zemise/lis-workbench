#pragma once

#ifdef _WIN32

#include "scheduled_result_check_core.h"

#include <string>
#include <vector>

namespace scheduled_check {

struct Alert : Match {
  int id = 0;
  bool handled = false;
  std::string discovered_at;
};

bool ensure_store(std::string &error);
bool load_rules(std::vector<Rule> &rows, std::string &error);
bool save_rule(Rule &row, std::string &error);
bool delete_rule(int id, std::string &error);
bool set_rule_enabled(int id, bool enabled, std::string &error);
bool record_matches(const std::vector<Rule> &rules,
                    const std::vector<Match> &matches,
                    std::vector<Alert> &newly_alerted, std::string &error);
bool load_review_alerts(std::vector<Alert> &rows, std::string &error);
bool load_unhandled_alerts(std::vector<Alert> &rows, std::string &error);
bool set_alert_handled(int id, bool handled, std::string &error);

} // namespace scheduled_check

#endif
