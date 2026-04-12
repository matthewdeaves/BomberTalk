cmake_minimum_required(VERSION 3.15)
project(BomberTalk C)

include(FetchContent)

# Detect platform
if(CMAKE_SYSTEM_NAME MATCHES "Retro68")
    set(BT_PLATFORM "68K")
    # PT_PLATFORM is set via -DPT_PLATFORM=OT or -DPT_PLATFORM=MACTCP
    if(NOT DEFINED PT_PLATFORM)
        set(PT_PLATFORM "MACTCP")
    endif()
elseif(CMAKE_SYSTEM_NAME MATCHES "RetroPPC")
    set(BT_PLATFORM "PPC")
    # PT_PLATFORM is set via -DPT_PLATFORM=OT or -DPT_PLATFORM=MACTCP
    if(NOT DEFINED PT_PLATFORM)
        set(PT_PLATFORM "OT")
    endif()
else()
    message(FATAL_ERROR "BomberTalk targets Classic Mac only (Retro68 or RetroPPC)")
endif()

message(STATUS "BomberTalk platform: ${BT_PLATFORM}, PT_PLATFORM: ${PT_PLATFORM}")

# --- clog dependency ---
# Use local dir if provided, otherwise fetch from GitHub
if(NOT DEFINED CLOG_DIR AND DEFINED ENV{CLOG_DIR})
    set(CLOG_DIR "$ENV{CLOG_DIR}")
endif()

if(DEFINED CLOG_DIR)
    message(STATUS "Using local clog: ${CLOG_DIR}")
    set(CLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CLOG_DIR} ${CMAKE_BINARY_DIR}/clog)
else()
    message(STATUS "Fetching clog from GitHub")
    FetchContent_Declare(clog
        GIT_REPOSITORY https://github.com/matthewdeaves/clog.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
    set(CLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(clog)
endif()

# --- peertalk dependency ---
# Use local dir if provided, otherwise fetch from GitHub
if(NOT DEFINED PEERTALK_DIR AND DEFINED ENV{PEERTALK_DIR})
    set(PEERTALK_DIR "$ENV{PEERTALK_DIR}")
endif()

set(PEERTALK_BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(DEFINED PEERTALK_DIR)
    message(STATUS "Using local peertalk: ${PEERTALK_DIR}")
    add_subdirectory(${PEERTALK_DIR} ${CMAKE_BINARY_DIR}/peertalk)
else()
    message(STATUS "Fetching peertalk from GitHub")
    FetchContent_Declare(peertalk
        GIT_REPOSITORY https://github.com/matthewdeaves/peertalk.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(peertalk)
endif()

# Debug logging toggle (default ON; set -DBOMBERTALK_DEBUG=OFF for release)
option(BOMBERTALK_DEBUG "Enable clog debug logging" ON)

# Compiler flags: C89 with warnings (no -pedantic due to clog variadic macros)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c89 -Wall -Wextra")

# Game sources
set(GAME_SOURCES
    src/main.c
    src/screens.c
    src/screen_loading.c
    src/screen_menu.c
    src/screen_lobby.c
    src/screen_game.c
    src/tilemap.c
    src/player.c
    src/bomb.c
    src/renderer.c
    src/input.c
    src/net.c
)

# Resource files
set(GAME_RESOURCES
    resources/bombertalk.r
    resources/bombertalk_size.r
)

# Build the application
add_application(BomberTalk ${GAME_SOURCES} ${GAME_RESOURCES})

target_include_directories(BomberTalk PRIVATE
    include
)

# When debug logging is OFF, strip all CLOG_* macros to ((void)0)
if(NOT BOMBERTALK_DEBUG)
    target_compile_definitions(BomberTalk PRIVATE CLOG_STRIP)
endif()

# Link PeerTalk and clog targets (built from source as subdirectories)
# clog always linked — PeerTalk depends on it. CLOG_STRIP only strips
# BomberTalk's own CLOG_* macro calls, not PeerTalk's internal logging.
# peertalk PUBLIC include dir propagates automatically; clog PUBLIC include
# dir propagates automatically.
target_link_libraries(BomberTalk peertalk clog)

# OT needs Open Transport libraries (peertalk links them PRIVATE,
# which doesn't propagate through static libs to consumers).
# Library names differ by architecture.
if(PT_PLATFORM STREQUAL "OT")
    if(BT_PLATFORM STREQUAL "68K")
        find_library(OT_APP_LIB OpenTransportApp)
        find_library(OT_LIB OpenTransport)
        find_library(OT_INET_LIB OpenTptInet)
    else()
        find_library(OT_APP_LIB OpenTransportAppPPC)
        find_library(OT_LIB OpenTransportLib)
        find_library(OT_INET_LIB OpenTptInternetLib)
    endif()
    foreach(LIB OT_APP_LIB OT_LIB OT_INET_LIB)
        if(${LIB})
            target_link_libraries(BomberTalk ${${LIB}})
        endif()
    endforeach()
endif()
