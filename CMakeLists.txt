cmake_minimum_required(VERSION 3.15)
project(BomberTalk C)

# PeerTalk SDK location
if(NOT DEFINED PEERTALK_DIR)
    set(PEERTALK_DIR "$ENV{HOME}/peertalk" CACHE PATH "PeerTalk SDK")
endif()

# clog location
if(NOT DEFINED CLOG_DIR)
    set(CLOG_DIR "$ENV{HOME}/clog" CACHE PATH "clog library")
endif()

# Detect platform and select library paths
if(CMAKE_SYSTEM_NAME MATCHES "Retro68")
    set(BT_PLATFORM "68K")
    set(PT_PLATFORM "MACTCP")
    set(CLOG_LIB_DIR "${CLOG_DIR}/build-m68k")
    set(PT_LIB_DIR "${PEERTALK_DIR}/build-68k")
elseif(CMAKE_SYSTEM_NAME MATCHES "RetroPPC")
    set(BT_PLATFORM "PPC")
    # PT_PLATFORM is set via -DPT_PLATFORM=OT or -DPT_PLATFORM=MACTCP
    if(NOT DEFINED PT_PLATFORM)
        set(PT_PLATFORM "OT")
    endif()
    set(CLOG_LIB_DIR "${CLOG_DIR}/build-ppc")
    if(PT_PLATFORM STREQUAL "OT")
        set(PT_LIB_DIR "${PEERTALK_DIR}/build-ppc-ot")
    else()
        set(PT_LIB_DIR "${PEERTALK_DIR}/build-ppc-mactcp")
    endif()
else()
    message(FATAL_ERROR "BomberTalk targets Classic Mac only (Retro68 or RetroPPC)")
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
    ${PEERTALK_DIR}/include
    ${CLOG_DIR}/include
)

# When debug logging is OFF, strip all CLOG_* macros to ((void)0)
if(NOT BOMBERTALK_DEBUG)
    target_compile_definitions(BomberTalk PRIVATE CLOG_STRIP)
endif()

# Link PeerTalk and clog static libraries
# (clog always linked — PeerTalk depends on it. CLOG_STRIP only strips
#  BomberTalk's own CLOG_* macro calls, not PeerTalk's internal logging.)
target_link_libraries(BomberTalk
    ${PT_LIB_DIR}/libpeertalk.a
    ${CLOG_LIB_DIR}/libclog.a
)

# PPC needs Open Transport libraries (handled by PeerTalk's static lib linkage)
if(BT_PLATFORM STREQUAL "PPC" AND PT_PLATFORM STREQUAL "OT")
    target_link_libraries(BomberTalk
        -lOpenTransportAppPPC
        -lOpenTransportLib
        -lOpenTptInternetLib
    )
endif()
