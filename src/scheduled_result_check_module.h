#pragma once

#ifdef _WIN32

#include "module_registry.h"

constexpr UINT WM_SCHEDULED_RESULT_NOTIFICATION = WM_APP + 246;

HWND create_scheduled_result_check_module(const ModuleContext &ctx);
void start_scheduled_result_check_monitor(HWND main_window,
                                          const ModuleContext &ctx);
void stop_scheduled_result_check_monitor();
bool run_scheduled_result_check_now();
bool run_scheduled_result_check_timer();
void handle_scheduled_result_check_notification(LPARAM event_code);

#endif
