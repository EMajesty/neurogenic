cmake_minimum_required(VERSION 3.22)
project(neurogenic VERSION 0.1.0 LANGUAGES C CXX)

set(NEUROGENIC_BUILD_COMMIT "" CACHE STRING "Source commit recorded in diagnostics")
if(NOT NEUROGENIC_BUILD_COMMIT)
  execute_process(COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE NEUROGENIC_BUILD_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
endif()
if(NOT NEUROGENIC_BUILD_COMMIT)
  set(NEUROGENIC_BUILD_COMMIT "unknown")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(NEUROGENIC_FETCH_DEPENDENCIES "Fetch pinned audio, MIDI, Lua and sol2 dependencies" OFF)
option(NEUROGENIC_BUILD_TESTS "Build tests" ON)
option(NEUROGENIC_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(NEUROGENIC_ENABLE_GUI "Build the optional SDL3 graphical frontend" ON)
option(NEUROGENIC_ENABLE_SLINT "Build the optional Slint graphical frontend" OFF)

if(NEUROGENIC_ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  else()
    message(FATAL_ERROR "NEUROGENIC_ENABLE_SANITIZERS requires GCC or Clang")
  endif()
endif()

include(cmake/Dependencies.cmake)
find_package(Threads REQUIRED)

add_library(neurogenic_engine STATIC
  src/application/control.cpp src/application/controller.cpp src/application/controller_coordinator.cpp src/application/controller_protocol.cpp src/application/controller_session.cpp src/application/discovery.cpp src/application/parameter_binding.cpp src/application/lifecycle.cpp src/application/performance_macros.cpp src/application/recording_workflow.cpp
  src/audio/engine.cpp src/audio/live_loop.cpp src/audio/mixer.cpp src/audio/recording.cpp src/audio/sample.cpp src/audio/sample_analysis.cpp src/audio/sample_manager.cpp src/audio/sample_processing.cpp src/audio/slicing.cpp src/core/command.cpp src/core/control_dispatcher.cpp
  src/dsp/effect_chain.cpp src/dsp/modulation.cpp src/dsp/voice_modulation.cpp src/dsp/purest_gain.cpp src/dsp/processor.cpp src/dsp/parameter_destination.cpp src/dsp/time_stretch.cpp src/instrument/subtractive_synth.cpp src/midi/mapping.cpp src/midi/routing.cpp src/project/project.cpp src/project/preset.cpp
  src/diagnostics/hardware.cpp
  src/performance/monitor.cpp
  src/sequencer/scheduler.cpp src/sequencer/pattern_adapter.cpp src/scripting/runtime.cpp src/timeline/arrangement.cpp src/timeline/arrangement_recording.cpp src/timeline/edit.cpp src/timeline/io.cpp src/timeline/performance_launching.cpp src/timeline/playback.cpp)
target_include_directories(neurogenic_engine PUBLIC include)
target_compile_options(neurogenic_engine PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(neurogenic_engine PRIVATE
  NEUROGENIC_VERSION="${PROJECT_VERSION}"
  NEUROGENIC_COMMIT="${NEUROGENIC_BUILD_COMMIT}")
target_link_libraries(neurogenic_engine PUBLIC Threads::Threads)

add_library(neurogenic_gui STATIC src/ui/gui.cpp src/ui/sdl_backend.cpp)
target_include_directories(neurogenic_gui PUBLIC include)
target_link_libraries(neurogenic_gui PUBLIC neurogenic_engine)
target_compile_options(neurogenic_gui PRIVATE -Wall -Wextra -Wpedantic)
if(NEUROGENIC_ENABLE_GUI)
  find_package(SDL3 QUIET CONFIG)
  if(SDL3_FOUND)
    target_compile_definitions(neurogenic_gui PRIVATE NEUROGENIC_HAS_SDL3)
    target_link_libraries(neurogenic_gui PRIVATE SDL3::SDL3)
    message(STATUS "NEUROGENIC: SDL3 graphical frontend enabled")
  else()
    message(STATUS "NEUROGENIC: SDL3 not found; building GUI-unavailable stub")
  endif()
endif()

# The renderer-independent adapter is always available so headless builds test
# the same state/action bridge. Slint itself is resolved only when explicitly
# requested, keeping graphical dependencies out of headless configurations.
add_library(neurogenic_slint_adapter STATIC src/ui/slint_adapter.cpp)
target_include_directories(neurogenic_slint_adapter PUBLIC include)
target_link_libraries(neurogenic_slint_adapter PUBLIC neurogenic_engine)
target_compile_options(neurogenic_slint_adapter PRIVATE -Wall -Wextra -Wpedantic)
if(NEUROGENIC_ENABLE_SLINT)
  # Dependency-enabled builds deliberately use the pinned source declaration,
  # even if a prior install tree is nearby. This prevents an installed compiler
  # or config package from contaminating a subsequent reproducible build.
  if(NOT NEUROGENIC_FETCH_DEPENDENCIES)
    find_package(Slint 1.19.0 QUIET CONFIG)
  endif()
  if(NOT Slint_FOUND)
    include(FetchContent)
    FetchContent_Declare(Slint
      GIT_REPOSITORY https://github.com/slint-ui/slint.git
      GIT_TAG v1.19.0
      GIT_SHALLOW TRUE
      PATCH_COMMAND ${CMAKE_COMMAND}
        -DSLINT_SOURCE_DIR=<SOURCE_DIR>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DisableSlintCompilerJemalloc.cmake
      SOURCE_SUBDIR api/cpp)
    FetchContent_MakeAvailable(Slint)
  endif()
  # FetchContent source builds accept an externally prepared host compiler but
  # do not create the imported compiler target/macro in that configuration.
  if(SLINT_COMPILER AND NOT TARGET Slint::slint-compiler)
    add_executable(Slint::slint-compiler IMPORTED GLOBAL)
    set_target_properties(Slint::slint-compiler PROPERTIES
      IMPORTED_LOCATION "${SLINT_COMPILER}")
    include("${slint_SOURCE_DIR}/api/cpp/cmake/SlintMacro.cmake")
  endif()
  add_library(neurogenic_slint_frontend STATIC src/ui/slint_frontend.cpp)
  target_include_directories(neurogenic_slint_frontend PUBLIC include)
  target_link_libraries(neurogenic_slint_frontend PUBLIC
    neurogenic_slint_adapter Slint::Slint)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Slint's winit backend loads this library dynamically. Retain it as an
    # ELF dependency so Nix records a clean RUNPATH; do not depend on a global
    # LD_LIBRARY_PATH in CI or the appliance.
    find_library(NEUROGENIC_XKBCOMMON_X11 xkbcommon-x11 REQUIRED)
    target_link_libraries(neurogenic_slint_frontend PRIVATE
      "-Wl,--no-as-needed" ${NEUROGENIC_XKBCOMMON_X11} "-Wl,--as-needed")
  endif()
  target_compile_options(neurogenic_slint_frontend PRIVATE
    -Wall -Wextra -Wpedantic)
  slint_target_sources(neurogenic_slint_frontend ui/slint/performance.slint
    EDITION 1.19)
  add_executable(neurogenic_slint apps/neurogenic_slint.cpp)
  target_link_libraries(neurogenic_slint PRIVATE neurogenic_slint_frontend)
  set_target_properties(neurogenic_slint PROPERTIES OUTPUT_NAME neurogenic-slint
    INSTALL_RPATH "$ORIGIN/../lib64;$ORIGIN/../lib")
  install(TARGETS neurogenic_slint RUNTIME DESTINATION bin)
  message(STATUS "NEUROGENIC: Slint 1.19 frontend enabled")
else()
  target_sources(neurogenic_slint_adapter PRIVATE src/ui/slint_stub.cpp)
endif()

if(NEUROGENIC_FETCH_DEPENDENCIES)
  target_compile_definitions(neurogenic_engine PRIVATE NEUROGENIC_HAS_MINIAUDIO PUBLIC NEUROGENIC_HAS_LUA)
  target_link_libraries(neurogenic_engine PRIVATE neurogenic_miniaudio sol2::sol2 lua_static)
endif()

add_library(neurogenic_live STATIC src/audio/device.cpp src/midi/input.cpp src/midi/output.cpp)
target_include_directories(neurogenic_live PUBLIC include)
target_link_libraries(neurogenic_live PUBLIC neurogenic_engine)
if(NEUROGENIC_FETCH_DEPENDENCIES)
  target_compile_definitions(neurogenic_live PRIVATE NEUROGENIC_HAS_MINIAUDIO NEUROGENIC_HAS_LIBREMIDI)
  target_link_libraries(neurogenic_live PRIVATE neurogenic_miniaudio libremidi)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Retain ALSA as an ELF dependency so Nix records alsa-lib in RUNPATH.
    target_compile_definitions(neurogenic_live PRIVATE NEUROGENIC_HAS_ALSA)
    target_link_libraries(neurogenic_live PUBLIC ALSA::ALSA)
  endif()
endif()

add_executable(neurogenic apps/neurogenic.cpp)
target_link_libraries(neurogenic PRIVATE neurogenic_engine neurogenic_live neurogenic_gui
  neurogenic_slint_adapter)
if(NEUROGENIC_ENABLE_SLINT)
  target_link_libraries(neurogenic PRIVATE neurogenic_slint_frontend)
  set_target_properties(neurogenic PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib64;$ORIGIN/../lib")
endif()
add_executable(neurogenic_diagnostics apps/neurogenic_diagnostics.cpp)
target_link_libraries(neurogenic_diagnostics PRIVATE neurogenic_engine neurogenic_live neurogenic_gui)
set_target_properties(neurogenic_diagnostics PROPERTIES OUTPUT_NAME neurogenic-diagnostics)
add_executable(neurogenic_discovery apps/neurogenic_discovery.cpp)
target_link_libraries(neurogenic_discovery PRIVATE neurogenic_engine)
set_target_properties(neurogenic_discovery PROPERTIES OUTPUT_NAME neurogenic-discovery)
install(TARGETS neurogenic neurogenic_diagnostics neurogenic_discovery RUNTIME DESTINATION bin)
install(FILES examples/generate.lua examples/timeline_drums.lua examples/timeline_mutate.lua
  examples/timeline_polyrhythm.lua examples/timeline_variation.lua DESTINATION share/neurogenic/examples)
install(DIRECTORY examples/controllers DESTINATION share/neurogenic/examples)
install(PROGRAMS packaging/neurogenic-appliance packaging/neurogenic-preflight
  DESTINATION bin)
install(FILES packaging/neurogenic-appliance.service DESTINATION share/neurogenic/appliance)

if(NEUROGENIC_BUILD_TESTS)
  enable_testing()
  add_executable(neurogenic_tests tests/tests.cpp)
  target_link_libraries(neurogenic_tests PRIVATE neurogenic_engine neurogenic_gui)
  target_compile_definitions(neurogenic_tests PRIVATE NEUROGENIC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME neurogenic_tests COMMAND neurogenic_tests)
  add_executable(neurogenic_gui_smoke tests/gui_smoke.cpp)
  target_link_libraries(neurogenic_gui_smoke PRIVATE neurogenic_engine neurogenic_gui)
  add_test(NAME neurogenic_gui_smoke COMMAND neurogenic_gui_smoke)
  set_tests_properties(neurogenic_gui_smoke PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_diagnostics_schema
    COMMAND neurogenic_diagnostics --headless --json)
  set_tests_properties(neurogenic_diagnostics_schema PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.hardware-diagnostics.v2" TIMEOUT 10
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_appliance_packaging
    COMMAND ${CMAKE_COMMAND}
      -DNEUROGENIC_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DNEUROGENIC_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyAppliancePackaging.cmake)
  set_tests_properties(neurogenic_appliance_packaging PROPERTIES TIMEOUT 15)
  add_test(NAME neurogenic_discovery_schema COMMAND neurogenic-discovery --json)
  set_tests_properties(neurogenic_discovery_schema PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.application-discovery.v1" TIMEOUT 10)
  add_executable(neurogenic_discovery_tests tests/discovery_tests.cpp)
  target_link_libraries(neurogenic_discovery_tests PRIVATE neurogenic_engine)
  add_test(NAME neurogenic_discovery_tests COMMAND neurogenic_discovery_tests)
  set_tests_properties(neurogenic_discovery_tests PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.discovery-tests.v1 passed" TIMEOUT 10)
  add_executable(neurogenic_parameter_binding_tests
    tests/parameter_binding_tests.cpp)
  target_link_libraries(neurogenic_parameter_binding_tests PRIVATE
    neurogenic_engine)
  add_test(NAME neurogenic_parameter_binding_tests
    COMMAND neurogenic_parameter_binding_tests)
  set_tests_properties(neurogenic_parameter_binding_tests PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.parameter-binding-tests.v1 passed"
    TIMEOUT 10)
  add_executable(neurogenic_client_independence_tests
    tests/client_independence_tests.cpp)
  target_link_libraries(neurogenic_client_independence_tests PRIVATE
    neurogenic_engine)
  add_test(NAME neurogenic_client_independence_tests
    COMMAND neurogenic_client_independence_tests)
  set_tests_properties(neurogenic_client_independence_tests PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.client-independence-tests.v1 passed"
    TIMEOUT 15)
  add_executable(neurogenic_modulation_authoring_tests
    tests/modulation_authoring_tests.cpp)
  target_link_libraries(neurogenic_modulation_authoring_tests PRIVATE
    neurogenic_engine)
  add_test(NAME neurogenic_modulation_authoring_tests
    COMMAND neurogenic_modulation_authoring_tests)
  set_tests_properties(neurogenic_modulation_authoring_tests PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.modulation-authoring-tests.v1 passed"
    TIMEOUT 30)
  add_executable(neurogenic_slint_adapter_tests tests/slint_adapter_tests.cpp)
  target_link_libraries(neurogenic_slint_adapter_tests PRIVATE
    neurogenic_slint_adapter)
  add_test(NAME neurogenic_slint_adapter_tests
    COMMAND neurogenic_slint_adapter_tests)
  set_tests_properties(neurogenic_slint_adapter_tests PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.slint-adapter-tests.v1 passed"
    TIMEOUT 10)
  add_test(NAME neurogenic_optional_frontend_configuration
    COMMAND ${CMAKE_COMMAND}
      -DNEUROGENIC_ENABLE_SLINT=${NEUROGENIC_ENABLE_SLINT}
      -DNEUROGENIC_SLINT_TARGET=$<TARGET_EXISTS:neurogenic_slint>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyOptionalSlint.cmake)
  add_test(NAME neurogenic_frontend_selection_validation
    COMMAND neurogenic --frontend invalid)
  set_tests_properties(neurogenic_frontend_selection_validation PROPERTIES
    WILL_FAIL TRUE TIMEOUT 10)
  if(NEUROGENIC_ENABLE_SLINT)
    add_test(NAME neurogenic_slint_missing_display
      COMMAND neurogenic --frontend slint --headless --run-for 0.05)
    set_tests_properties(neurogenic_slint_missing_display PROPERTIES
      WILL_FAIL TRUE TIMEOUT 10 ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=")
    add_executable(neurogenic_slint_rendered_tests
      tests/slint_rendered_tests.cpp)
    target_link_libraries(neurogenic_slint_rendered_tests PRIVATE
      neurogenic_slint_frontend)
    add_test(NAME neurogenic_slint_rendered_primary
      COMMAND neurogenic_slint_rendered_tests 1920 1200
        ${CMAKE_CURRENT_BINARY_DIR}/slint-primary.ppm)
    add_test(NAME neurogenic_slint_rendered_minimum
      COMMAND neurogenic_slint_rendered_tests 1024 600
        ${CMAKE_CURRENT_BINARY_DIR}/slint-minimum.ppm)
    set_tests_properties(neurogenic_slint_rendered_primary
      neurogenic_slint_rendered_minimum PROPERTIES
      PASS_REGULAR_EXPRESSION "neurogenic.slint-rendered-tests.v1 passed"
      TIMEOUT 30 ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=")
  endif()
  add_executable(neurogenic_slint_benchmark tests/slint_benchmark.cpp)
  target_link_libraries(neurogenic_slint_benchmark PRIVATE
    neurogenic_slint_adapter)
  add_test(NAME neurogenic_slint_incremental_publication
    COMMAND neurogenic_slint_benchmark)
  set_tests_properties(neurogenic_slint_incremental_publication PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.slint-benchmark.v1 passed"
    TIMEOUT 20)
  add_executable(neurogenic_preset_tests tests/preset_tests.cpp)
  target_link_libraries(neurogenic_preset_tests PRIVATE neurogenic_engine)
  add_test(NAME neurogenic_preset_tests COMMAND neurogenic_preset_tests)
  set_tests_properties(neurogenic_preset_tests PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.preset-tests.v1 passed" TIMEOUT 10)
  add_executable(neurogenic_sample_analysis_tests
    tests/sample_analysis_tests.cpp)
  target_link_libraries(neurogenic_sample_analysis_tests PRIVATE
    neurogenic_engine)
  add_test(NAME neurogenic_sample_analysis_tests
    COMMAND neurogenic_sample_analysis_tests)
  set_tests_properties(neurogenic_sample_analysis_tests PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.sample-analysis-tests.v1 passed"
    TIMEOUT 10)
  add_executable(neurogenic_sample_processing_tests
    tests/sample_processing_tests.cpp)
  target_link_libraries(neurogenic_sample_processing_tests PRIVATE
    neurogenic_engine)
  add_test(NAME neurogenic_sample_processing_tests
    COMMAND neurogenic_sample_processing_tests)
  set_tests_properties(neurogenic_sample_processing_tests PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.sample-processing-tests.v1 passed"
    TIMEOUT 10)
  add_executable(neurogenic_audio_clip_tests tests/audio_clip_tests.cpp)
  target_link_libraries(neurogenic_audio_clip_tests PRIVATE neurogenic_engine)
  add_test(NAME neurogenic_audio_clip_tests COMMAND neurogenic_audio_clip_tests)
  set_tests_properties(neurogenic_audio_clip_tests PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.audio-clip-tests.v1 passed"
    TIMEOUT 10)
  add_executable(neurogenic_live_loop_tests tests/live_loop_tests.cpp)
  target_link_libraries(neurogenic_live_loop_tests PRIVATE neurogenic_engine)
  add_test(NAME neurogenic_live_loop_tests COMMAND neurogenic_live_loop_tests)
  set_tests_properties(neurogenic_live_loop_tests PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.live-loop-tests.v1 passed"
    TIMEOUT 10)
  add_executable(neurogenic_performance_launching_tests
    tests/performance_launching_tests.cpp)
  target_link_libraries(neurogenic_performance_launching_tests PRIVATE
    neurogenic_engine)
  add_test(NAME neurogenic_performance_launching_tests
    COMMAND neurogenic_performance_launching_tests)
  set_tests_properties(neurogenic_performance_launching_tests PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.performance-launching-tests.v1 passed"
    TIMEOUT 10)
  add_test(NAME neurogenic_diagnostics_stress
    COMMAND neurogenic_diagnostics --headless --stress 0.05 --buffer 127
      --sample-voices 8 --synth-voices 4)
  set_tests_properties(neurogenic_diagnostics_stress PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.hardware-test-report.v1" TIMEOUT 10
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_diagnostics_device_lifecycle
    COMMAND neurogenic_diagnostics --headless --realtime --stress 0.05
      --output-channels 2)
  set_tests_properties(neurogenic_diagnostics_device_lifecycle PROPERTIES
    PASS_REGULAR_EXPRESSION
      "realtime-device-stress.*starts.:[ ]*1.*stops.:[ ]*1"
    TIMEOUT 10
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_diagnostics_requested_period
    COMMAND neurogenic_diagnostics --headless --realtime --stress 0.05
      --buffer 64 --output-channels 2)
  set_tests_properties(neurogenic_diagnostics_requested_period PROPERTIES
    PASS_REGULAR_EXPRESSION
      "requested_buffer_frames.:[ ]*64.*negotiated_buffer_frames.:[ ]*64"
    TIMEOUT 10 ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_display_diagnostics
    COMMAND neurogenic_diagnostics --display-test)
  set_tests_properties(neurogenic_display_diagnostics PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.display-test.v1" TIMEOUT 10
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_diagnostics_invalid_configuration
    COMMAND neurogenic_diagnostics --stress 1 --buffer 0)
  set_tests_properties(neurogenic_diagnostics_invalid_configuration PROPERTIES
    WILL_FAIL TRUE TIMEOUT 10)
  add_test(NAME neurogenic_appliance_lifecycle_schema
    COMMAND neurogenic_diagnostics --lifecycle-test)
  set_tests_properties(neurogenic_appliance_lifecycle_schema PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.appliance-lifecycle.v1" TIMEOUT 10)
  add_test(NAME neurogenic_controller_protocol_schema
    COMMAND neurogenic_diagnostics --controller-protocol-test)
  set_tests_properties(neurogenic_controller_protocol_schema PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.controller-protocol.v1" TIMEOUT 10)
  add_test(NAME neurogenic_appliance_start_stop
    COMMAND neurogenic --headless --gui --run-for 0.05 --preferences
      ${CMAKE_CURRENT_BINARY_DIR}/appliance-smoke.conf)
  set_tests_properties(neurogenic_appliance_start_stop PROPERTIES
    PASS_REGULAR_EXPRESSION "Stopped. dropped MIDI=" TIMEOUT 10
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_appliance_missing_hardware
    COMMAND neurogenic --headless --gui --audio definitely-missing-audio
      --midi definitely-missing-midi --run-for 0.05 --preferences
      ${CMAKE_CURRENT_BINARY_DIR}/appliance-missing-hardware.conf)
  set_tests_properties(neurogenic_appliance_missing_hardware PROPERTIES
    PASS_REGULAR_EXPRESSION "\"degraded\":true" TIMEOUT 10
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_test(NAME neurogenic_appliance_missing_controller
    COMMAND neurogenic --headless --gui --controller
      /dev/neurogenic-definitely-missing-controller --run-for 0.05
      --preferences ${CMAKE_CURRENT_BINARY_DIR}/appliance-missing-controller.conf)
  set_tests_properties(neurogenic_appliance_missing_controller PROPERTIES
    PASS_REGULAR_EXPRESSION "controller unavailable.*will retry exact path"
    TIMEOUT 10 ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_executable(neurogenic_workflows tests/workflow_scenarios.cpp)
  target_link_libraries(neurogenic_workflows PRIVATE neurogenic_engine neurogenic_gui)
  add_test(NAME neurogenic_workflows COMMAND neurogenic_workflows all)
  set_tests_properties(neurogenic_workflows PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.workflow.summary.v1 passed=6"
    TIMEOUT 30)
  add_executable(neurogenic_integration_validation
    tests/integration_validation.cpp)
  target_link_libraries(neurogenic_integration_validation PRIVATE
    neurogenic_engine)
  add_test(NAME neurogenic_integration_validation
    COMMAND neurogenic_integration_validation)
  set_tests_properties(neurogenic_integration_validation PROPERTIES
    PASS_REGULAR_EXPRESSION
      "neurogenic.integration-validation.summary.v1 passed=5"
    TIMEOUT 30)
  add_executable(neurogenic_benchmark benchmarks/timeline_benchmark.cpp)
  target_link_libraries(neurogenic_benchmark PRIVATE neurogenic_engine neurogenic_gui)
  # CTest validates every scenario and runs it under sanitizers, but does not
  # collect performance data. Keep this bounded independently of runner speed;
  # the benchmark executable retains its 2,000-iteration measurement default.
  add_test(NAME neurogenic_benchmark_schema COMMAND neurogenic_benchmark --iterations 100)
  set_tests_properties(neurogenic_benchmark_schema PROPERTIES
    PASS_REGULAR_EXPRESSION "neurogenic.benchmark.summary.v1" TIMEOUT 30)
  if(NEUROGENIC_FETCH_DEPENDENCIES AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME neurogenic_alsa_runtime_link COMMAND ${CMAKE_COMMAND}
      -DNEUROGENIC_EXECUTABLE=$<TARGET_FILE:neurogenic>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyAlsaRuntime.cmake)
    add_executable(neurogenic_midi_runtime_tests tests/midi_runtime_tests.cpp)
    target_link_libraries(neurogenic_midi_runtime_tests PRIVATE neurogenic_live)
    add_test(NAME neurogenic_midi_missing_infrastructure
      COMMAND neurogenic_midi_runtime_tests)
    set_tests_properties(neurogenic_midi_missing_infrastructure PROPERTIES
      PASS_REGULAR_EXPRESSION "neurogenic.midi-runtime-tests.v1 passed"
      TIMEOUT 10)
  endif()
endif()
