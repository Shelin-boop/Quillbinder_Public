# Add-on "Speech Transcription" (0.61): two libraries (see SpeechApi.h).
#  - libqb-speech.so       the writing tool the program loads (Qt Widgets, Network) -> build/tool-plugins/
#  - libqb-speech-impl.so  the engine: Qt Multimedia and whisper.cpp               -> build/tool-plugins/speech/
# whisper.cpp is built by tools/get-whisper.sh (build/whisper, or QB_WHISPER_DIR); without it only
# the tool is built (it then says that its engine is missing). packaging/build-addons.sh puts both
# into the add-on with the libraries of addons/speech-transcription/stage.sh.
qb_tool_plugin(qb-speech SpeechTool.cpp SpeechTool.h SpeechLogic.cpp SpeechLogic.h SpeechApi.h)
target_link_libraries(qb-speech PRIVATE Qt6::Network ${CMAKE_DL_LIBS})

find_package(Qt6 QUIET COMPONENTS Multimedia)
if(DEFINED ENV{QB_WHISPER_DIR})
  set(QB_WHISPER_DIR "$ENV{QB_WHISPER_DIR}")
else()
  set(QB_WHISPER_DIR "${CMAKE_SOURCE_DIR}/build/whisper")
endif()
if(Qt6Multimedia_FOUND AND EXISTS "${QB_WHISPER_DIR}/include/whisper.h")
  add_library(qb-speech-impl MODULE impl/SpeechEngine.cpp SpeechLogic.cpp SpeechLogic.h SpeechApi.h)
  target_include_directories(qb-speech-impl SYSTEM PRIVATE "${QB_WHISPER_DIR}/include")
  target_include_directories(qb-speech-impl PRIVATE ${CMAKE_SOURCE_DIR}/src) # core/AudioBackend.h (header-only)
  target_link_libraries(qb-speech-impl PRIVATE Qt6::Core Qt6::Multimedia ${CMAKE_DL_LIBS}
    "${QB_WHISPER_DIR}/lib/libwhisper.so" "${QB_WHISPER_DIR}/lib/libggml.so" "${QB_WHISPER_DIR}/lib/libggml-base.so")
  set_target_properties(qb-speech-impl PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tool-plugins/speech
    CXX_VISIBILITY_PRESET hidden
    BUILD_RPATH "$ORIGIN/lib" INSTALL_RPATH "$ORIGIN/lib")
  # runs from the build folder (QB_SPEECH_DIR=build/tool-plugins/speech): the libraries and the
  # test recording beside the engine, as in the add-on
  add_custom_command(TARGET qb-speech-impl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/tool-plugins/speech/samples
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${QB_WHISPER_DIR}/lib" ${CMAKE_BINARY_DIR}/tool-plugins/speech/lib
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${QB_WHISPER_DIR}/samples/jfk.wav" ${CMAKE_BINARY_DIR}/tool-plugins/speech/samples/
    VERBATIM)
  message(STATUS "Speech Transcription: engine with whisper.cpp of ${QB_WHISPER_DIR}")
else()
  message(STATUS "Speech Transcription: no engine (tools/get-whisper.sh, and Qt Multimedia for building)")
endif()
