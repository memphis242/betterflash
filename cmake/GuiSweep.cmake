if(NOT DEFINED BETTERFLASH_EXECUTABLE OR NOT DEFINED BETTERFLASH_ARTIFACT_DIR)
    message(FATAL_ERROR "Supply BETTERFLASH_EXECUTABLE and BETTERFLASH_ARTIFACT_DIR.")
endif()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef sweep_id)
set(sweep_data "/tmp/betterflash-gui-${sweep_id}")
file(MAKE_DIRECTORY "${sweep_data}" "${BETTERFLASH_ARTIFACT_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
        "QT_LOGGING_RULES=*.warning=true;*.critical=true" QT_FORCE_STDERR_LOGGING=1
        "${BETTERFLASH_EXECUTABLE}" --data-dir "${sweep_data}"
        --gui-sweep "${BETTERFLASH_ARTIFACT_DIR}"
    RESULT_VARIABLE sweep_result
    TIMEOUT 180)
file(REMOVE_RECURSE "${sweep_data}")
if(NOT sweep_result STREQUAL "0")
    message(FATAL_ERROR "Native GUI sweep failed (${sweep_result}). Inspect ${BETTERFLASH_ARTIFACT_DIR}/report.json.")
endif()
