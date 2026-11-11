include(FetchContent)

set(BETTERFLASH_LIBARCHIVE_SOURCE_SHA256
    "ad5dfe0d9a5999494c445b9bc3271d84160950bb1790cf892fb132f563d036b6")
FetchContent_Declare(betterflash_libarchive
    URL "https://codeload.github.com/libarchive/libarchive/tar.gz/refs/tags/v3.8.1"
    URL_HASH "SHA256=${BETTERFLASH_LIBARCHIVE_SOURCE_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_GetProperties(betterflash_libarchive)
if(NOT betterflash_libarchive_POPULATED)
    FetchContent_Populate(betterflash_libarchive)
endif()
set(BETTERFLASH_LIBARCHIVE_SOURCE_DIR "${betterflash_libarchive_SOURCE_DIR}")

if(NOT EXISTS "${BETTERFLASH_LIBARCHIVE_SOURCE_DIR}/libarchive/archive.h"
   OR NOT EXISTS "${BETTERFLASH_LIBARCHIVE_SOURCE_DIR}/libarchive/archive_entry.h")
    message(FATAL_ERROR
        "Pinned libarchive 3.8.1 public headers are missing at "
        "${BETTERFLASH_LIBARCHIVE_SOURCE_DIR}. Supply the verified source tree.")
endif()

if(ANDROID)
    # Android model assets use ZIP and gzip-compressed tar archives. Build the
    # pinned source with the NDK zlib and avoid host-only optional libraries.
    function(betterflash_add_android_libarchive)
        set(ENABLE_OPENSSL OFF)
        set(ENABLE_LIBB2 OFF)
        set(ENABLE_LZ4 OFF)
        set(ENABLE_LZO OFF)
        set(ENABLE_LZMA OFF)
        set(ENABLE_ZSTD OFF)
        set(ENABLE_ZLIB ON)
        set(ENABLE_BZip2 OFF)
        set(ENABLE_LIBXML2 OFF)
        set(ENABLE_EXPAT OFF)
        set(ENABLE_PCREPOSIX OFF)
        set(ENABLE_PCRE2POSIX OFF)
        set(ENABLE_TEST OFF)
        set(ENABLE_INSTALL OFF)
        set(ENABLE_TAR OFF)
        set(ENABLE_CPIO OFF)
        set(ENABLE_CAT OFF)
        set(ENABLE_UNZIP OFF)
        set(ENABLE_WERROR OFF)
        set(BUILD_SHARED_LIBS OFF)
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        add_subdirectory("${BETTERFLASH_LIBARCHIVE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/libarchive")
        set_target_properties(archive_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endfunction()
    betterflash_add_android_libarchive()
    add_library(betterflash_archive INTERFACE)
    target_link_libraries(betterflash_archive INTERFACE archive_static)
    target_include_directories(betterflash_archive SYSTEM INTERFACE
        "${BETTERFLASH_LIBARCHIVE_SOURCE_DIR}/libarchive")
else()
    find_library(BETTERFLASH_LIBARCHIVE_LIBRARY NAMES archive libarchive.so.13
        PATHS /usr/lib64 /usr/lib /usr/local/lib)
    if(NOT BETTERFLASH_LIBARCHIVE_LIBRARY)
        message(FATAL_ERROR
            "libarchive runtime library was not found. Install it or set "
            "BETTERFLASH_LIBARCHIVE_LIBRARY to a compatible build.")
    endif()
    add_library(betterflash_archive UNKNOWN IMPORTED GLOBAL)
    set_target_properties(betterflash_archive PROPERTIES
        IMPORTED_LOCATION "${BETTERFLASH_LIBARCHIVE_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${BETTERFLASH_LIBARCHIVE_SOURCE_DIR}/libarchive"
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${BETTERFLASH_LIBARCHIVE_SOURCE_DIR}/libarchive")
endif()
