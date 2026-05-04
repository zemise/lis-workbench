#include "app_settings.h"

namespace search {

std::wstring build_connection_string_w(const DbSettings& settings) {
    if (settings.server.empty() || settings.initial_database.empty() || settings.user.empty()) {
        return {};
    }
    return L"packet size=4096;user id=" + settings.user +
           L";password=" + settings.password +
           L";data source=" + settings.server +
           L";persist security info=True;initial catalog=" + settings.initial_database;
}

}  // namespace search
