cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# The application name lives in exactly one place. Change APP_NAME /
# APP_ID to rename the program everywhere (window titles, config folders,
# desktop file, AppImage file name).
# ---------------------------------------------------------------------------
set(APP_NAME "Quillbinder")
set(APP_ID   "quillbinder")
set(APP_VERSION "0.90.0")
# A word shown after the version number in Help > About, System Information and the
# AppImage's file name ("alpha", "beta"; empty for a normal release). It is never part
# of version comparisons: the program, the loader and the packages compare APP_VERSION only.
set(APP_VERSION_LABEL "beta")
if(APP_VERSION_LABEL)
  set(APP_VERSION_SHOWN "${APP_VERSION} ${APP_VERSION_LABEL}")
else()
  set(APP_VERSION_SHOWN "${APP_VERSION}")
endif()
# Identifies the set of libraries inside the AppImage. Program updates
# (.qbpkg "core" packages) only run on the runtime they were built for;
# change it whenever the bundled libraries change (new Qt, new modules).
set(APP_RUNTIME "u2404-qt6.12-r1")
# the first program version of that runtime: add-ons with libraries built for it need it (tools/make-package.py)
set(APP_RUNTIME_SINCE "0.80.0")
# The Qt of the Linux build (0.80.0): The Qt Company's own binaries of this version (tools/get-qt.sh puts them
# into /opt/qt/<version>/gcc_64; the release workflow installs the same with aqtinstall), no longer Ubuntu's
# Qt 6.4.2. The runtime id above names it; change both together.
set(QB_QT_VERSION "6.12.0")
# The day that Qt version was released (its tag in qt/qtbase): Help > System Information shows the runtime's age
# (the external audit of 2026-11-09, X-7).
set(APP_RUNTIME_DATE "2026-09-30")
if(WIN32)
  # Windows (port, W2; Qt 6.12.0 since W4a, 2026-11-11, the same Qt as Linux): The Qt Company's build of
  # QB_QT_VERSION for MSVC with its DLLs in the program folder; program updates for Windows must be built with the
  # same Qt (.github/workflows/windows.yml pins it). The test builds of 0.60.5 had win64-qt6.8.3-msvc-r1.
  set(APP_RUNTIME "win64-qt6.12-msvc-r1")
endif()
# The public project page (0.50): downloads, and the release "Third-party source code" with the
# source of every library the AppImage and the add-ons carry (Help > Third-Party Licenses).
set(APP_HOMEPAGE "https://github.com/Shelin-boop/Quillbinder_Public")

project(${APP_ID} VERSION ${APP_VERSION} LANGUAGES C CXX) # C: Monocypher (src/core/third_party, 0.65.0)

# Switches of the tests that load plugins from other folders or leave checks out (QB_PLUGIN_PATH,
# QB_SPEECH_SOURCE, QB_SPEECH_ACCEPT_CHANGED, QB_SPEECH_SKIP_TEST, QB_TEST_SIGNING_KEY, unsigned program
# updates): 0.70.0 (audit A S-04) off unless asked for, so that a program built as the README says behaves
# like a release (signatures required). The test runs build with -DQB_TEST_SWITCHES=ON.
# Fuzzing (0.80.0, tests/fuzz/README.md): a build of its own with clang, every part instrumented for libFuzzer.
option(QB_FUZZ "Build the libFuzzer harnesses of the readers (clang only)" OFF)
if(QB_FUZZ)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "QB_FUZZ needs clang (CC=clang CXX=clang++)")
  endif()
  add_compile_options(-fsanitize=fuzzer-no-link,address -g)
  add_link_options(-fsanitize=address)
endif()

option(QB_TEST_SWITCHES "Honor the test switches that load plugins from other folders or skip checks (test builds only)" OFF)
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

# Linux (0.80.0): build with the Qt named above. QB_QT_DIR (cache or environment) names another folder of the
# same version; tools/get-qt.sh fetches it in a cloud session.
if(NOT WIN32)
  if(NOT QB_QT_DIR)
    if(DEFINED ENV{QB_QT_DIR})
      set(QB_QT_DIR "$ENV{QB_QT_DIR}")
    else()
      set(QB_QT_DIR "/opt/qt/${QB_QT_VERSION}/gcc_64")
    endif()
  endif()
  set(QB_QT_DIR "${QB_QT_DIR}" CACHE PATH "Qt ${QB_QT_VERSION} for the Linux build (runtime ${APP_RUNTIME})")
  list(PREPEND CMAKE_PREFIX_PATH "${QB_QT_DIR}")
endif()
# Network: local sockets between running instances (the AppImage has carried libQt6Network since 0.2.0).
find_package(Qt6 REQUIRED COMPONENTS Core Gui Xml Widgets Svg PrintSupport Pdf PdfWidgets Network)
# Page view sets the page margins of a text layout through a private Qt class
# (src/text/PageLayout.cpp) and FontFaces.cpp: the private headers of the same Qt.
set(QT_NO_PRIVATE_MODULE_WARNING ON) # (Qt 6.10 and later warn about private modules; used knowingly)
find_package(Qt6 QUIET COMPONENTS GuiPrivate)
if(NOT TARGET Qt6::GuiPrivate)
  message(FATAL_ERROR "The private Qt GUI headers are missing (Qt ${QB_QT_VERSION}: tools/get-qt.sh).")
endif()
if(NOT Qt6_VERSION VERSION_EQUAL QB_QT_VERSION)
  if(WIN32)
    message(FATAL_ERROR "Qt ${Qt6_VERSION} found in ${Qt6_DIR}; the runtime ${APP_RUNTIME} needs Qt ${QB_QT_VERSION} "
                        "(the Windows workflow installs it, .github/workflows/windows.yml)")
  endif()
  message(FATAL_ERROR "Qt ${Qt6_VERSION} found in ${Qt6_DIR}; the runtime ${APP_RUNTIME} needs Qt ${QB_QT_VERSION} "
                      "(tools/get-qt.sh, or -DQB_QT_DIR=<folder of Qt ${QB_QT_VERSION}>)")
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
add_library(qbcore STATIC ${CORE_SOURCES}
  # package signatures (0.65.0): Monocypher's Ed25519 check, unchanged third-party C code
  src/core/third_party/monocypher/monocypher.c src/core/third_party/monocypher/monocypher-ed25519.c)
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
# 0.70.0: the German texts (translations/README.md)
set(QB_TRANSLATIONS_QRC ${CMAKE_SOURCE_DIR}/translations/translations.qrc)
add_executable(${APP_ID} src/main.cpp ${QB_RESOURCES_QRC} ${QB_TRANSLATIONS_QRC})
target_link_libraries(${APP_ID} PRIVATE qbui)
if(WIN32)
  set_target_properties(${APP_ID} PROPERTIES WIN32_EXECUTABLE ON) # no console window
  # W4b: the program icon for Explorer, shortcuts and the Start menu; the Start menu entry, the desktop shortcut and the
  # project file types (src/ui/WinShell.cpp) use Windows' shell (COM) and the registry
  target_sources(${APP_ID} PRIVATE ${CMAKE_SOURCE_DIR}/resources/app/quillbinder.rc)
  target_link_libraries(qbui PRIVATE ole32 uuid shell32)
endif()
if(QB_HUNSPELL_TARGET)
  target_link_libraries(qbcore PUBLIC ${QB_HUNSPELL_TARGET})
  target_compile_definitions(qbcore PUBLIC QB_HAVE_HUNSPELL=1)
endif()
# Plugins must see the exporter interface symbols of the executable.
set_target_properties(${APP_ID} PROPERTIES ENABLE_EXPORTS ON)

install(TARGETS ${APP_ID} RUNTIME DESTINATION bin)
install(FILES packaging/linux/${APP_ID}.desktop DESTINATION share/applications)
install(FILES resources/app/${APP_ID}.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES packaging/linux/${APP_ID}-mime.xml DESTINATION share/mime/packages)
install(FILES packaging/linux/${APP_ID}.metainfo.xml DESTINATION share/metainfo RENAME io.github.shelin_boop.Quillbinder.metainfo.xml)
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
# packaging/linux/build-addons.sh puts them into the add-ons (addons/tool-*).
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
if(QB_BUILD_TESTS AND NOT QB_TEST_SWITCHES)
  message(STATUS "The test suites need the test switches: configure with -DQB_TEST_SWITCHES=ON to run them")
endif()
if(QB_BUILD_TESTS)
  if(WIN32)
    # Windows (W3): only the suites packaging/windows/win-tests.sh names are built (cmake --build build --target ...)
    add_subdirectory(tests EXCLUDE_FROM_ALL)
  else()
    add_subdirectory(tests)
  endif()
endif()
if(QB_FUZZ)
  add_subdirectory(tests/fuzz)
endif()

# Developer tools (not installed)
option(QB_BUILD_TOOLS "Build developer tools" ON)
if(QB_BUILD_TOOLS)
  add_executable(qb_template tools/qb-template.cpp)
  target_link_libraries(qb_template PRIVATE qbcore)
endif()
