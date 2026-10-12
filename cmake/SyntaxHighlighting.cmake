include(FetchContent)
include(CMakePackageConfigHelpers)

# Keep framework configuration scoped to its subdirectory and build only the library.
function(betterflash_add_syntax_highlighting)
    FetchContent_Declare(betterflash_ecm
        URL https://download.kde.org/stable/frameworks/6.18/extra-cmake-modules-6.18.0.tar.xz
        URL_HASH SHA256=b2d2222cfe5ace502c60bd8a77a79bc0230f48cee83265c89dcc2618b960e293
        SOURCE_SUBDIR betterflash-unused DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_Declare(betterflash_syntax
        URL https://download.kde.org/stable/frameworks/6.18/syntax-highlighting-6.18.0.tar.xz
        URL_HASH SHA256=8be8057221a982c8d1fe42f95454fac9610eb51975226c6652689293cd335bfd
        SOURCE_SUBDIR betterflash-unused DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(betterflash_ecm betterflash_syntax)
    # ECM is a collection of CMake modules. Expose its source modules directly.
    set(ECM_DIR "${betterflash_ecm_BINARY_DIR}")
    file(MAKE_DIRECTORY "${ECM_DIR}")
    file(WRITE "${ECM_DIR}/ECMConfig.cmake"
        "set(ECM_MODULE_DIR \"${betterflash_ecm_SOURCE_DIR}/modules\")\n"
        "set(ECM_FIND_MODULE_DIR \"${betterflash_ecm_SOURCE_DIR}/find-modules\")\n"
        "set(ECM_KDE_MODULE_DIR \"${betterflash_ecm_SOURCE_DIR}/kde-modules\")\n"
        "set(ECM_MODULE_PATH \"${betterflash_ecm_SOURCE_DIR}/modules;${betterflash_ecm_SOURCE_DIR}/find-modules;${betterflash_ecm_SOURCE_DIR}/kde-modules\")\n"
        "set(ECM_GLOBAL_FIND_VERSION 6.18.0)\n"
        "include(\"${betterflash_ecm_SOURCE_DIR}/modules/ECMUseFindModules.cmake\")\n")
    write_basic_package_version_file("${ECM_DIR}/ECMConfigVersion.cmake"
        VERSION 6.18.0 COMPATIBILITY AnyNewerVersion)
    set(BUILD_SHARED_LIBS OFF)
    # Preserve this scoped library choice when ECM defines its cache default.
    set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
    set(BUILD_TESTING OFF)
    set(BUILD_DOC OFF)
    set(KF_SKIP_PO_PROCESSING ON)
    set(KDE_L10N_AUTO_TRANSLATIONS OFF)
    set(KDE_L10N_SYNC_TRANSLATIONS OFF)
    set(BUILD_QCH OFF)
    set(QRC_SYNTAX ON)
    set(NO_STANDARD_PATHS ON)
    set(KDE_SKIP_UNINSTALL_TARGET ON)
    if(CMAKE_CROSSCOMPILING AND NOT KATEHIGHLIGHTINGINDEXER_EXECUTABLE)
        message(FATAL_ERROR "Cross-compiling code highlighting requires a host katehighlightingindexer. Build the desktop target first and set KATEHIGHLIGHTINGINDEXER_EXECUTABLE to that build's bin/katehighlightingindexer; see docs/code-rendering.md.")
    endif()
    add_subdirectory("${betterflash_syntax_SOURCE_DIR}" "${betterflash_syntax_BINARY_DIR}" EXCLUDE_FROM_ALL)
    get_target_property(betterflash_syntax_type KF6SyntaxHighlighting TYPE)
    if(NOT betterflash_syntax_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "BetterFlash requires the embedded syntax highlighter to build as a static library.")
    endif()
    set_target_properties(KF6SyntaxHighlighting PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()
betterflash_add_syntax_highlighting()
