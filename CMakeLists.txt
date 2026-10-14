cmake_minimum_required(VERSION 3.25)
project(BetterFlash VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Sql Network Multimedia TextToSpeech Concurrent)
qt_standard_project_setup(REQUIRES 6.7)
include(cmake/MicroTeX.cmake)
include(cmake/OnnxRuntime.cmake)
include(cmake/SyntaxHighlighting.cmake)
include(cmake/LocalModels.cmake)
include(cmake/ModelArchive.cmake)
include(cmake/LocalSpeech.cmake)
include(GNUInstallDirs)

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/core/*.h)
add_library(betterflash_core STATIC ${CORE_SOURCES})
option(BETTERFLASH_DEVELOPMENT_TOOLS "Enable development-only review reset controls" OFF)
if(BETTERFLASH_DEVELOPMENT_TOOLS)
    target_compile_definitions(betterflash_core PUBLIC BETTERFLASH_DEVELOPMENT_TOOLS)
endif()
target_include_directories(betterflash_core PUBLIC src)
target_link_libraries(betterflash_core PUBLIC Qt6::Core Qt6::Gui Qt6::Sql)

file(GLOB MODEL_SOURCES CONFIGURE_DEPENDS src/models/*.cpp src/models/*.h)
add_library(betterflash_models STATIC ${MODEL_SOURCES})
target_include_directories(betterflash_models PUBLIC src)
target_link_libraries(betterflash_models PUBLIC Qt6::Core Qt6::Network Qt6::Concurrent
    PRIVATE betterflash_archive llama-common)
target_compile_options(betterflash_models PRIVATE -Wall -Wextra -Wpedantic)

file(GLOB VOICE_SOURCES CONFIGURE_DEPENDS src/voice/*.cpp src/voice/*.h)
add_library(betterflash_voice STATIC ${VOICE_SOURCES})
target_include_directories(betterflash_voice PUBLIC src)
target_link_libraries(betterflash_voice PUBLIC Qt6::Core Qt6::Multimedia Qt6::TextToSpeech Qt6::Network betterflash_models PRIVATE betterflash_local_speech)

file(GLOB RENDER_SOURCES CONFIGURE_DEPENDS src/render/*.cpp src/render/*.h)
add_library(betterflash_render STATIC ${RENDER_SOURCES})
target_include_directories(betterflash_render PUBLIC src)
target_link_libraries(betterflash_render PUBLIC Qt6::Quick Qt6::Network KF6SyntaxHighlighting PRIVATE betterflash_tex)
file(GLOB CODE_THEMES CONFIGURE_DEPENDS src/render/themes/*.theme)
qt_add_resources(betterflash_render code_themes PREFIX /betterflash-code/themes BASE src/render/themes FILES ${CODE_THEMES})
qt_add_resources(betterflash_render math_assets PREFIX /tex BASE "${microtex_SOURCE_DIR}/res"
    FILES ${MICROTEX_RESOURCES})

file(GLOB INTEGRATION_SOURCES CONFIGURE_DEPENDS src/integrations/*.cpp src/integrations/*.h)
add_library(betterflash_integrations STATIC ${INTEGRATION_SOURCES})
target_include_directories(betterflash_integrations PUBLIC src)
target_link_libraries(betterflash_integrations PUBLIC betterflash_core Qt6::Network Qt6::Gui Qt6::Concurrent betterflash_models)
target_link_libraries(betterflash_voice PRIVATE betterflash_integrations)

file(GLOB SEARCH_SOURCES CONFIGURE_DEPENDS src/search/*.cpp src/search/*.h)
add_library(betterflash_search STATIC ${SEARCH_SOURCES})
target_include_directories(betterflash_search PUBLIC src)
target_link_libraries(betterflash_search PUBLIC betterflash_core betterflash_integrations Qt6::Network Qt6::Concurrent
    betterflash_onnxruntime)
target_compile_options(betterflash_search PRIVATE -Wall -Wextra -Wpedantic)

qt_add_executable(betterflash src/main.cpp src/ui/TextFocusDismissal.cpp src/ui/TextFocusDismissal.h)
if(NOT ANDROID AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    include(cmake/LocalWhisper.cmake)
endif()
set(BETTERFLASH_ICON_SIZES 16 24 32 48 64 128 256 512)
set(BETTERFLASH_ICON_FILES)
foreach(icon_size IN LISTS BETTERFLASH_ICON_SIZES)
    list(APPEND BETTERFLASH_ICON_FILES "packaging/icons/${icon_size}x${icon_size}/betterflash.png")
endforeach()
qt_add_resources(betterflash application_icons PREFIX /icons BASE packaging/icons
    FILES ${BETTERFLASH_ICON_FILES})
qt_add_resources(betterflash branding_assets PREFIX /branding BASE packaging/branding
    FILES packaging/branding/walnut.png packaging/branding/walnut-blueberry.png
    packaging/branding/walnut-spinach.png)
qt_add_resources(betterflash logo_study_assets PREFIX /logo-studies BASE packaging/branding/tea-studies
    FILES packaging/branding/tea-studies/tea-01-ceramic.png
    packaging/branding/tea-studies/tea-02-glass.png
    packaging/branding/tea-studies/tea-03-porcelain.png
    packaging/branding/tea-studies/blueberry.png)
qt_add_resources(betterflash application_fonts PREFIX /fonts BASE packaging/fonts
    FILES packaging/fonts/IBMPlexMono-Regular.ttf packaging/fonts/IBMPlexMono-Bold.ttf
    packaging/fonts/IBMPlexMono-Italic.ttf packaging/fonts/IBMPlexMono-BoldItalic.ttf)
file(GLOB LICENSE_NOTICES CONFIGURE_DEPENDS packaging/licenses/*.txt)
qt_add_resources(betterflash license_notices PREFIX /licenses BASE packaging/licenses FILES ${LICENSE_NOTICES})
file(GLOB_RECURSE QML_SOURCES CONFIGURE_DEPENDS qml/*.qml qml/*.js)
foreach(qml_file IN LISTS QML_SOURCES)
    file(RELATIVE_PATH qml_alias "${CMAKE_CURRENT_SOURCE_DIR}/qml" "${qml_file}")
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_alias}")
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(betterflash URI BetterFlash VERSION 1.0 QML_FILES ${QML_SOURCES}
    NO_CACHEGEN)
target_link_libraries(betterflash PRIVATE betterflash_core betterflash_voice betterflash_render
    betterflash_integrations betterflash_search Qt6::QuickControls2)
set_target_properties(betterflash PROPERTIES
    BUILD_RPATH "${BETTERFLASH_ONNXRUNTIME_ROOT}/lib"
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
target_compile_options(betterflash PRIVATE -Wall -Wextra -Wpedantic)
foreach(target betterflash_core betterflash_voice betterflash_render betterflash_integrations betterflash_search)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

if(ANDROID)
    set(BETTERFLASH_ANDROID_OPENSSL_LIB_DIR "" CACHE PATH "Directory containing Android arm64 OpenSSL 3 libraries")
    foreach(library libcrypto_3.so libssl_3.so)
        if(NOT EXISTS "${BETTERFLASH_ANDROID_OPENSSL_LIB_DIR}/${library}")
            message(FATAL_ERROR "Android HTTPS requires ${library}. Set BETTERFLASH_ANDROID_OPENSSL_LIB_DIR to the matching arm64 OpenSSL 3 directory; see docs/android.md.")
        endif()
    endforeach()
    set_property(TARGET betterflash APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
        "${BETTERFLASH_ANDROID_OPENSSL_LIB_DIR}/libcrypto_3.so"
        "${BETTERFLASH_ANDROID_OPENSSL_LIB_DIR}/libssl_3.so"
        "${BETTERFLASH_ONNXRUNTIME_LIBRARY}")
    set_property(TARGET betterflash PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    set_property(TARGET betterflash PROPERTY QT_ANDROID_PACKAGE_NAME "org.betterflash.app")
    set_property(TARGET betterflash PROPERTY QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}")
    set_property(TARGET betterflash PROPERTY QT_ANDROID_VERSION_CODE 1)
    set_property(TARGET betterflash PROPERTY QT_ANDROID_MIN_SDK_VERSION 28)
    set_property(TARGET betterflash PROPERTY QT_ANDROID_TARGET_SDK_VERSION 36)
endif()

install(TARGETS betterflash RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(NOT ANDROID AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    install(PROGRAMS "${BETTERFLASH_WHISPER_RUNTIME}" DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(ANDROID)
    install(FILES "${BETTERFLASH_ONNXRUNTIME_LIBRARY}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
else()
    install(DIRECTORY "${BETTERFLASH_ONNXRUNTIME_ROOT}/lib/"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        FILES_MATCHING PATTERN "libonnxruntime.so*")
endif()
install(FILES packaging/org.betterflash.BetterFlash.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
foreach(icon_size IN LISTS BETTERFLASH_ICON_SIZES)
    install(FILES "packaging/icons/${icon_size}x${icon_size}/betterflash.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${icon_size}x${icon_size}/apps")
endforeach()
install(FILES ${LICENSE_NOTICES} THIRD_PARTY.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/betterflash)

include(CTest)
if(BUILD_TESTING AND NOT ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/verification/GuiSweep.cpp")
        target_sources(betterflash PRIVATE src/verification/GuiSweep.cpp src/verification/GuiSweep.h
            src/verification/QueueDesignCapture.cpp src/verification/QueueDesignCapture.h
            src/verification/ReviewControlCapture.cpp src/verification/ReviewControlCapture.h)
        target_link_libraries(betterflash PRIVATE Qt6::Test)
        target_compile_definitions(betterflash PRIVATE BETTERFLASH_GUI_SWEEP)
        add_test(NAME gui COMMAND "${CMAKE_COMMAND}"
            "-DBETTERFLASH_EXECUTABLE=$<TARGET_FILE:betterflash>"
            "-DBETTERFLASH_ARTIFACT_DIR=${CMAKE_CURRENT_BINARY_DIR}/artifacts/gui"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GuiSweep.cmake")
        add_test(NAME gui-utc14 COMMAND "${CMAKE_COMMAND}"
            "-DBETTERFLASH_EXECUTABLE=$<TARGET_FILE:betterflash>"
            "-DBETTERFLASH_ARTIFACT_DIR=${CMAKE_CURRENT_BINARY_DIR}/artifacts/gui-utc14"
            "-DBETTERFLASH_SWEEP_TIMEZONE=Pacific/Kiritimati"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GuiSweep.cmake")
        set_tests_properties(gui gui-utc14 PROPERTIES TIMEOUT 200)
    endif()
    foreach(test_name core voice)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${test_name}_test.cpp")
            qt_add_executable(${test_name}_test "tests/${test_name}_test.cpp")
            target_link_libraries(${test_name}_test PRIVATE betterflash_${test_name} Qt6::Test)
            add_test(NAME ${test_name} COMMAND ${test_name}_test)
            set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        endif()
    endforeach()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/render_test.cpp")
        qt_add_executable(render_test tests/render_test.cpp)
        target_link_libraries(render_test PRIVATE betterflash_render Qt6::Test)
        add_test(NAME render COMMAND render_test)
        set_tests_properties(render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_test.cpp")
        qt_add_executable(integration_test tests/integration_test.cpp)
        target_link_libraries(integration_test PRIVATE betterflash_integrations Qt6::Test)
        target_compile_definitions(integration_test PRIVATE BETTERFLASH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME integrations COMMAND integration_test)
        set_tests_properties(integrations PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/provider_test.cpp")
        qt_add_executable(provider_test tests/provider_test.cpp)
        target_link_libraries(provider_test PRIVATE betterflash_integrations Qt6::Test)
        add_test(NAME providers COMMAND provider_test)
        set_tests_properties(providers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME sync-server COMMAND "${Python3_EXECUTABLE}" -W error::ResourceWarning
        -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/server/tests")
endif()
