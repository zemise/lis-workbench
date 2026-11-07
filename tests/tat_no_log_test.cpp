#include "query_diagnostics.h"
#include "search_core.h"
#include "log.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
namespace {
std::atomic<int> writes{0};
int callbacks = 0;
void check(bool ok, const char *message) {
    if (!ok) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
} // namespace
// Link-time recorder: test observes actual LOG_* calls, never creates a log file.
namespace applog {
void write(Level, const char *, int, const std::string &) { ++writes; }
} // namespace applog
int main() {
    std::string error;
    {
        querydiag::SilenceScope silence;
        querydiag::RequestScope outer("tat", error, [](const auto &) { ++callbacks; });
        outer.event("event=execute");
        {
            querydiag::RequestScope nested("dictionary", error, outer.log_callback());
            nested.failure("DB_READ", "read", "diagnostic=unavailable");
            querydiag::stage_event(querydiag::Operation::fetch);
        }
        check(writes == 0 && callbacks == 0, "silent scopes include nested failures");
        std::thread other([] {
            std::string e;
            querydiag::RequestScope normal("other-page", e);
        });
        other.join();
        check(writes == 2, "silencing is thread-local");
    }
    writes = 0;
    search::TatStatQuery q;
    q.start_time = "2026-09-01";
    q.end_time = "2026-09-02";
    search::TatStatSummary summary;
    std::vector<search::TatStatDetailRow> rows;
    check(!search::query_tat_statistics(q, summary, rows, error, [](const auto &) { ++callbacks; }),
          "no connection configured");
    check(writes == 0 && callbacks == 0, "TAT failure must not log or invoke supplied sink");
    search::TatPickerOptions options;
    check(!search::query_tat_picker_options("", options, error), "no picker connection configured");
    check(writes == 0, "picker failures must not log");
    q.start_time = "invalid";
    check(!search::query_tat_statistics(q, summary, rows, error), "invalid input");
    check(writes == 0, "validation failure must not log");
    {
        querydiag::RequestScope normal("normal", error);
    }
    check(writes == 2, "logging restored after silent TAT request");
    std::cout << "tat no-log tests passed\n";
}
