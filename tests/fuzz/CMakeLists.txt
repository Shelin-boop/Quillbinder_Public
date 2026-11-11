# libFuzzer harnesses (0.80.0, tests/fuzz/README.md): one program per reader, with the whole build instrumented
# (CMakeLists.txt adds -fsanitize=fuzzer-no-link,address when QB_FUZZ is ON).
set(QB_FUZZ_READERS rtf markdown xml zip word97)
set(_n 1)
foreach(r IN LISTS QB_FUZZ_READERS)
  add_executable(qb_fuzz_${r} fuzz_reader.cpp ${CMAKE_SOURCE_DIR}/plugins/importers/word97/Cfb.cpp
                 ${CMAKE_SOURCE_DIR}/plugins/importers/word97/WordReader.cpp)
  target_link_libraries(qb_fuzz_${r} PRIVATE qbui)
  target_include_directories(qb_fuzz_${r} PRIVATE ${CMAKE_SOURCE_DIR}/plugins/importers/word97)
  target_compile_definitions(qb_fuzz_${r} PRIVATE QB_FUZZ_TARGET=${_n})
  target_link_options(qb_fuzz_${r} PRIVATE -fsanitize=fuzzer,address)
  math(EXPR _n "${_n} + 1")
endforeach()
