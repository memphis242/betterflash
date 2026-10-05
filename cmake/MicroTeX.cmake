include(FetchContent)

# Only the native renderer is built; resource files travel with the application.
FetchContent_Declare(microtex
    URL https://codeload.github.com/NanoMichael/MicroTeX/tar.gz/0e3707f6dafebb121d98b53c64364d16fefe481d
    URL_HASH SHA256=47476269d29c41df322bce6bdd2daa7017cc50b9eda841b2bec1767dba28daa6
    SOURCE_SUBDIR betterflash-unused
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(tinyxml2
    URL https://codeload.github.com/leethomason/tinyxml2/tar.gz/refs/tags/11.0.0
    URL_HASH SHA256=5556deb5081fb246ee92afae73efd943c889cef0cafea92b0b82422d6a18f289
    SOURCE_SUBDIR betterflash-unused
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(microtex tinyxml2)

add_library(betterflash_xml STATIC "${tinyxml2_SOURCE_DIR}/tinyxml2.cpp")
target_include_directories(betterflash_xml SYSTEM PUBLIC "${tinyxml2_SOURCE_DIR}")
set_target_properties(betterflash_xml PROPERTIES POSITION_INDEPENDENT_CODE ON)
file(GLOB_RECURSE MICROTEX_SOURCES CONFIGURE_DEPENDS "${microtex_SOURCE_DIR}/src/*.cpp")
list(FILTER MICROTEX_SOURCES EXCLUDE REGEX "/(samples|platform)/")
# Formula glyphs use vector outlines for consistent rasterization at every scale.
file(READ "${microtex_SOURCE_DIR}/src/platform/qt/graphic_qt.cpp" microtex_qt)
string(REPLACE "#include \"graphic_qt.h\"" "#include \"platform/qt/graphic_qt.h\"" microtex_qt "${microtex_qt}")
string(REPLACE "#include <QPainter>" "#include <QPainter>\n#include <QPainterPath>" microtex_qt "${microtex_qt}")
string(REPLACE "_painter->drawText(QPointF(x, y), text);"
    "QPainterPath outline; outline.addText(QPointF(x, y), _font->getQFont(), text); _painter->fillPath(outline, getQBrush());"
    microtex_qt "${microtex_qt}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/microtex_qt.cpp" "${microtex_qt}")
add_library(betterflash_tex STATIC ${MICROTEX_SOURCES}
    "${CMAKE_CURRENT_BINARY_DIR}/microtex_qt.cpp")
target_include_directories(betterflash_tex SYSTEM PUBLIC "${microtex_SOURCE_DIR}/src")
target_compile_definitions(betterflash_tex PUBLIC BUILD_QT)
target_link_libraries(betterflash_tex PRIVATE betterflash_xml Qt6::Gui)
set_target_properties(betterflash_tex PROPERTIES POSITION_INDEPENDENT_CODE ON)
file(GLOB_RECURSE MICROTEX_RESOURCES LIST_DIRECTORIES FALSE "${microtex_SOURCE_DIR}/res/*")
list(APPEND MICROTEX_RESOURCES "${microtex_SOURCE_DIR}/res/.clatexmath-res_root")
