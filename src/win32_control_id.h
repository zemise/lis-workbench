#pragma once

#ifdef _WIN32

#include <windows.h>

inline HMENU win32_control_id(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

#endif
