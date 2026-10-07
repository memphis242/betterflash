cmake_minimum_required(VERSION 3.25)
project(BetterFlash VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Sql Network Multimedia TextToSpeech Concurrent)
qt_standard_project_setup(REQUIRES 6.6)
include(cmake/MicroTeX.cmake)

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/core/*.h)
add_library(betterflash_core STATIC ${CORE_SOURCES})
target_include_directories(betterflash_core PUBLIC src)
target_link_libraries(betterflash_core PUBLIC Qt6::Core Qt6::Gui Qt6::Sql)

file(GLOB VOICE_SOURCES CONFIGURE_DEPENDS src/voice/*.cpp src/voice/*.h)
add_library(betterflash_voice STATIC ${VOICE_SOURCES})
target_include_directories(betterflash_voice PUBLIC src)
target_link_libraries(betterflash_voice PUBLIC Qt6::Core Qt6::Multimedia Qt6::TextToSpeech Qt6::Network)

add_library(betterflash_render STATIC src/render/MarkdownView.cpp src/render/MarkdownView.h)
target_include_directories(betterflash_render PUBLIC src)
target_link_libraries(betterflash_render PUBLIC Qt6::Quick Qt6::Network PRIVATE betterflash_tex)
qt_add_resources(betterflash_render math_assets PREFIX /tex BASE "${microtex_SOURCE_DIR}/res"
    FILES ${MICROTEX_RESOURCES})

file(GLOB INTEGRATION_SOURCES CONFIGURE_DEPENDS src/integrations/*.cpp src/integrations/*.h)
add_library(betterflash_integrations STATIC ${INTEGRATION_SOURCES})
target_include_directories(betterflash_integrations PUBLIC src)
target_link_libraries(betterflash_integrations PUBLIC betterflash_core Qt6::Network Qt6::Gui Qt6::Concurrent)
target_link_libraries(betterflash_voice PRIVATE betterflash_integrations)

qt_add_executable(betterflash src/main.cpp)
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
    betterflash_integrations Qt6::QuickControls2)
target_compile_options(betterflash PRIVATE -Wall -Wextra -Wpedantic)
foreach(target betterflash_core betterflash_voice betterflash_render betterflash_integrations)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

if(ANDROID)
    set(BETTERFLASH_ANDROID_OPENSSL_LIB_DIR "" CACHE PATH "Directory containing Android arm64 OpenSSL 3 libraries")
    foreach(library libcrypto_3.so libssl_3.so)
        if(NOT EXISTS "${BETTERFLASH_ANDROID_OPENSSL_LIB_DIR}/${library}")
            message(FATAL_ERROR "Android HTTPS requires ${library}. Set BETTERFLASH_ANDROID_OPENSSL_LIB_DIR to the matching arm64 OpenSSL 3 directory; see docs/android.md.")
        endif()
    endforeach()
    set_property(TARGET betterflash PROPERTY QT_ANDROID_EXTRA_LIBS
        "${BETTERFLASH_ANDROID_OPENSSL_LIB_DIR}/libcrypto_3.so"
        "${BETTERFLASH_ANDROID_OPENSSL_LIB_DIR}/libssl_3.so")
    set_property(TARGET betterflash PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    set_property(TARGET betterflash PROPERTY QT_ANDROID_PACKAGE_NAME "org.betterflash.app")
    set_property(TARGET betterflash PROPERTY QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}")
    set_property(TARGET betterflash PROPERTY QT_ANDROID_VERSION_CODE 1)
    set_property(TARGET betterflash PROPERTY QT_ANDROID_MIN_SDK_VERSION 28)
    set_property(TARGET betterflash PROPERTY QT_ANDROID_TARGET_SDK_VERSION 36)
endif()

include(GNUInstallDirs)
install(TARGETS betterflash RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
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
