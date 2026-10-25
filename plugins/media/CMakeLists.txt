# Add-on "Media Playback": two libraries (see MediaEngine.h).
#  - libqb-media.so       the plugin the program loads (QtCore only)  -> build/tool-plugins/
#  - libqb-media-impl.so  the players, built against Qt Multimedia   -> build/tool-plugins/media/
# packaging/build-addons.sh puts both into the add-on together with the libraries
# of addons/media-playback/stage.sh (Qt Multimedia, FFmpeg built by tools/build-ffmpeg.sh).
add_library(qb-media MODULE MediaEngine.cpp MediaEngine.h ${CMAKE_SOURCE_DIR}/src/media/MediaApi.h)
target_include_directories(qb-media PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(qb-media PRIVATE Qt6::Core ${CMAKE_DL_LIBS})
set_target_properties(qb-media PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tool-plugins)

add_library(qb-media-impl MODULE impl/QtPlayer.cpp ${CMAKE_SOURCE_DIR}/src/media/MediaApi.h)
target_include_directories(qb-media-impl PRIVATE ${CMAKE_SOURCE_DIR}/src)
# FFmpeg's headers (0.50): the player looks at a file's formats before it plays it, with the
# FFmpeg the engine loaded (functions found at run time, nothing linked). The headers of the
# add-on's own build (tools/get-ffmpeg.sh) or of Ubuntu's libavformat-dev (the same 6.1).
find_path(QB_FFMPEG_INCLUDE libavformat/avformat.h
  HINTS $ENV{QB_FFMPEG_DIR}/include ${CMAKE_SOURCE_DIR}/build/ffmpeg/include
  PATHS /usr/include/x86_64-linux-gnu /usr/include)
if(QB_FFMPEG_INCLUDE)
  target_include_directories(qb-media-impl SYSTEM PRIVATE ${QB_FFMPEG_INCLUDE})
  target_compile_definitions(qb-media-impl PRIVATE QB_MEDIA_PROBE=1)
else()
  message(WARNING "FFmpeg headers not found (tools/get-ffmpeg.sh or libavformat-dev): the media player cannot "
                  "name formats it cannot decode")
  target_compile_definitions(qb-media-impl PRIVATE QB_MEDIA_PROBE=0)
endif()
target_link_libraries(qb-media-impl PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia ${CMAKE_DL_LIBS})
set_target_properties(qb-media-impl PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tool-plugins/media
  CXX_VISIBILITY_PRESET hidden
  BUILD_RPATH "$ORIGIN/lib" INSTALL_RPATH "$ORIGIN/lib")
