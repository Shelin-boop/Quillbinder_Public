cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# The application name lives in exactly one place. Change APP_NAME /
# APP_ID to rename the program everywhere (window titles, config folders,
# desktop file, AppImage file name).
# ---------------------------------------------------------------------------
set(APP_NAME "Quillbinder")
set(APP_ID   "quillbinder")
set(APP_VERSION "0.64.2")
# A word shown after the version number in Help > About, System Information and the
# AppImage's file name ("alpha", "beta"; empty for a normal release). It is never part
# of version comparisons: the program, the loader and the packages compare APP_VERSION only.
set(APP_VERSION_LABEL "alpha")
if(APP_VERSION_LABEL)
  set(APP_VERSION_SHOWN "${APP_VERSION} ${APP_VERSION_LABEL}")
else()
  set(APP_VERSION_SHOWN "${APP_VERSION}")
endif()
# Identifies the set of libraries inside the AppImage. Program updates
# (.qbpkg "core" packages) only run on the runtime they were built for;
# change it whenever the bundled libraries change (new Qt, new modules).
set(APP_RUNTIME "u2404-qt6.4-r1")
if(WIN32)
  # Windows (port, W2): Qt 6.8.3 for MSVC with its DLLs in the program folder; program updates
  # for Windows must be built with the same Qt (.github/workflows/windows.yml pins it)
  set(APP_RUNTIME "win64-qt6.8.3-msvc-r1")
endif()
# The public project page (0.50): downloads, and the release "Third-party source code" with the
# source of every library the AppImage and the add-ons carry (Help > Third-Party Licenses).
set(APP_HOMEPAGE "https://github.com/Shelin-boop/Quillbinder_Public")

project(${APP_ID} VERSION ${APP_VERSION} LANGUAGES CXX)

# Switches of the tests that load plugins from other folders or leave checks out (QB_PLUGIN_PATH,
# QB_SPEECH_SOURCE, QB_SPEECH_ACCEPT_CHANGED, QB_SPEECH_SKIP_TEST): only in builds with the tests
# (0.64.1, the external audit's X-10). The packages are built with -DQB_BUILD_TESTS=OFF.
if(DEFINED QB_BUILD_TESTS)
  set(QB_TEST_SWITCHES_DEFAULT ${QB_BUILD_TESTS})
else()
  set(QB_TEST_SWITCHES_DEFAULT ON)
endif()
option(QB_TEST_SWITCHES "Honor the test switches that load plugins from other folders or skip checks" ${QB_TEST_SWITCHES_DEFAULT})
if(QB_TEST_SWITCHES)
  add_compile_definitions(QB_TEST_SWITCHES=1)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Windows with MSVC (the port, W1): the sources are UTF-8, M_PI comes from <cmath>, and
# windows.h must not define min/max. Linux builds are not affected.
if(MSVC)
  add_compile_options(/utf-8 /bigobj)
  add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
  # a symbols file (.pdb) for every release build (W2): crash reports name program offsets, the
  # .pdb turns them into function names. /Z7 keeps the debug data in the objects (sccache can cache
  # them); the linker writes the .pdb.
  add_compile_options($<$<CONFIG:Release>:/Z7>)
  add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()

# Network: local sockets between running instances (the AppImage has carried libQt6Network since 0.2.0).
find_package(Qt6 REQUIRED COMPONENTS Core Gui Xml Widgets Svg PrintSupport Pdf PdfWidgets Network)
# Page view sets the page margins of a text layout through a private Qt class
# (src/text/PageLayout.cpp): needs qt6-base-private-dev of the same Qt.
find_package(Qt6 QUIET COMPONENTS GuiPrivate)
if(NOT TARGET Qt6::GuiPrivate)
  message(FATAL_ERROR "The private Qt GUI headers are missing (Ubuntu: qt6-base-private-dev).")
endif()
find_package(ZLIB REQUIRED)
find_package(PkgConfig)
if(PkgConfig_FOUND)
  pkg_check_modules(HUNSPELL IMPORTED_TARGET hunspell)
endif()
if(HUNSPELL_FOUND)
  set(QB_HUNSPELL_TARGET PkgConfig::HUNSPELL)
else()
  # Without pkg-config (Windows, Hunspell from vcpkg): look for the library itself.
  find_path(QB_HUNSPELL_INCLUDE hunspell/hunspell.h)
  find_library(QB_HUNSPELL_LIB NAMES hunspell-1.7 libhunspell-1.7 hunspell)
  if(QB_HUNSPELL_INCLUDE AND QB_HUNSPELL_LIB)
    add_library(qb_hunspell INTERFACE)
    target_include_directories(qb_hunspell INTERFACE ${QB_HUNSPELL_INCLUDE})
    target_link_libraries(qb_hunspell INTERFACE ${QB_HUNSPELL_LIB})
    # a static Hunspell from vcpkg also needs libiconv
    find_library(QB_ICONV_LIB NAMES iconv libiconv)
    find_library(QB_CHARSET_LIB NAMES charset libcharset)
    foreach(lib QB_ICONV_LIB QB_CHARSET_LIB)
      if(${lib})
        target_link_libraries(qb_hunspell INTERFACE ${${lib}})
      endif()
    endforeach()
    set(QB_HUNSPELL_TARGET qb_hunspell)
  endif()
endif()

configure_file(src/app/AppInfo.h.in ${CMAKE_BINARY_DIR}/generated/AppInfo.h @ONLY)

# Core library: project model, text formats, compile, import (no widgets).
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
  src/core/*.cpp src/core/*.h src/text/*.cpp src/text/*.h
  src/compile/*.cpp src/compile/*.h src/import/*.cpp src/import/*.h)
add_library(qbcore STATIC ${CORE_SOURCES})
target_include_directories(qbcore PUBLIC src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(qbcore PUBLIC Qt6::Core Qt6::Gui Qt6::Xml ZLIB::ZLIB)
target_link_libraries(qbcore PRIVATE Qt6::GuiPrivate)

# User interface library (shared by the program and the UI tests).
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp src/ui/*.h)
add_library(qbui STATIC ${UI_SOURCES})
target_link_libraries(qbui PUBLIC qbcore
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::PrintSupport Qt6::Pdf Qt6::PdfWidgets Qt6::Network)

# Windows (W3): the same resources with the license list of the Windows program (tools/gen-resources.py)
if(WIN32)
  set(QB_RESOURCES_QRC ${CMAKE_SOURCE_DIR}/resources/resources-windows.qrc)
else()
  set(QB_RESOURCES_QRC ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
endif()
add_executable(${APP_ID} src/main.cpp ${QB_RESOURCES_QRC})
target_link_libraries(${APP_ID} PRIVATE qbui)
if(WIN32)
  set_target_properties(${APP_ID} PROPERTIES WIN32_EXECUTABLE ON) # no console window
endif()
if(QB_HUNSPELL_TARGET)
  target_link_libraries(qbcore PUBLIC ${QB_HUNSPELL_TARGET})
  target_compile_definitions(qbcore PUBLIC QB_HAVE_HUNSPELL=1)
endif()
# Plugins must see the exporter interface symbols of the executable.
set_target_properties(${APP_ID} PROPERTIES ENABLE_EXPORTS ON)

install(TARGETS ${APP_ID} RUNTIME DESTINATION bin)
install(FILES packaging/${APP_ID}.desktop DESTINATION share/applications)
install(FILES resources/app/${APP_ID}.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES packaging/${APP_ID}-mime.xml DESTINATION share/mime/packages)
install(FILES packaging/${APP_ID}.metainfo.xml DESTINATION share/metainfo RENAME io.github.shelin_boop.Quillbinder.metainfo.xml)
install(DIRECTORY plugins/command/ DESTINATION share/${APP_ID}/plugins FILES_MATCHING PATTERN "*.json")
# Make the command plugins available when running from the build folder.
file(COPY plugins/command/ DESTINATION ${CMAKE_BINARY_DIR}/plugins FILES_MATCHING PATTERN "*.json")

# Example C++ exporter plugin (Markdown). Shows how third parties add formats.
option(QB_BUILD_EXAMPLE_PLUGIN "Build the example exporter plugin (Markdown)" ON)
if(QB_BUILD_EXAMPLE_PLUGIN)
  add_subdirectory(plugins/markdown-exporter)
endif()

# Writing tools (tools/ToolApi.h). They are delivered as add-ons, not with the
# program update: the libraries go to build/tool-plugins, from where
# packaging/build-addons.sh puts them into the add-ons (addons/tool-*).
# Run from the build folder with QB_PLUGIN_PATH=build/tool-plugins QB_TOOL_DATA=addons/tool-name-generator/payload
option(QB_BUILD_TOOL_PLUGINS "Build the writing tools" ON)
if(QB_BUILD_TOOL_PLUGINS)
  add_subdirectory(plugins/tools)
endif()

# Importers (src/import/ImporterApi.h): .doc files and web pages, add-ons as well (addons/import-*).
option(QB_BUILD_IMPORTER_PLUGINS "Build the importer add-ons' plugins" ON)
if(QB_BUILD_IMPORTER_PLUGINS)
  add_subdirectory(plugins/importers)
endif()

# Media playback (src/media/MediaApi.h): an add-on as well (addons/media-playback).
# Needs Qt Multimedia at build time (Ubuntu: qt6-multimedia-dev); the program itself does not.
find_package(Qt6 QUIET COMPONENTS Multimedia)
option(QB_BUILD_MEDIA_PLUGIN "Build the media playback add-on's libraries" ${Qt6Multimedia_FOUND})
if(QB_BUILD_MEDIA_PLUGIN)
  add_subdirectory(plugins/media)
endif()

# Tests (headless round-trip checks)
option(QB_BUILD_TESTS "Build tests" ON)
if(QB_BUILD_TESTS)
  if(WIN32)
    # Windows (W3): only the suites tools/win-tests.sh names are built (cmake --build build --target ...)
    add_subdirectory(tests EXCLUDE_FROM_ALL)
  else()
    add_subdirectory(tests)
  endif()
endif()

# Developer tools (not installed)
option(QB_BUILD_TOOLS "Build developer tools" ON)
if(QB_BUILD_TOOLS)
  add_executable(qb_template tools/qb-template.cpp)
  target_link_libraries(qb_template PRIVATE qbcore)
endif()
