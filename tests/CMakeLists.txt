add_executable(qb_tests core_tests.cpp script_checks.cpp scrivener_checks.cpp compile_checks.cpp)
target_link_libraries(qb_tests PRIVATE qbcore Qt6::Widgets)

# Scripted UI run with screenshots (QT_QPA_PLATFORM=offscreen ./qb_ui_tests <dir>)
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(qb_ui_tests ui_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_ui_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_ui_tests PRIVATE QB_TEST_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins")

# Page view and copyholders (QT_QPA_PLATFORM=offscreen ./qb_m6_tests <dir>)
add_executable(qb_m6_tests m6_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_m6_tests PRIVATE qbui Qt6::Test)

# Drag and drop, navigation, Quick Reference panels (QT_QPA_PLATFORM=offscreen ./qb_m7_tests <dir>;
# under X11 also real mouse drags: xvfb-run -a -s "-screen 0 1920x1080x24" ./qb_m7_tests <dir>)
add_executable(qb_m7_tests m7_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_m7_tests PRIVATE qbui Qt6::Test)

# Search and replace, filters, find by formatting (QT_QPA_PLATFORM=offscreen ./qb_m8_tests <dir>)
add_executable(qb_m8_tests m8_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_m8_tests PRIVATE qbui Qt6::Test)

# Scriptwriting: script mode, elements, formats, compile (QT_QPA_PLATFORM=offscreen ./qb_m9_tests <dir>)
add_executable(qb_m9_tests m9_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_m9_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_m9_tests PRIVATE QB_TEST_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins")

# Writing tools: name generator, scratch pad, text statistics (QT_QPA_PLATFORM=offscreen ./qb_m10_tests <dir>).
# Installs the add-on packages from <build>/addons-dist (packaging/build-addons.sh <build> <build>/addons-dist)
# and also compiles the counting parts of two tools for direct checks.
find_package(Qt6 REQUIRED COMPONENTS Network)
add_executable(qb_m10_tests m10_tests.cpp ${QB_RESOURCES_QRC}
  ${CMAKE_SOURCE_DIR}/plugins/tools/text-statistics/TextAnalysis.cpp
  ${CMAKE_SOURCE_DIR}/plugins/tools/name-generator/NameLists.cpp)
target_include_directories(qb_m10_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/tools)
target_link_libraries(qb_m10_tests PRIVATE qbui Qt6::Test Qt6::Network)
target_compile_definitions(qb_m10_tests PRIVATE QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}")

# Audio and video (QT_QPA_PLATFORM=offscreen ./qb_m11_tests /tmp/shots). Installs <build>/addons-dist/media-playback.qbpkg
# (packaging/build-addons.sh <build> <build>/addons-dist, after tools/build-ffmpeg.sh <ffmpeg source> <build>/ffmpeg).
# Needs a sound server for the audio checks (a null sink is enough: pulseaudio -D --exit-idle-time=-1, PULSE_SERVER=...).
add_executable(qb_m11_tests m11_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_m11_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_m11_tests PRIVATE QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}" QB_TEST_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")

# Import and export (QT_QPA_PLATFORM=offscreen ./qb_m12_tests /tmp/shots): installs <build>/addons-dist/import-word97.qbpkg
# and import-web.qbpkg (packaging/build-addons.sh <build> <build>/addons-dist); serves the test pages itself (HTTP and HTTPS).
add_executable(qb_m12_tests m12_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_m12_tests PRIVATE qbui Qt6::Test Qt6::Network)
target_compile_definitions(qb_m12_tests PRIVATE QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}" QB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/import")

# Scrivener project format, compile part 2 (format editor, PDF, e-book, metadata, preview) and a menu audit
# (QT_QPA_PLATFORM=offscreen ./qb_m13_tests /tmp/shots); installs the add-ons in <build>/addons-dist when they were built
add_executable(qb_m13_tests m13_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_m13_tests PRIVATE qbui Qt6::Test Qt6::PdfWidgets)
target_compile_definitions(qb_m13_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}")

# Corrections of 0.14.1: fonts at once and project-wide, label color in synopsis and notes, project bookmarks,
# document icons, search results marked in the editor (QT_QPA_PLATFORM=offscreen ./qb_fixes_tests /tmp/shots)
add_executable(qb_fixes_tests fixes_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_fixes_tests PRIVATE qbui Qt6::Test)

# Printing (milestone M14): File > Print Settings, Print Preview, Print Current Document for texts, index cards,
# the outliner, pictures, PDF files and web pages; the printouts go into PDF files checked with poppler-utils
# (QT_QPA_PLATFORM=offscreen ./qb_m14_tests /tmp/shots)
add_executable(qb_m14_tests m14_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_m14_tests PRIVATE qbui Qt6::Test Qt6::PrintSupport)

# Version 0.17: binder, fonts, color buttons, icons, icon bar, folder comments, mind maps, developer guide
# (QT_QPA_PLATFORM=offscreen ./qb_v017_tests /tmp/shots)
add_executable(qb_v017_tests v017_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v017_tests PRIVATE qbui Qt6::Test)


# Version 0.18: the mind map help and File > Compare and Transfer
# (QT_QPA_PLATFORM=offscreen ./qb_v018_tests /tmp/shots)
add_executable(qb_v018_tests v018_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v018_tests PRIVATE qbui Qt6::Test)

# Version 0.19: the Page Down / Page Up crash, crash reports with a snapshot of program and system,
# sessions that ended unexpectedly, saving on SIGTERM, Help > System Information and Crash Reports.
# Starts <build>/quillbinder and ends it with signals (QT_QPA_PLATFORM=offscreen ./qb_v019_tests /tmp/shots)
add_executable(qb_v019_tests v019_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v019_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v019_tests PRIVATE QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}")
add_dependencies(qb_v019_tests ${APP_ID})

# 0.20: the Objects sheet and the data-safety fixes (QT_QPA_PLATFORM=offscreen ./qb_v020_tests /tmp/shots)
add_executable(qb_v020_tests v020_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v020_tests PRIVATE qbui Qt6::Test)

# 0.21: Options in Scrivener's layout, zoom, mind map menus, Compile Overview (QT_QPA_PLATFORM=offscreen ./qb_v021_tests /tmp/shots)
add_executable(qb_v021_tests v021_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v021_tests PRIVATE qbui Qt6::Test)

# 0.21.1: format bar target, editable bookmark preview, a zoom per part, mind map backgrounds, places kept,
# formatted object texts, credits (QT_QPA_PLATFORM=offscreen ./qb_v0211_tests /tmp/shots)
add_executable(qb_v0211_tests v0211_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v0211_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v0211_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# 0.21.2: Scrivener headings and project bookmarks, the new standard settings (QT_QPA_PLATFORM=offscreen ./qb_v0212_tests /tmp/shots)
add_executable(qb_v0212_tests v0212_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v0212_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v0212_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Version 0.40: the welcome project (first start, Help > Welcome Project, its mind map, objects and links), the Quick
# Start Guide, Help > Third-Party Licenses and About (0BSD, credits), "Combined Text" instead of the old name, no
# tutorial left, the menu places the guide and the welcome project name, List of All Placeholders, and two
# corrections carried over from the suite of the old tutorial (QT_QPA_PLATFORM=offscreen ./qb_v040_tests /tmp/shots).
add_executable(qb_v040_tests v040_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v040_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v040_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}")

# Version 0.40.1: fonts with several designs in one family (optical sizes such as Sitka's; tested with the free
# Source Serif 4 of tests/fonts) get a family per design; the Bold / Italic / Underline / Strikethrough buttons show
# the whole selection, also one made from right to left (QT_QPA_PLATFORM=offscreen ./qb_v0401_tests /tmp/shots).
add_executable(qb_v0401_tests v0401_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v0401_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v0401_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Version 0.40.2 (private line): no jump on a right-click, a look per project, the border beside the text, settings
# in the right-click menus, project settings saved to a file and read back, style changes reaching all text of the
# style (QT_QPA_PLATFORM=offscreen ./qb_v0402_tests /tmp/shots).
add_executable(qb_v0402_tests v0402_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v0402_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v0402_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Version 0.40.3 (private line): titles from the first line, No Style redefined, the short spelling menu, a format
# bar that shows what is seen, dialogs above the Styles panel, the alignment of the main text, the chosen style list
# (xvfb-run -a ./qb_v0403_tests /tmp/shots).
add_executable(qb_v0403_tests v0403_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v0403_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v0403_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
# Version 0.60.1: Paste with Formatting, Show Invisibles (menu, toolbar button, drawing), the total of a folder in
# the footer and the inspector, paragraph styles over several paragraphs (xvfb-run -a ./qb_v0601_tests /tmp/shots).
add_executable(qb_v0601_tests v0601_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v0601_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v0601_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Version 0.60.2: the three ways to paste in the right-click menu of the text (xvfb-run -a ./qb_v0602_tests /tmp/shots).
add_executable(qb_v0602_tests v0602_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v0602_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v0602_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Version 0.60.3: folder totals in the binder and the footers, Inspector Background, the color of the toolbars
# (xvfb-run -a ./qb_v0603_tests /tmp/shots).
add_executable(qb_v0603_tests v0603_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v0603_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v0603_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Version 0.60.4: the paragraph buttons of the format bar, the README's Wayland line (xvfb-run -a ./qb_v0604_tests /tmp/shots).
add_executable(qb_v0604_tests v0604_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v0604_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v0604_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# 0.50: the release on the project page, the source files named in the license list, the media add-ons split into free
# formats and "Extra Video and Audio Formats" (QT_QPA_PLATFORM=offscreen ./qb_v050_tests /tmp/shots). Installs
# <build>/addons-dist/media-playback.qbpkg and media-extra-formats.qbpkg (tools/get-ffmpeg.sh, then
# packaging/build-addons.sh <build> <build>/addons-dist); needs a sound server like qb_m11_tests.
add_executable(qb_v050_tests v050_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v050_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v050_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}")

# 0.60: one source for the private and the public repository: About names FFmpeg, the character and place sheets,
# the media package with all formats (media-all-formats.qbpkg), the release text and README, tools/legal-check.py
# and tools/publish-public.sh on test copies (QT_QPA_PLATFORM=offscreen ./qb_v060_tests /tmp/shots).
add_executable(qb_v060_tests v060_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v060_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v060_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}")

# Version 0.60.5: speed with a large project in Scrivener format, the right-click menu (xvfb-run -a ./qb_v0605_tests /tmp/shots).
add_executable(qb_v0605_tests v0605_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v0605_tests PRIVATE qbui Qt6::Test)
target_compile_definitions(qb_v0605_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Version 0.61: tool interface 1.1, Undo and Redo on the toolbar, the add-on "Speech Transcription"
# (xvfb-run -a ./qb_v061_tests /tmp/shots). Installs <build>/addons-dist/speech-transcription.qbpkg
# (packaging/build-addons.sh, after tools/get-whisper.sh); a tool built for interface 1.0 (tool10/) is
# loaded from <build>/tests/tool10-plugins.
add_library(qb-test-old-tool MODULE tool10/OldTool.cpp tool10/ToolApi10.h)
target_link_libraries(qb-test-old-tool PRIVATE Qt6::Core)
set_target_properties(qb-test-old-tool PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/tool10-plugins)
add_executable(qb_v061_tests v061_tests.cpp ${QB_RESOURCES_QRC}
  ${CMAKE_SOURCE_DIR}/plugins/tools/speech-transcription/SpeechLogic.cpp)
target_include_directories(qb_v061_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/tools)
target_link_libraries(qb_v061_tests PRIVATE qbui Qt6::Test Qt6::Network)
target_compile_definitions(qb_v061_tests PRIVATE QB_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}" QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(qb_v061_tests qb-test-old-tool)

# Version 0.61.1: quote marks converted from File > Options > Corrections, Project > Delete Old Snapshots
# (also in Scrivener format) (xvfb-run -a ./qb_v0611_tests /tmp/shots).
add_executable(qb_v0611_tests v0611_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v0611_tests PRIVATE qbui Qt6::Test)

# Version 0.62: settings per project, the first start, tables and pictures (xvfb-run -a ./qb_v062_tests /tmp/shots).
add_executable(qb_v062_tests v062_tests.cpp v062_settings.cpp v062_tables.cpp v062_common.h ${QB_RESOURCES_QRC})
target_link_libraries(qb_v062_tests PRIVATE qbui Qt6::Test)
# Version 0.64: the spelling menu on words with apostrophes, the autosave while typing on, the card
# size, a project in a synced folder (lock, conflicts, nothing written without a change, texts that
# have not arrived), also in Scrivener format (xvfb-run -a ./qb_v064_tests /tmp/shots).
add_executable(qb_v064_tests v064_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_v064_tests PRIVATE qbui Qt6::Test)
# Windows port, step W2: what it changed in the code Linux runs too (packages' platform, the process
# check, pruning reports with memory dumps). No window (xvfb-run -a ./qb_w2_tests /tmp/shots).
add_executable(qb_w2_tests w2_tests.cpp)
target_link_libraries(qb_w2_tests PRIVATE qbcore)
target_compile_definitions(qb_w2_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
# Windows port, step W3 (Linux and the Windows runner): bundles with a program update of the running
# version, the full version's add-ons, the license list in the program, the spelling dictionaries
# (QB_TEST_DICTIONARIES=<folder>). No window.
add_executable(qb_w3_tests w3_tests.cpp ${QB_RESOURCES_QRC})
target_link_libraries(qb_w3_tests PRIVATE qbui)
target_compile_definitions(qb_w3_tests PRIVATE QB_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
