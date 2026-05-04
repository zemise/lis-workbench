#pragma once

#include <string>

namespace search {

struct DbSettings {
    std::wstring server;
    std::wstring initial_database;
    std::wstring user;
    std::wstring password;
};

struct UiSettings {
    int font_size = 9;
    int splitter_x = 0;
};

struct AppSettings {
    DbSettings db;
    UiSettings ui;
};

std::wstring build_connection_string_w(const DbSettings& settings);

}  // namespace search
