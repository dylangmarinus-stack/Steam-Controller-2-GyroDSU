cmake_minimum_required(VERSION 3.16)
project(sc2gyrodsu CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# ── hidapi linkage ────────────────────────────────────────────────────────────
# SC2_STATIC_HIDAPI=ON links libhidapi-hidraw statically so the binary runs on
# any Linux without requiring libhidapi-hidraw installed (used for CI releases).
# OFF (default) links the system shared library (fine for local builds).

option(SC2_STATIC_HIDAPI "Link libhidapi-hidraw statically (portable release binary)" OFF)

find_path(HIDAPI_INC hidapi/hidapi.h REQUIRED)

if(SC2_STATIC_HIDAPI)
    # Prefer .a over .so when searching
    set(_saved_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    find_library(HIDAPI_LIB hidapi-hidraw REQUIRED)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${_saved_suffixes})

    # hidapi-hidraw (hidraw backend) depends on libudev at link time.
    find_library(UDEV_LIB udev REQUIRED)
    set(HIDAPI_LINK ${HIDAPI_LIB} ${UDEV_LIB})
    message(STATUS "hidapi: static  (${HIDAPI_LIB})")
else()
    find_library(HIDAPI_LIB hidapi-hidraw REQUIRED)
    set(HIDAPI_LINK ${HIDAPI_LIB})
    message(STATUS "hidapi: dynamic (${HIDAPI_LIB})")
endif()

# ── Daemon: sc2gyrodsu ────────────────────────────────────────────────────────

add_executable(sc2gyrodsu
    src/main.cpp
    src/hiddev.cpp
    src/triton.cpp
    src/dsu.cpp
    src/config.cpp
)

target_include_directories(sc2gyrodsu PRIVATE inc ${HIDAPI_INC})
target_link_libraries(sc2gyrodsu PRIVATE
    Threads::Threads
    ${HIDAPI_LINK}
)
target_compile_options(sc2gyrodsu PRIVATE -O3 -Wall -Wextra)

# ── GUI configuration tool: sc2gyrodsu-config ─────────────────────────────────

find_package(Qt6 COMPONENTS Core Widgets Network QUIET)

if(Qt6_FOUND)
    message(STATUS "Qt6 found — building sc2gyrodsu-config")

    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)

    add_executable(sc2gyrodsu-config
        src/gui/configapp.cpp
        src/gui/mainwindow.cpp
        src/gui/axis_wizard.cpp
        src/gui/calibworker.cpp
        src/gui/dsu_client.cpp
        src/gui/controller_view.cpp
        src/gui/test_tab.cpp
        src/config.cpp
        src/triton.cpp
        assets/sc2gyrodsu.qrc
    )

    target_include_directories(sc2gyrodsu-config PRIVATE inc src ${HIDAPI_INC})
    target_link_libraries(sc2gyrodsu-config PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        ${HIDAPI_LINK}
        Threads::Threads
    )
    target_compile_options(sc2gyrodsu-config PRIVATE -O2 -Wall -Wextra)

    install(TARGETS sc2gyrodsu-config DESTINATION bin)
else()
    message(STATUS "Qt6 not found — skipping sc2gyrodsu-config (install qt6-base-dev to enable)")
endif()

install(TARGETS sc2gyrodsu DESTINATION bin)
