#pragma once

#ifdef _WIN32

#include <windows.h>

// ── MenuToolbar — native Win32 command bar ──────────────────
//
// Usage:
//   HWND tb = mtCreate(hwnd, inst, font, ID_TOOLBAR);
//   mtAddButton(tb, L"关闭",   CMD_CLOSE);
//   mtAddButton(tb, L"保存",   CMD_SAVE);
//   mtAddSeparator(tb);
//   mtAddButton(tb, L"退出",   CMD_EXIT, true);  // disabled
//   mtEnableButton(tb, CMD_EXIT, false);
//
//   // In parent WM_SIZE: MoveWindow(tb, 0, 0, w, mtGetHeight(tb), TRUE);
//   // Buttons fire WM_COMMAND with their cmdId.

#define MT_MAXBTN 32

// Button styles
#define MTBS_SEPARATOR  0x0001
#define MTBS_DISABLED   0x0002
#define MTBS_STRETCH    0x0004
#define MTBS_CLOSE      0x0008

HWND mtCreate(HWND parent, HINSTANCE inst, HFONT font, int ctrlId);
void mtSetFont(HWND hwnd, HFONT font);
void mtAddButton(HWND hwnd, const wchar_t* text, int cmdId, bool enabled = true);
void mtAddButton(HWND hwnd, const wchar_t* text, int cmdId, HICON icon, bool enabled = true);
void mtAddCloseButton(HWND hwnd, const wchar_t* text, int cmdId, bool enabled = true);
void mtAddSeparator(HWND hwnd);
void mtAddStretch(HWND hwnd);
void mtEnableButton(HWND hwnd, int cmdId, bool enable);
void mtSetActiveButton(HWND hwnd, int cmdId);
int  mtGetHeight(HWND hwnd);

#endif
