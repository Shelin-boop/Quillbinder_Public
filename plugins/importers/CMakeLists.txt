# Importers (src/import/ImporterApi.h): Qt plugins delivered as add-ons
# (addons/import-*), built into build/tool-plugins like the writing tools.
function(qb_importer_plugin target)
  add_library(${target} MODULE ${ARGN})
  target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  set_target_properties(${target} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tool-plugins)
endfunction()

# Add-on "Word 97–2003 Import"
set(QB_WORD97_SOURCES word97/Cfb.cpp word97/Cfb.h word97/WordReader.cpp word97/WordReader.h)
qb_importer_plugin(qb-import-word97 word97/Word97Importer.cpp word97/Word97Importer.h ${QB_WORD97_SOURCES}
                   ${CMAKE_SOURCE_DIR}/src/import/ImporterApi.h)
# Developer tool: prints what the reader makes of a .doc file
add_executable(qb_doc_dump word97/DocDump.cpp ${QB_WORD97_SOURCES})
target_include_directories(qb_doc_dump PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qb_doc_dump PRIVATE Qt6::Core Qt6::Gui)

# Add-on "Web Page Import" (needs Qt Network, which the AppImage carries)
set(QB_WEB_SOURCES web/Html.cpp web/Html.h web/Article.cpp web/Article.h)
qb_importer_plugin(qb-import-web web/WebImporter.cpp web/WebImporter.h ${QB_WEB_SOURCES}
                   ${CMAKE_SOURCE_DIR}/src/import/ImporterApi.h)
target_link_libraries(qb-import-web PRIVATE Qt6::Network ${CMAKE_DL_LIBS})
# Developer tool: what the article finder makes of a saved page
add_executable(qb_web_extract web/WebExtract.cpp ${QB_WEB_SOURCES})
target_include_directories(qb_web_extract PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qb_web_extract PRIVATE Qt6::Core Qt6::Gui)
