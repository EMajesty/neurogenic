include(FetchContent)
if(NOT NEUROGENIC_FETCH_DEPENDENCIES)
  return()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(ALSA REQUIRED)
  message(STATUS "NEUROGENIC: ALSA development files found; enabling native Linux audio/MIDI backends")
endif()

# Pins are immutable release tags or commits. Update with license review.
set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.21 GIT_SHALLOW TRUE)

FetchContent_Declare(lua URL https://www.lua.org/ftp/lua-5.4.8.tar.gz URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(sol2 GIT_REPOSITORY https://github.com/ThePhD/sol2.git GIT_TAG v3.5.0 GIT_SHALLOW TRUE)
set(LIBREMIDI_NO_JACK ON CACHE BOOL "" FORCE)
set(LIBREMIDI_NO_PIPEWIRE ON CACHE BOOL "" FORCE)
set(LIBREMIDI_NO_NETWORK ON CACHE BOOL "" FORCE)
set(LIBREMIDI_NO_KEYBOARD ON CACHE BOOL "" FORCE)
# libremidi 5.4.2's optional ALSA/udev metadata helper asserts when the udev
# netlink monitor cannot be created (for example in a Nix sandbox or restricted
# service). ALSA sequencer enumeration and announce-port hotplug do not require
# this helper, so keep MIDI functional while avoiding a process-fatal optional
# metadata path. Remove this only after the pinned upstream handles a missing
# monitor without asserting.
set(LIBREMIDI_NO_UDEV ON CACHE BOOL "" FORCE)
set(LIBREMIDI_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBREMIDI_TESTS OFF CACHE BOOL "" FORCE)
set(LIBREMIDI_SLIM_MESSAGE 32 CACHE STRING "" FORCE)
FetchContent_Declare(libremidi GIT_REPOSITORY https://github.com/celtera/libremidi.git GIT_TAG v5.4.2 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(miniaudio lua sol2 libremidi)

add_library(neurogenic_miniaudio STATIC "${CMAKE_CURRENT_LIST_DIR}/../src/audio/miniaudio_impl.c")
target_include_directories(neurogenic_miniaudio PUBLIC "${miniaudio_SOURCE_DIR}")
if(UNIX)
  target_link_libraries(neurogenic_miniaudio PUBLIC ${CMAKE_DL_LIBS} m pthread)
endif()

# Lua upstream has no CMake project. Build only its portable library sources.
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${lua_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM LUA_SOURCES "${lua_SOURCE_DIR}/src/lua.c" "${lua_SOURCE_DIR}/src/luac.c")
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC "${lua_SOURCE_DIR}/src")
target_compile_definitions(lua_static PRIVATE LUA_USE_LINUX)
target_link_libraries(lua_static PRIVATE ${CMAKE_DL_LIBS} m)

set(NEUROGENIC_AIRWINDOWS_REV "master-2026-11-review-required" CACHE STRING "Airwindows provenance marker")
