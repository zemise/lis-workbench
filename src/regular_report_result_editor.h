#pragma once

#ifdef _WIN32

#include <windows.h>

struct RegularReportState;

enum class RegularResultEditExit { discard, leave, confirm };

void regularBeginResultEdit(RegularReportState* st, int row);
void regularFinishResultEdit(RegularReportState* st, RegularResultEditExit exit,
                             bool restoreListFocus = true);
LRESULT CALLBACK regularResultListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                       UINT_PTR subclassId, DWORD_PTR data);

// The report module owns persistence, report selection and post-save refresh.
void regularSaveEditedResultAndAdvance(RegularReportState* st);

#endif
