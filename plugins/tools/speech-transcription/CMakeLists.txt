# Add-on "Speech Transcription" (0.61): two libraries (see SpeechApi.h).
#  - libqb-speech.so       the writing tool the program loads (Qt Widgets, Network) -> build/tool-plugins/
#  - libqb-speech-impl.so  the engine: Qt Multimedia and whisper.cpp               -> build/tool-plugins/speech/
# whisper.cpp is built by tools/get-whisper.sh (build/whisper, or QB_WHISPER_DIR; on Windows
# packaging/windows/win-whisper.sh, W4c); without it only
# the tool is built (it then says that its engine is missing). packaging/linux/build-addons.sh puts both
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
  set_target_properties(qb-speech-impl PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tool-plugins/speech
    CXX_VISIBILITY_PRESET hidden)
  if(WIN32)
    # W4c (2026-11-12): whisper.cpp built with MSVC by packaging/windows/win-whisper.sh: import libraries in lib/,
    # the DLLs (whisper, ggml, ggml-base and the compute modules ggml-cpu-*) in bin/; the tool loads them from the
    # add-on's speech\lib (SpeechTool.cpp)
    target_compile_definitions(qb-speech-impl PRIVATE WHISPER_SHARED GGML_SHARED GGML_BACKEND_SHARED)
    target_link_libraries(qb-speech-impl PRIVATE Qt6::Core Qt6::Multimedia
      "${QB_WHISPER_DIR}/lib/whisper.lib" "${QB_WHISPER_DIR}/lib/ggml.lib" "${QB_WHISPER_DIR}/lib/ggml-base.lib")
    # runs from the build folder (QB_SPEECH_DIR=build/tool-plugins/speech): the libraries and the test recording
    # beside the engine, as in the add-on (copies: links need rights on Windows)
    add_custom_command(TARGET qb-speech-impl POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/tool-plugins/speech/samples
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${QB_WHISPER_DIR}/bin" ${CMAKE_BINARY_DIR}/tool-plugins/speech/lib
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${QB_WHISPER_DIR}/samples/jfk.wav" ${CMAKE_BINARY_DIR}/tool-plugins/speech/samples/
      VERBATIM)
  else()
    target_link_libraries(qb-speech-impl PRIVATE Qt6::Core Qt6::Multimedia ${CMAKE_DL_LIBS}
      "${QB_WHISPER_DIR}/lib/libwhisper.so" "${QB_WHISPER_DIR}/lib/libggml.so" "${QB_WHISPER_DIR}/lib/libggml-base.so")
    set_target_properties(qb-speech-impl PROPERTIES BUILD_RPATH "$ORIGIN/lib" INSTALL_RPATH "$ORIGIN/lib")
    # runs from the build folder (QB_SPEECH_DIR=build/tool-plugins/speech): the libraries and the
    # test recording beside the engine, as in the add-on
    add_custom_command(TARGET qb-speech-impl POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/tool-plugins/speech/samples
      COMMAND ${CMAKE_COMMAND} -E create_symlink "${QB_WHISPER_DIR}/lib" ${CMAKE_BINARY_DIR}/tool-plugins/speech/lib
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${QB_WHISPER_DIR}/samples/jfk.wav" ${CMAKE_BINARY_DIR}/tool-plugins/speech/samples/
      VERBATIM)
  endif()
  message(STATUS "Speech Transcription: engine with whisper.cpp of ${QB_WHISPER_DIR}")
else()
  message(STATUS "Speech Transcription: no engine (tools/get-whisper.sh, and Qt Multimedia for building)")
endif()
