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
    find_library(BETTERFLASH_LIBARCHIVE_LIBRARY NAMES archive libarchive.so.13)
    if(NOT BETTERFLASH_LIBARCHIVE_LIBRARY)
        message(FATAL_ERROR
            "Android needs a target libarchive build. The host Fedora libarchive "
            "cannot be linked into an Android application.")
    endif()
else()
    find_library(BETTERFLASH_LIBARCHIVE_LIBRARY NAMES archive libarchive.so.13
        PATHS /usr/lib64 /usr/lib /usr/local/lib)
    if(NOT BETTERFLASH_LIBARCHIVE_LIBRARY)
        message(FATAL_ERROR
            "libarchive runtime library was not found. Install it or set "
            "BETTERFLASH_LIBARCHIVE_LIBRARY to a compatible build.")
    endif()
endif()

add_library(betterflash_archive UNKNOWN IMPORTED GLOBAL)
set_target_properties(betterflash_archive PROPERTIES
    IMPORTED_LOCATION "${BETTERFLASH_LIBARCHIVE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${BETTERFLASH_LIBARCHIVE_SOURCE_DIR}/libarchive"
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${BETTERFLASH_LIBARCHIVE_SOURCE_DIR}/libarchive")
