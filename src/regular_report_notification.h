#pragma once

#ifdef _WIN32

#include "search_core.h"

bool regularNotifyDoctorStations(const search::ReportNotification& notification,
                                 const std::wstring& sender);

#endif
