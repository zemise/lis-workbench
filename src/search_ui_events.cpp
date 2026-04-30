#include "search_ui_events.h"

#ifdef _WIN32

#include <commctrl.h>

namespace search {

bool handle_command(HWND hwnd, WPARAM wparam, const CommandEventHandlers& handlers) {
    const int id = LOWORD(wparam);
    switch (id) {
        case 1009:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                if (handlers.on_room_changed) {
                    handlers.on_room_changed();
                }
                return true;
            }
            return false;
        case 3001:
            if (handlers.on_query) {
                handlers.on_query();
            }
            return true;
        case 3006:
            if (handlers.on_show_settings) {
                handlers.on_show_settings(hwnd);
            }
            return true;
        case 3003:
        case 3004:
        case 3005:
            if (handlers.on_unimplemented_action) {
                handlers.on_unimplemented_action(hwnd);
            }
            return true;
        case 3002:
            if (handlers.on_exit) {
                handlers.on_exit(hwnd);
            }
            return true;
        default:
            return false;
    }
}

bool handle_notify(LPARAM lparam, const NotifyEventHandlers& handlers, LRESULT& result) {
    auto* hdr = reinterpret_cast<NMHDR*>(lparam);
    if (hdr->idFrom == 2001 && hdr->code == LVN_ITEMCHANGED) {
        auto* item = reinterpret_cast<NMLISTVIEW*>(lparam);
        if ((item->uNewState & LVIS_SELECTED) != 0 && handlers.on_report_selected) {
            handlers.on_report_selected(item->iItem);
        }
        result = 0;
        return true;
    }

    if (hdr->idFrom == 2001 && hdr->code == NM_CUSTOMDRAW) {
        auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lparam);
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
            result = CDRF_NOTIFYITEMDRAW;
            return true;
        }
        if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            const auto index = static_cast<size_t>(draw->nmcd.dwItemSpec);
            if (handlers.report_rows && index < handlers.report_rows->size() && handlers.report_row_background) {
                draw->clrTextBk = handlers.report_row_background((*handlers.report_rows)[index]);
            }
            result = CDRF_DODEFAULT;
            return true;
        }
    }

    if (hdr->idFrom == 2002 && hdr->code == NM_CUSTOMDRAW) {
        auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lparam);
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
            result = CDRF_NOTIFYITEMDRAW;
            return true;
        }
        if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            const auto index = static_cast<size_t>(draw->nmcd.dwItemSpec);
            if (handlers.result_rows && index < handlers.result_rows->size() && handlers.result_row_color) {
                const auto color = handlers.result_row_color((*handlers.result_rows)[index]);
                if (color != CLR_INVALID) {
                    draw->clrText = color;
                }
            }
            result = CDRF_DODEFAULT;
            return true;
        }
    }

    return false;
}

}  // namespace search

#endif
