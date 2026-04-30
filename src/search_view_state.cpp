#include "search_view_state.h"

namespace search {

ViewState load_view_state() {
    ViewState state;
    state.ini_path = default_ini_path();
    state.settings = load_settings(state.ini_path);
    return state;
}

bool save_view_state_settings(const ViewState& state) {
    return save_settings(state.ini_path, state.settings);
}

}  // namespace search
