include(FetchContent)

# Portable CPU inference is always available. GPU support is compiled when its
# toolchain is available, and the application still selects devices at runtime.
find_package(Vulkan QUIET COMPONENTS glslc)
find_package(SPIRV-Headers CONFIG QUIET)
if(Vulkan_FOUND AND Vulkan_GLSLC_EXECUTABLE AND NOT SPIRV-Headers_FOUND)
    FetchContent_Declare(betterflash_spirv_headers
        URL https://codeload.github.com/KhronosGroup/SPIRV-Headers/tar.gz/refs/tags/vulkan-sdk-1.4.341.0
        URL_HASH SHA256=cab0a654c4917e16367483296b44cdb1d614e3120c721beafcd37e3a8580486c
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(SPIRV_HEADERS_ENABLE_TESTS OFF CACHE BOOL "No shader header tests" FORCE)
    set(SPIRV_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "No shader header install" FORCE)
    FetchContent_MakeAvailable(betterflash_spirv_headers)
    file(MAKE_DIRECTORY "${betterflash_spirv_headers_BINARY_DIR}/cmake")
    file(WRITE "${betterflash_spirv_headers_BINARY_DIR}/cmake/SPIRV-HeadersConfig.cmake"
        "set(SPIRV_HEADERS_INCLUDE_DIR \"${betterflash_spirv_headers_SOURCE_DIR}/include\")\n")
    set(SPIRV-Headers_DIR "${betterflash_spirv_headers_BINARY_DIR}/cmake" CACHE PATH "Pinned shader header package" FORCE)
    find_package(SPIRV-Headers CONFIG QUIET)
endif()
set(BETTERFLASH_VULKAN_DEFAULT OFF)
if(Vulkan_FOUND AND Vulkan_GLSLC_EXECUTABLE AND SPIRV-Headers_FOUND)
    set(BETTERFLASH_VULKAN_DEFAULT ON)
endif()
option(BETTERFLASH_LOCAL_VULKAN "Build Vulkan acceleration for local language models" ${BETTERFLASH_VULKAN_DEFAULT})
set(GGML_VULKAN ${BETTERFLASH_LOCAL_VULKAN} CACHE BOOL "Vulkan backend" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "Portable CPU instructions" FORCE)
set(GGML_BACKEND_DL OFF CACHE BOOL "Link inference backends" FORCE)
set(LLAMA_BUILD_COMMON ON CACHE BOOL "Chat templates and structured sampling" FORCE)
foreach(option LLAMA_BUILD_TESTS LLAMA_BUILD_TOOLS LLAMA_BUILD_EXAMPLES LLAMA_BUILD_SERVER
        LLAMA_BUILD_APP LLAMA_BUILD_MTMD LLAMA_OPENSSL LLAMA_SUBPROCESS)
    set(${option} OFF CACHE BOOL "Exclude unused language runtime components" FORCE)
endforeach()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static inference runtime" FORCE)
FetchContent_Declare(betterflash_llama
    URL https://codeload.github.com/ggml-org/llama.cpp/tar.gz/8bb3cf0d216a5ae48360179f2931daf284be4d01
    URL_HASH SHA256=1b1a5bd079840425683ff6caebf3543e08d2dbcab7563782fabdf145030521a2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(betterflash_llama)
set(BETTERFLASH_CLEF_SOURCE "${betterflash_llama_SOURCE_DIR}/src/models/clef.cpp")
file(READ "${BETTERFLASH_CLEF_SOURCE}" BETTERFLASH_CLEF_CONTENT)
if(NOT BETTERFLASH_CLEF_CONTENT MATCHES "#include <algorithm>")
    file(WRITE "${BETTERFLASH_CLEF_SOURCE}" "#include <algorithm>\n${BETTERFLASH_CLEF_CONTENT}")
endif()
if(TARGET ggml-vulkan)
    if(TARGET SPIRV-Headers::SPIRV-Headers)
        target_link_libraries(ggml-vulkan PRIVATE SPIRV-Headers::SPIRV-Headers)
    elseif(TARGET SPIRV-Headers)
        target_link_libraries(ggml-vulkan PRIVATE SPIRV-Headers)
    elseif(SPIRV_HEADERS_INCLUDE_DIR)
        target_include_directories(ggml-vulkan PRIVATE "${SPIRV_HEADERS_INCLUDE_DIR}")
    endif()
endif()
message(STATUS "BetterFlash local language model Vulkan backend: ${BETTERFLASH_LOCAL_VULKAN}")
