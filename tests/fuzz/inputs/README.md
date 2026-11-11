# Inputs found by fuzzing

Damaged files that once made a reader or writer fail, kept as seed inputs for `qb_fuzz_tests` and as a starting
corpus for the libFuzzer harnesses (`../README.md`). They are the program's own test documents, damaged by a fuzzer;
they hold no text or pictures of anyone else.

- `rtf-figure-in-table-row.rtf` (2,710 bytes): found by the external audit of 0.81.0 (2026-11-11, finding F-04) after
  8,933 inputs, from a damaged copy of an RTF file this program writes. The reader built a table whose cells
  overlapped a figure, and the RTF writer did not return on it. Fixed in 0.81.1: the reader no longer lets a figure
  and a table row take the same paragraphs, and the writer's walk over a table cell stops where the cell ends.
