# Fuzzing the readers

Two ways (0.80.0; the external audit of 2026-11-09, ST-5):

- **`qb_fuzz_tests`** (`tests/fuzz_tests.cpp`), part of the full test suite: small valid inputs of RTF, Markdown,
  XML, ZIP, DOCX, ODT, Word 97 and .scrivx are damaged in fixed-seed ways and read in the test's process; every
  input must come back without a crash, an exception or a hang. `QB_FUZZ_SEED=<n>` and `QB_FUZZ_ROUNDS=<n>` change
  the seed and the number of inputs. After a crash, `fuzz-current.bin` and `fuzz-current.txt` in the shots folder
  hold the input and the reader.
- **libFuzzer**, for long runs: a build of its own with clang, every part instrumented, one program per reader
  (`fuzz_reader.cpp`):

```
CC=clang CXX=clang++ cmake -S . -B build-fuzz -G Ninja -DQB_FUZZ=ON -DQB_BUILD_TESTS=OFF
cmake --build build-fuzz --target qb_fuzz_rtf qb_fuzz_markdown qb_fuzz_xml qb_fuzz_zip qb_fuzz_word97
mkdir -p corpus-rtf && build-fuzz/tests/fuzz/qb_fuzz_rtf corpus-rtf -max_total_time=3600
```

  A file libFuzzer reports (`crash-...`, `timeout-...`) goes into `inputs/` and into `qb_fuzz_tests` as a new seed input
  once the reader is fixed; `inputs/` is also a good starting corpus (`qb_fuzz_rtf corpus-rtf inputs`). The RTF harness
  writes what it read as well (0.81.1).
