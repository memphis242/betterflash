include(ExternalProject)

set(BETTERFLASH_WHISPER_BUILD_DIR "${CMAKE_BINARY_DIR}/betterflash-whisper-build")
ExternalProject_Add(betterflash_whisper_helper
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/whisper-runner"
    BINARY_DIR "${BETTERFLASH_WHISPER_BUILD_DIR}"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_CXX_STANDARD=23
        -DCMAKE_PREFIX_PATH:PATH=${CMAKE_PREFIX_PATH}
        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${BETTERFLASH_WHISPER_BUILD_DIR}/bin
        -DBETTERFLASH_LOCAL_VULKAN=${BETTERFLASH_LOCAL_VULKAN}
        -DSPIRV-Headers_DIR:PATH=${SPIRV-Headers_DIR}
        -DSPIRV_HEADERS_INCLUDE_DIR:PATH=${SPIRV_HEADERS_INCLUDE_DIR}
        -DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS}
    BUILD_COMMAND ${CMAKE_COMMAND} --build . --parallel 24
    INSTALL_COMMAND ""
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS "${BETTERFLASH_WHISPER_BUILD_DIR}/bin/betterflash-whisper")
set(BETTERFLASH_WHISPER_RUNTIME "${CMAKE_BINARY_DIR}/betterflash-whisper")
add_custom_target(betterflash_whisper_copy ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BETTERFLASH_WHISPER_BUILD_DIR}/bin/betterflash-whisper"
        "${BETTERFLASH_WHISPER_RUNTIME}"
    DEPENDS betterflash_whisper_helper
    VERBATIM)
add_dependencies(betterflash betterflash_whisper_copy)
