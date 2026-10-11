include(FetchContent)

set(BETTERFLASH_ONNXRUNTIME_VERSION "1.20.1" CACHE STRING
    "Pinned ONNX Runtime version used by local semantic search")
if(NOT BETTERFLASH_ONNXRUNTIME_VERSION STREQUAL "1.20.1")
    message(FATAL_ERROR "Only ONNX Runtime 1.20.1 has a pinned archive checksum")
endif()

if(ANDROID)
    set(BETTERFLASH_ONNXRUNTIME_ANDROID_VERSION "1.20.0" CACHE STRING
        "Pinned Android ONNX Runtime AAR version")
    if(NOT BETTERFLASH_ONNXRUNTIME_ANDROID_VERSION STREQUAL "1.20.0")
        message(FATAL_ERROR "Only Android ONNX Runtime 1.20.0 has a pinned AAR checksum")
    endif()
    FetchContent_Declare(betterflash_onnxruntime
        URL "https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/1.20.0/onnxruntime-android-1.20.0.aar"
        DOWNLOAD_NAME "onnxruntime-android-1.20.0.zip"
        URL_HASH SHA256=07a8f71ef890afed8c6087a56220e6d558a492804276ee2dd7cb7f6262242027
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
else()
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        message(FATAL_ERROR "Local semantic search currently pins ONNX Runtime only for Linux x86_64")
    endif()
    FetchContent_Declare(betterflash_onnxruntime
        URL "https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-linux-x64-1.20.1.tgz"
        URL_HASH SHA256=67db4dc1561f1e3fd42e619575c82c601ef89849afc7ea85a003abbac1a1a105
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()
FetchContent_MakeAvailable(betterflash_onnxruntime)

set(BETTERFLASH_ONNXRUNTIME_ROOT
    "${betterflash_onnxruntime_SOURCE_DIR}")
if(ANDROID)
    if(NOT EXISTS "${BETTERFLASH_ONNXRUNTIME_ROOT}/headers/onnxruntime_cxx_api.h")
        message(FATAL_ERROR "Pinned ONNX Runtime AAR has an unexpected layout")
    endif()
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(BETTERFLASH_ONNXRUNTIME_ABI "arm64-v8a")
    elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
        set(BETTERFLASH_ONNXRUNTIME_ABI "armeabi-v7a")
    elseif(ANDROID_ABI STREQUAL "x86")
        set(BETTERFLASH_ONNXRUNTIME_ABI "x86")
    elseif(ANDROID_ABI STREQUAL "x86_64")
        set(BETTERFLASH_ONNXRUNTIME_ABI "x86_64")
    else()
        message(FATAL_ERROR "Unsupported Android ABI for pinned ONNX Runtime AAR: ${ANDROID_ABI}")
    endif()
    set(BETTERFLASH_ONNXRUNTIME_INCLUDE_DIR "${BETTERFLASH_ONNXRUNTIME_ROOT}/headers")
    set(BETTERFLASH_ONNXRUNTIME_LIBRARY
        "${BETTERFLASH_ONNXRUNTIME_ROOT}/jni/${BETTERFLASH_ONNXRUNTIME_ABI}/libonnxruntime.so")
else()
    if(NOT EXISTS "${BETTERFLASH_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "Pinned ONNX Runtime archive has an unexpected layout")
    endif()
    set(BETTERFLASH_ONNXRUNTIME_INCLUDE_DIR "${BETTERFLASH_ONNXRUNTIME_ROOT}/include")
    set(BETTERFLASH_ONNXRUNTIME_LIBRARY
        "${BETTERFLASH_ONNXRUNTIME_ROOT}/lib/libonnxruntime.so.1.20.1")
endif()

add_library(betterflash_onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(betterflash_onnxruntime PROPERTIES
    IMPORTED_LOCATION "${BETTERFLASH_ONNXRUNTIME_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${BETTERFLASH_ONNXRUNTIME_INCLUDE_DIR}")
if(NOT ANDROID)
    set_property(TARGET betterflash_onnxruntime PROPERTY IMPORTED_SONAME "libonnxruntime.so.1")
endif()
