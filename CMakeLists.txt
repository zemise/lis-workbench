cmake_minimum_required(VERSION 3.20)

project(cpp_search LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Core library ──────────────────────────────────────────────
# Platform-free business logic. Reusable across Win32 and Qt frontends.
add_library(search_core STATIC
    src/search_core.cpp
    src/search_app.cpp
    src/search_controller.cpp
    src/search_text.cpp
    src/app_settings.cpp
    src/trend_core.cpp
)
target_include_directories(search_core PUBLIC src)
target_compile_definitions(search_core PRIVATE UNICODE _UNICODE)

if(WIN32)
    target_link_libraries(search_core PUBLIC odbc32)
endif()

# ── Win32 frontend ────────────────────────────────────────────
add_executable(result_search WIN32
    src/main.cpp
    src/app_settings_io.cpp
    src/search_input_view_model.cpp
    src/search_ui_events.cpp
    src/search_ui_presenter.cpp
    src/search_settings_dialog.cpp
    src/search_ui_layout.cpp
    src/trend_window.cpp
    src/trend_chart_renderer.cpp
    resource/app.rc
)
target_include_directories(result_search PRIVATE src resource)
target_compile_definitions(result_search PRIVATE UNICODE _UNICODE)
target_link_libraries(result_search PRIVATE search_core comctl32 comdlg32 gdiplus shell32 ole32)

if(MINGW)
    target_link_options(result_search PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

# ── Qt 5.15 frontend (opt-in) ─────────────────────────────────
option(BUILD_QT_GUI "Build Qt 5.15 GUI" OFF)

if(BUILD_QT_GUI)
    find_package(Qt5 REQUIRED COMPONENTS Widgets)

    add_executable(result_search_qt WIN32
        src_qt/main.cpp
    )
    target_include_directories(result_search_qt PRIVATE src)
    target_link_libraries(result_search_qt PRIVATE search_core Qt5::Widgets)

    if(MSVC)
        set_property(TARGET result_search_qt PROPERTY
                     WIN32_EXECUTABLE TRUE)
    endif()
endif()
