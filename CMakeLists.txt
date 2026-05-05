cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(cpp_search LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# MSVC defaults to system locale (e.g. GBK/936) for source encoding.
# Force UTF-8 so Chinese string literals survive compilation.
if(MSVC)
    add_compile_options(/utf-8)
endif()

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
    find_package(Qt5 REQUIRED COMPONENTS Widgets Concurrent PrintSupport)

    # Qwt 6.3.0 — scientific chart library (LGPL)
    find_package(Qwt QUIET)
    if(Qwt_FOUND)
        message(STATUS "Qwt found: ${Qwt_DIR}")
    else()
        message(WARNING "Qwt not found — trend chart will be placeholder."
                        " Install: vcpkg install qwt")
    endif()

    add_executable(result_search_qt WIN32
        src_qt/main.cpp
        src_qt/main_window.cpp
        src_qt/settings_dialog.cpp
        src_qt/trend_window.cpp
        src_qt/resources.qrc
    )
    target_include_directories(result_search_qt PRIVATE src src_qt)
    target_link_libraries(result_search_qt PRIVATE search_core Qt5::Widgets Qt5::WinMain Qt5::Concurrent Qt5::PrintSupport)
    if(Qwt_FOUND)
        target_link_libraries(result_search_qt PRIVATE qwt::qwt)
        target_compile_definitions(result_search_qt PRIVATE HAS_QWT)
    endif()
endif()
