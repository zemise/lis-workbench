#pragma once

#include "app_settings_io.h"
#include "quick_machine_keys.h"
#include "search_text.h"
#include "search_core.h"

namespace search {

struct MicrobiologyQuickMachine {
    std::string code;
    std::string room_code;
    std::wstring name;
};

inline const wchar_t* default_microbiology_machine_code(int slot) {
    return slot == 0 ? L"3001" : slot == 1 ? L"7002" : L"";
}

// The returned pointer remains valid while the supplied options are unchanged.
inline const MachineOption* find_microbiology_quick_machine(
        const std::vector<MachineOption>& machines,
        const std::string& code, const std::string& room_code) {
    const auto wanted_code = trim(code);
    const auto wanted_room = trim(room_code);
    if (wanted_code.empty()) return nullptr;
    for (const auto& machine : machines) {
        if (trim(machine.mach_code) == wanted_code &&
            (wanted_room.empty() || trim(machine.room_code) == wanted_room)) {
            return &machine;
        }
    }
    return nullptr;
}

inline MicrobiologyQuickMachine load_microbiology_quick_machine(int slot) {
    MicrobiologyQuickMachine machine;
    if (slot < 0 || slot >= QUICK_MACHINE_COUNT) return machine;
    machine.code = trim(wide_to_utf8(load_module_str(
        L"MicrobiologyReport", quick_machine_code_key(slot), L"")));
    if (machine.code.empty()) {
        machine.code = wide_to_utf8(default_microbiology_machine_code(slot));
        return machine;  // Resolve default name/room from the database, never stale metadata.
    }
    machine.room_code = trim(wide_to_utf8(load_module_str(
        L"MicrobiologyReport", quick_machine_room_key(slot), L"")));
    machine.name = load_module_str(L"MicrobiologyReport", quick_machine_name_key(slot), L"");
    return machine;
}

}  // namespace search
