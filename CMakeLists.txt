cmake_minimum_required(VERSION 3.20)

project(cpp_search LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(result_search
    src/app_settings.cpp
    src/main.cpp
    src/search_app.cpp
    src/search_controller.cpp
    src/search_input_view_model.cpp
    src/search_core.cpp
    src/search_ui_events.cpp
    src/search_ui_presenter.cpp
    src/search_settings_dialog.cpp
    src/search_text.cpp
    src/search_ui_layout.cpp
    src/search_view_state.cpp
)

target_include_directories(result_search PRIVATE src)

if(WIN32)
    target_link_libraries(result_search PRIVATE odbc32 comctl32)
    target_compile_definitions(result_search PRIVATE UNICODE _UNICODE)
    if(MINGW)
        target_link_options(result_search PRIVATE -municode -static -static-libgcc -static-libstdc++)
    endif()
endif()
