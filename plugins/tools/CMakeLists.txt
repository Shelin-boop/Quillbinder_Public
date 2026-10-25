# Writing tools: one MODULE library each, built into build/tool-plugins.
# A tool needs only src/tools/ToolApi.h and Qt.
function(qb_tool_plugin target)
  add_library(${target} MODULE ${ARGN})
  target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  set_target_properties(${target} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tool-plugins)
endfunction()

add_subdirectory(name-generator)
add_subdirectory(scratch-pad)
add_subdirectory(text-statistics)
