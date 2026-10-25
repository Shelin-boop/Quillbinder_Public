# Example exporter plugin: writes the compiled manuscript as Markdown.
#
# A plugin needs nothing from the program but the header
# src/compile/ExporterApi.h and Qt (Core + Gui). Built as a MODULE, the
# resulting libmarkdown-exporter.so goes into one of the plugin folders
# (see Help > Open Plugin Folder).
add_library(markdown-exporter MODULE MarkdownExporter.cpp MarkdownExporter.h)
target_include_directories(markdown-exporter PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(markdown-exporter PRIVATE Qt6::Core Qt6::Gui)
set_target_properties(markdown-exporter PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
install(TARGETS markdown-exporter LIBRARY DESTINATION lib/${APP_ID}/plugins)
