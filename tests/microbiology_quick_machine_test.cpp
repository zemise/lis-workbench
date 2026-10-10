#include "microbiology_quick_machine.h"

#include <iostream>
#include <map>
#include <utility>

namespace {
std::map<std::pair<std::wstring, std::wstring>, std::wstring> config;
}

namespace search {
// Test the config consumer without reading or modifying the user's INI file.
std::wstring load_module_str(const wchar_t* module, const wchar_t* key, const wchar_t* fallback) {
    const auto found = config.find({module, key});
    return found == config.end() ? fallback : found->second;
}
}

#define CHECK(x) do { if (!(x)) { std::cerr << "check failed line " << __LINE__ << ": " #x "\n"; return 1; } } while (false)

int main() {
    config[{L"RegularReport", L"QuickMachine1Code"}] = L"1001";
    config[{L"RegularReport", L"QuickMachine1RoomCode"}] = L"regular-room";
    config[{L"RegularReport", L"QuickMachine1Name"}] = L"常规仪器";
    auto first = search::load_microbiology_quick_machine(0);
    CHECK(first.code == "3001");
    CHECK(first.room_code.empty() && first.name.empty());
    CHECK(search::load_microbiology_quick_machine(1).code == "7002");
    CHECK(search::load_microbiology_quick_machine(2).code.empty());

    config[{L"MicrobiologyReport", L"QuickMachine1Code"}] = L" 7002 ";
    config[{L"MicrobiologyReport", L"QuickMachine1RoomCode"}] = L" 401 ";
    config[{L"MicrobiologyReport", L"QuickMachine1Name"}] = L"滨水微生物仪器";
    first = search::load_microbiology_quick_machine(0);
    CHECK(first.code == "7002" && first.room_code == "401");
    CHECK(first.name == L"滨水微生物仪器");
    CHECK(config.at({L"RegularReport", L"QuickMachine1Code"}) == L"1001");
    CHECK(search::load_microbiology_quick_machine(1).code == "7002");
    config[{L"MicrobiologyReport", L"QuickMachine1Code"}] = L" ";
    first = search::load_microbiology_quick_machine(0);
    CHECK(first.code == "3001");
    CHECK(first.room_code.empty() && first.name.empty());
    CHECK(search::load_microbiology_quick_machine(-1).code.empty());
    CHECK(search::load_microbiology_quick_machine(3).code.empty());
    const std::vector<search::MachineOption> machines = {
        {" 102 ", " 3001 ", "岳麓"},
        {"401", "3001", "另一科室"},
        {"401", "7002", "滨水"},
    };
    CHECK(search::find_microbiology_quick_machine(machines, " 3001 ", "") == &machines[0]);
    CHECK(search::find_microbiology_quick_machine(machines, "3001", " 401 ") == &machines[1]);
    CHECK(search::find_microbiology_quick_machine(machines, "7002", "401") == &machines[2]);
    CHECK(search::find_microbiology_quick_machine(machines, "3001", "missing") == nullptr);
    CHECK(search::find_microbiology_quick_machine(machines, "missing", "") == nullptr);
    CHECK(search::find_microbiology_quick_machine(machines, " ", "") == nullptr);
    CHECK(search::find_microbiology_quick_machine({}, "3001", "") == nullptr);
    return 0;
}
