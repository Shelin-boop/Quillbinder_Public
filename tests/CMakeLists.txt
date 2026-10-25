add_executable(qb_tests core_tests.cpp script_checks.cpp scrivener_checks.cpp compile_checks.cpp)
target_link_libraries(qb_tests PRIVATE qbcore Qt6::Widgets)

# Scripted UI run with screenshots (QT_QPA_PLATFORM=offscreen ./qb_ui_tests <dir>)
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(qb_ui_tests ui_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_ui_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_ui_tests PRIVATE QB_TEST_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins")

# Page view and copyholders (QT_QPA_PLATFORM=offscreen ./qb_m6_tests <dir>)
add_executable(qb_m6_tests m6_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_m6_tests PRIVATE qbui Qt6::Test)

# Drag and drop, navigation, Quick Reference panels (QT_QPA_PLATFORM=offscreen ./qb_m7_tests <dir>;
# under X11 also real mouse drags: xvfb-run -a -s "-screen 0 1920x1080x24" ./qb_m7_tests <dir>)
add_executable(qb_m7_tests m7_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_m7_tests PRIVATE qbui Qt6::Test)

# Search and replace, filters, find by formatting (QT_QPA_PLATFORM=offscreen ./qb_m8_tests <dir>)
add_executable(qb_m8_tests m8_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_m8_tests PRIVATE qbui Qt6::Test)

# Scriptwriting: script mode, elements, formats, compile (QT_QPA_PLATFORM=offscreen ./qb_m9_tests <dir>)
add_executable(qb_m9_tests m9_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_m9_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_m9_tests PRIVATE QB_TEST_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins")

# Writing tools: name generator, scratch pad, text statistics (QT_QPA_PLATFORM=offscreen ./qb_m10_tests <dir>).
# Installs the add-on packages from <build>/addons-dist (packaging/build-addons.sh <build> <build>/addons-dist)
# and also compiles the counting parts of two tools for direct checks.
find_package(Qt6 REQUIRED COMPONENTS Network)
add_executable(qb_m10_tests m10_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc
  ${CMAKE_SOURCE_DIR}/plugins/tools/text-statistics/TextAnalysis.cpp
  ${CMAKE_SOURCE_DIR}/plugins/tools/name-generator/NameLists.cpp)
target_include_directories(qb_m10_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/tools)
target_link_libraries(qb_m10_tests PRIVATE qbui Qt6::Test Qt6::Network)
target_compile_definitions(qb_m10_tests PRIVATE QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}")

# Audio and video (QT_QPA_PLATFORM=offscreen ./qb_m11_tests /tmp/shots). Installs <build>/addons-dist/media-playback.qbpkg
# (packaging/build-addons.sh <build> <build>/addons-dist, after tools/build-ffmpeg.sh <ffmpeg source> <build>/ffmpeg).
# Needs a sound server for the audio checks (a null sink is enough: pulseaudio -D --exit-idle-time=-1, PULSE_SERVER=...).
add_executable(qb_m11_tests m11_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_m11_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_m11_tests PRIVATE QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}" QB_TEST_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")

# Import and export (QT_QPA_PLATFORM=offscreen ./qb_m12_tests /tmp/shots): installs <build>/addons-dist/import-word97.qbpkg
# and import-web.qbpkg (packaging/build-addons.sh <build> <build>/addons-dist); serves the test pages itself (HTTP and HTTPS).
add_executable(qb_m12_tests m12_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_m12_tests PRIVATE qbui Qt6::Test Qt6::Network)
target_compile_definitions(qb_m12_tests PRIVATE QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}" QB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/import")

# Scrivener project format, compile part 2 (format editor, PDF, e-book, metadata, preview) and a menu audit
# (QT_QPA_PLATFORM=offscreen ./qb_m13_tests /tmp/shots); installs the add-ons in <build>/addons-dist when they were built
add_executable(qb_m13_tests m13_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_m13_tests PRIVATE qbui Qt6::Test Qt6::PdfWidgets)
target_compile_definitions(qb_m13_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}")

# Corrections of 0.14.1: fonts at once and project-wide, label color in synopsis and notes, project bookmarks,
# document icons, search results marked in the editor (QT_QPA_PLATFORM=offscreen ./qb_fixes_tests /tmp/shots)
add_executable(qb_fixes_tests fixes_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_fixes_tests PRIVATE qbui Qt6::Test)

# Printing (milestone M14): File > Print Settings, Print Preview, Print Current Document for texts, index cards,
# the outliner, pictures, PDF files and web pages; the printouts go into PDF files checked with poppler-utils
# (QT_QPA_PLATFORM=offscreen ./qb_m14_tests /tmp/shots)
add_executable(qb_m14_tests m14_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_m14_tests PRIVATE qbui Qt6::Test Qt6::PrintSupport)

# Version 0.17: binder, fonts, color buttons, icons, icon bar, folder comments, mind maps, developer guide
# (QT_QPA_PLATFORM=offscreen ./qb_v017_tests /tmp/shots)
add_executable(qb_v017_tests v017_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_v017_tests PRIVATE qbui Qt6::Test)


# Version 0.18: the mind map help and File > Compare and Transfer
# (QT_QPA_PLATFORM=offscreen ./qb_v018_tests /tmp/shots)
add_executable(qb_v018_tests v018_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_v018_tests PRIVATE qbui Qt6::Test)

# Version 0.19: the Page Down / Page Up crash, crash reports with a snapshot of program and system,
# sessions that ended unexpectedly, saving on SIGTERM, Help > System Information and Crash Reports.
# Starts <build>/quillbinder and ends it with signals (QT_QPA_PLATFORM=offscreen ./qb_v019_tests /tmp/shots)
add_executable(qb_v019_tests v019_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_v019_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v019_tests PRIVATE QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}")
add_dependencies(qb_v019_tests ${APP_ID})

# 0.20: the Objects sheet and the data-safety fixes (QT_QPA_PLATFORM=offscreen ./qb_v020_tests /tmp/shots)
add_executable(qb_v020_tests v020_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_v020_tests PRIVATE qbui Qt6::Test)

# 0.21: Options in Scrivener's layout, zoom, mind map menus, Compile Overview (QT_QPA_PLATFORM=offscreen ./qb_v021_tests /tmp/shots)
add_executable(qb_v021_tests v021_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_v021_tests PRIVATE qbui Qt6::Test)

# 0.21.1: format bar target, editable bookmark preview, a zoom per part, mind map backgrounds, places kept,
# formatted object texts, credits (QT_QPA_PLATFORM=offscreen ./qb_v0211_tests /tmp/shots)
add_executable(qb_v0211_tests v0211_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_v0211_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v0211_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# 0.21.2: Scrivener headings and project bookmarks, the new standard settings (QT_QPA_PLATFORM=offscreen ./qb_v0212_tests /tmp/shots)
add_executable(qb_v0212_tests v0212_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_v0212_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v0212_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Version 0.40: the welcome project (first start, Help > Welcome Project, its mind map, objects and links), the Quick
# Start Guide, Help > Third-Party Licenses and About (0BSD, credits), "Combined Text" instead of the old name, no
# tutorial left, the menu places the guide and the welcome project name, List of All Placeholders, and two
# corrections carried over from the suite of the old tutorial (QT_QPA_PLATFORM=offscreen ./qb_v040_tests /tmp/shots).
add_executable(qb_v040_tests v040_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_v040_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v040_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}")

# 0.50: the release on the project page, the source files named in the license list, the media add-ons split into free
# formats and "Extra Video and Audio Formats" (QT_QPA_PLATFORM=offscreen ./qb_v050_tests /tmp/shots). Installs
# <build>/addons-dist/media-playback.qbpkg and media-extra-formats.qbpkg (tools/get-ffmpeg.sh, then
# packaging/build-addons.sh <build> <build>/addons-dist); needs a sound server like qb_m11_tests.
add_executable(qb_v050_tests v050_tests.cpp ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(qb_v050_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v050_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}")
