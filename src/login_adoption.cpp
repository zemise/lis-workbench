#include "login_adoption.h"
#ifdef _WIN32
#include "login_database.h"
#include "login_source_win32.h"

namespace login {

Result adopt_source(const search::DbSettings& settings, const std::wstring& directory,
                    const Source& source, const std::function<bool()>& cancelled,
                    bool require_enabled, bool require_unique_instance) {
    auto stopped = [&] { return cancelled && cancelled(); };
    auto initial = scan_sources(directory, cancelled);
    if (require_unique_instance && (initial.mains.size() != 1 || initial.sources.size() != 1))
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
    if (require_unique_instance && (final.mains.size() != 1 || final.sources.size() != 1))
        return {};

    auto after = match_source(final, source);
    if (stopped() || after.status != SourceStatus::ready ||
        !same_observation(*after.source, *before.source) ||
        (require_enabled && !after.source->main_enabled))
        return {};

    result.identity->adopted_source = *after.source;
    return result;
}

std::vector<Recommendation> recommendations(
    const search::DbSettings& settings, const std::wstring& directory,
    const std::function<bool()>& cancelled) {
    std::vector<Recommendation> result;
    auto scan = scan_sources(directory, cancelled);
    if (!scan.complete) return result;

    for (const auto& source : scan.sources) {
        if (cancelled && cancelled()) return {};
        if (!source.main_enabled || match_source(scan, source).status != SourceStatus::ready)
            continue;
        auto account = validate_source_account(settings, source, cancelled);
        if (account.status == Status::success && account.identity) {
            result.push_back({source, std::move(*account.identity),
                              scan.mains.size() == 1 && scan.sources.size() == 1});
        }
    }

    // Publish only candidates still associated with the same observation.
    if (result.empty()) return result;
    auto after = scan_sources(directory, cancelled);
    std::vector<Recommendation> stable;
    for (auto& recommendation : result) {
        auto match = match_source(after, recommendation.source);
        if (match.status == SourceStatus::ready && match.source->main_enabled &&
            same_observation(*match.source, recommendation.source)) {
            recommendation.automatic_allowed = recommendation.automatic_allowed &&
                after.mains.size() == 1 && after.sources.size() == 1;
            stable.push_back(std::move(recommendation));
        }
    }
    return stable;
}

} // namespace login
#endif
