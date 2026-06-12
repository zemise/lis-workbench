#pragma once

#include <array>
#include <algorithm>

constexpr int QUICK_MACHINE_COUNT = 3;

constexpr std::array<const wchar_t*, QUICK_MACHINE_COUNT> QUICK_MACHINE_CODE_KEYS = {
    L"QuickMachine1Code", L"QuickMachine2Code", L"QuickMachine3Code"};

constexpr std::array<const wchar_t*, QUICK_MACHINE_COUNT> QUICK_MACHINE_NAME_KEYS = {
    L"QuickMachine1Name", L"QuickMachine2Name", L"QuickMachine3Name"};

constexpr std::array<const wchar_t*, QUICK_MACHINE_COUNT> QUICK_MACHINE_ROOM_KEYS = {
    L"QuickMachine1RoomCode", L"QuickMachine2RoomCode", L"QuickMachine3RoomCode"};

inline const wchar_t* quick_machine_code_key(int slot) {
    return QUICK_MACHINE_CODE_KEYS[static_cast<size_t>(std::clamp(slot, 0, QUICK_MACHINE_COUNT - 1))];
}

inline const wchar_t* quick_machine_name_key(int slot) {
    return QUICK_MACHINE_NAME_KEYS[static_cast<size_t>(std::clamp(slot, 0, QUICK_MACHINE_COUNT - 1))];
}

inline const wchar_t* quick_machine_room_key(int slot) {
    return QUICK_MACHINE_ROOM_KEYS[static_cast<size_t>(std::clamp(slot, 0, QUICK_MACHINE_COUNT - 1))];
}
