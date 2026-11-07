#include "login_adoption.h"
#ifdef _WIN32
#include "login_database.h"
#include "login_source_win32.h"
#include "log.h"
#include "search_text.h"
#include <windows.h>

namespace login {

Result adopt_source(const search::DbSettings &settings, const std::wstring &directory,
                    const Source &source, const std::function<bool()> &cancelled,
                    bool require_enabled, bool require_unique_instance) {
    auto stopped = [&] { return cancelled && cancelled(); };
    auto initial = scan_sources(directory, cancelled);
    if (require_unique_instance && (initial.mains.size() != 1 || initial.sources.size() != 1 ||
                                    !initial.unreadable_mains.empty()))
        return {};

    auto before = match_source(initial, source);
    if (stopped() || before.status != SourceStatus::ready ||
        !same_observation(*before.source, source) ||
        (require_enabled && !before.source->main_enabled))
        return {};

    auto result = validate_source_account(settings, *before.source, cancelled);
    if (result.status != Status::success || !result.identity || stopped())
        return {};

    auto final = scan_sources(directory, cancelled);
    if (require_unique_instance &&
        (final.mains.size() != 1 || final.sources.size() != 1 || !final.unreadable_mains.empty()))
        return {};

    auto after = match_source(final, source);
    if (stopped() || after.status != SourceStatus::ready ||
        !same_observation(*after.source, *before.source) ||
        (require_enabled && !after.source->main_enabled))
        return {};

    result.identity->adopted_source = *after.source;
    return result;
}

std::vector<Recommendation> recommendations(const search::DbSettings &settings,
                                            const std::wstring &directory,
                                            const std::function<bool()> &cancelled,
                                            std::wstring *diagnostic) {
    if (diagnostic)
        diagnostic->clear();
    auto explain = [&](const std::wstring &text) {
        if (diagnostic && diagnostic->empty())
            *diagnostic = text;
    };
    auto explainScan = [&](const SourceScan &scan) {
        if (!scan.diagnostics.empty())
            explain(scan.diagnostics.front());
        else if (!scan.complete)
            explain(L"来源扫描未完成，请刷新");
    };
    std::vector<Recommendation> result;
    auto scan = scan_sources(directory, cancelled);
    // Retry once for a helper/window still starting. Never retry an unknown
    // file version or relax identity checks. Wait remains cancellable.
    if (scan.complete && scan.sources.empty() && scan.retryable) {
        for (int i = 0; i < 10; ++i) {
            if (cancelled && cancelled())
                return {};
            Sleep(100);
        }
        scan = scan_sources(directory, cancelled);
    }
    if (!scan.complete) {
        explainScan(scan);
        return result;
    }

    for (const auto &source : scan.sources) {
        if (cancelled && cancelled())
            return {};
        auto match = match_source(scan, source);
        if (!source.main_enabled || match.status != SourceStatus::ready) {
            explain(match.status == SourceStatus::locked ? L"来源程序已锁屏，请先解锁"
                    : !source.main_enabled ? L"来源主窗口暂不可用，请关闭其对话框后刷新"
                                           : L"登录来源存在冲突或无法核实");
            continue;
        }
        auto account = validate_source_account(settings, source, cancelled);
        if (account.status == Status::success && account.identity) {
            result.push_back({source, std::move(*account.identity),
                              scan.mains.size() == 1 && scan.sources.size() == 1 &&
                                  scan.unreadable_mains.empty()});
        } else {
            explain(!account.diagnostic.empty()        ? account.diagnostic
                    : account.status == Status::failed ? L"来源账号或数据库在线状态无法核实"
                                                       : message(account.status));
            LOG_WARN(
                "Login source account validation failed pid=" + std::to_string(source.main_pid) +
                " status=" + std::to_string(int(account.status)) +
                " reason=" + search::wide_to_utf8(account.diagnostic));
        }
    }

    // Publish only candidates still associated with the same observation.
    if (result.empty()) {
        explainScan(scan);
        return result;
    }
    auto after = scan_sources(directory, cancelled);
    std::vector<Recommendation> stable;
    for (auto &recommendation : result) {
        auto match = match_source(after, recommendation.source);
        if (match.status == SourceStatus::ready && match.source->main_enabled &&
            same_observation(*match.source, recommendation.source)) {
            recommendation.automatic_allowed =
                recommendation.automatic_allowed && after.mains.size() == 1 &&
                after.sources.size() == 1 && after.unreadable_mains.empty();
            stable.push_back(std::move(recommendation));
        }
    }
    if (stable.empty()) {
        explain(L"登录来源在核实期间发生变化，请刷新");
        explainScan(after);
    } else if (diagnostic)
        diagnostic->clear();
    return stable;
}

} // namespace login
#endif
