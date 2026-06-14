#include "search_ui_events.h"

#ifdef _WIN32

#include <commctrl.h>

namespace search {

bool handle_command(HWND hwnd, WPARAM wparam, const MainUiIds& ids, const CommandEventHandlers& handlers) {
    const int id = LOWORD(wparam);
    if (id == ids.room) {
        if (HIWORD(wparam) == CBN_SELCHANGE) {
            if (handlers.on_room_changed) {
                handlers.on_room_changed();
            }
            return true;
        }
        return false;
    }
    if (id == ids.query) {
        if (handlers.on_query) {
            handlers.on_query();
        }
        return true;
    }
    if (id == ids.trend) {
        if (handlers.on_show_trend) {
            handlers.on_show_trend(hwnd);
        }
        return true;
    }
    if (id == ids.settings) {
        if (handlers.on_show_settings) {
            handlers.on_show_settings(hwnd);
        }
        return true;
    }
    if (id == ids.export_ || id == ids.preview || id == ids.print) {
        if (handlers.on_unimplemented_action) {
            handlers.on_unimplemented_action(hwnd);
        }
        return true;
    }
    if (id == ids.exit) {
        if (handlers.on_exit) {
            handlers.on_exit(hwnd);
        }
        return true;
    }
    return false;
}

bool handle_notify(LPARAM lparam, const MainUiIds& ids, const NotifyEventHandlers& handlers, LRESULT& result) {
    auto* hdr = reinterpret_cast<NMHDR*>(lparam);
    if (hdr->idFrom == static_cast<UINT_PTR>(ids.reports) && hdr->code == LVN_ITEMCHANGED) {
        auto* item = reinterpret_cast<NMLISTVIEW*>(lparam);
        if ((item->uNewState & LVIS_SELECTED) != 0 && handlers.on_report_selected) {
            handlers.on_report_selected(item->iItem);
        }
        result = 0;
        return true;
    }

    if (hdr->idFrom == static_cast<UINT_PTR>(ids.reports) &&
        (hdr->code == NM_DBLCLK || hdr->code == LVN_ITEMACTIVATE)) {
        auto* item = reinterpret_cast<NMITEMACTIVATE*>(lparam);
        if (item->iItem >= 0 && handlers.on_report_activated) {
            handlers.on_report_activated(item->iItem);
        }
        result = 0;
        return true;
    }

    if (hdr->idFrom == static_cast<UINT_PTR>(ids.reports) && hdr->code == NM_CUSTOMDRAW) {
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

    if (hdr->idFrom == static_cast<UINT_PTR>(ids.results) && hdr->code == NM_CUSTOMDRAW) {
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
