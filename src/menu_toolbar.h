#pragma once

#ifdef _WIN32

#include <windows.h>

// ── MenuToolbar — flat menu-style button bar ────────────────
//
// Usage:
//   HWND tb = mtCreate(hwnd, inst, font, ID_TOOLBAR);
//   mtAddButton(tb, L"按钮1", CMD_FOO);
//   mtAddButton(tb, L"按钮2", CMD_BAR);
//   // Buttons fire WM_COMMAND with their cmdId to the parent.
//   // In parent's WM_SIZE, call MoveWindow(tb, 0, 0, w, 28, TRUE).

#define MT_MAXBTN 16

HWND mtCreate(HWND parent, HINSTANCE inst, HFONT font, int ctrlId);
void mtAddButton(HWND hwnd, const wchar_t* text, int cmdId);

#endif
