# Slint 1.19 unconditionally enables jemalloc for its host compiler on native
# builds. jemalloc's configure probe is incompatible with Nix's wrapped GCC 15
# environment. The allocator affects only the build-time compiler, not the UI
# runtime; removing this optional feature keeps source builds portable.
set(file "${SLINT_SOURCE_DIR}/api/cpp/CMakeLists.txt")
file(READ "${file}" content)
set(block "    # Enable jemalloc, except when cross-compiling. (#7463)\n    if(NOT CMAKE_CROSSCOMPILING)\n        list(APPEND slint_compiler_features \"jemalloc\")\n    endif()\n")
string(REPLACE "${block}" "" patched "${content}")
if(patched STREQUAL content AND content MATCHES "slint_compiler_features.*jemalloc")
  message(FATAL_ERROR "Slint compiler jemalloc patch no longer matches upstream")
endif()
file(WRITE "${file}" "${patched}")
