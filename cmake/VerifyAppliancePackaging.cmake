set(root "${NEUROGENIC_BINARY_DIR}/appliance-test")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${root}/home" "${root}/bin")
file(WRITE "${root}/bin/neurogenic-diagnostics"
  "#!/bin/sh\nprintf '%s\\n' '{\"preferred_sample_rate\":48000}'\n")
file(CHMOD "${root}/bin/neurogenic-diagnostics"
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env
    HOME=${root}/home XDG_CONFIG_HOME=${root}/config XDG_DATA_HOME=${root}/data
    XDG_STATE_HOME=${root}/state XDG_CACHE_HOME=${root}/cache
    NEUROGENIC_DIAGNOSTICS_BIN=${root}/bin/neurogenic-diagnostics
    bash ${NEUROGENIC_SOURCE_DIR}/packaging/neurogenic-preflight --prepare
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "appliance preflight failed: ${output}${error}")
endif()
foreach(path config/neurogenic data/neurogenic/projects data/neurogenic/samples
             data/neurogenic/presets data/neurogenic/controllers
             data/neurogenic/recordings data/neurogenic/exports
             state/neurogenic/logs state/neurogenic/reports cache/neurogenic/tmp)
  if(NOT IS_DIRECTORY "${root}/${path}")
    message(FATAL_ERROR "preflight omitted ${path}")
  endif()
endforeach()
file(READ "${NEUROGENIC_SOURCE_DIR}/packaging/neurogenic-appliance" launcher)
if(NOT launcher MATCHES "NEUROGENIC_FRONTEND:-slint" OR
   NOT launcher MATCHES "NEUROGENIC_MAINTENANCE")
  message(FATAL_ERROR "launcher is not Slint-first with maintenance recovery")
endif()
