#pragma once

#include "module_registry.h"

#ifdef _WIN32
HWND create_emergency_statistics_module(const ModuleContext& ctx);
#endif
