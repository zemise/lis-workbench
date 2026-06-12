#include "crash_handler.h"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <ctime>

#pragma comment(lib, "dbghelp.lib")

namespace {

LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* exception_info) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t name[128]{};
    swprintf_s(name, L"crash_%04u%02u%02u_%02u%02u%02u.dmp",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);

    HANDLE file = CreateFileW(name, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = exception_info;
        mei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          file, MiniDumpNormal,
                          &mei, nullptr, nullptr);
        CloseHandle(file);
    }

    return EXCEPTION_EXECUTE_HANDLER;  // Let the OS show the crash dialog / WER
}

}  // namespace

void install_crash_handler() {
    SetUnhandledExceptionFilter(unhandled_exception_filter);
}

#endif
