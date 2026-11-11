# Third-party parts

Quillbinder's own source code and files are under the Apache License 2.0 (`LICENSE`), since 0.80.0:
anyone may use, copy, change and pass them on, with or without fee, keeping the license and the
`NOTICE` file, which names the project and its source. Versions up to 0.70.0 were published under the
Zero-Clause BSD license (0BSD); copies of those versions stay under it.

The program uses, and its AppImage carries, parts of other projects under their own licenses. None
of them is changed by the Apache License 2.0 of Quillbinder. The full license texts of everything the
AppImage carries are in `resources/licenses` (made by `tools/collect-licenses.py` from the Ubuntu
24.04 packages the libraries come from), in the AppImage under `usr/share/doc/quillbinder/licenses`,
and in the program under **Help > Third-Party Licenses** (also a button of **Help > About**).

| Part | Where | License |
|------|-------|---------|
| Qt 6.12.0 (Core, Gui, Widgets, Network, PrintSupport, Pdf, PdfWidgets, Svg, Xml, DBus, Wayland and XCB platform parts, image format plugins; The Qt Company's own build for Linux, since 0.80.0), with the parts Qt builds in (Qt's attribution files) and ICU 73.2 as Qt ships it | linked dynamically; carried by the AppImage | LGPL-3.0 (also available under GPL-2.0 / GPL-3.0); the parts inside Qt and Qt PDF (PDFium) under their own licenses (MIT, BSD, Zlib, FTL, Unicode and others), ICU Unicode-3.0 |
| Qt Multimedia 6.12.0 with Qt Quick, Qt QML, Qt OpenGL and Qt Concurrent (what its FFmpeg backend needs), Qt's TLS plugin | the add-ons "Media Playback", "Speech Transcription" and "Web Page Import" | LGPL-3.0 |
| Hunspell (spell checking) | linked dynamically; carried by the AppImage | tri-license MPL-1.1 / GPL-2.0-or-later / LGPL-2.1-or-later |
| The other libraries the AppImage carries (Hunspell's and the system parts linuxdeploy takes from Ubuntu 24.04: libxkbcommon, Wayland and XCB libraries and others) | carried by the AppImage | their own licenses (MIT, BSD, Zlib, LGPL and others); each with its Ubuntu copyright file in `resources/licenses` |
| AppImage type2-runtime (the start code of the AppImage file), with musl libc, libfuse, squashfuse, zstd and zlib linked into it | the beginning of the AppImage file | MIT; musl MIT, libfuse LGPL-2.1, squashfuse BSD-2-Clause, zstd BSD-3-Clause or GPL-2.0, zlib Zlib (`packaging/licenses`) |
| Breeze icons (a subset; the microphone, record and list icons added in 0.61) | `resources/icons/` | LGPL-3.0-or-later, see `resources/icons/COPYING-BREEZE` |
| Monocypher 4.0.3 (only its Ed25519 check of package signatures, 0.65.0; unchanged) | `src/core/third_party/monocypher/`, compiled into the program | BSD-2-Clause (dual-licensed BSD-2-Clause or CC0-1.0), see `src/core/third_party/monocypher/LICENCE.md` |
| FFmpeg 9.0.2 (decoding only, built without GPL parts; 6.1.1 before 0.80.0) | the add-ons "Media Playback" (free formats only) and "Extra Video and Audio Formats" (every decoder; `addons/media-*`, built by `tools/build-ffmpeg.sh`) | LGPL-2.1-or-later; the add-ons carry their license texts |
| whisper.cpp and ggml (speech recognition), built by `tools/get-whisper.sh` | the add-on "Speech Transcription" | MIT (`speech/licenses/whisper.cpp-MIT.txt` in the add-on) |
| Whisper models (converted for whisper.cpp) | not carried: downloaded by the program when the writer chooses one (Speech Transcription) | MIT |
| Test recording of the speech add-on (J. F. Kennedy, inaugural address, 1961; as whisper.cpp ships it) | the add-on "Speech Transcription" | public domain (a work of the United States government) |
| Name lists of the Name Generator | the add-on "Name Generator", made from the Python package Faker by `tools/make-name-lists.py` | MIT (`names/LICENSE-Faker.txt` in the add-on) |
| linuxdeploy, linuxdeploy-plugin-qt, appimagetool | used to build the AppImage, not part of the program | MIT |

The source code of every one of these parts, exactly in the version the AppImage and the add-ons
carry, is in the release "Third-party source code" of the project page:
https://github.com/Shelin-boop/Quillbinder_Public/releases/tag/third-party-sources (the Qt Project's
source release of Qt 6.12.0, one file per Ubuntu source package, FFmpeg with the patch and build scripts of
the add-ons, the AppImage runtime with the libraries it links in; `tools/collect-sources.py` makes them, the
release workflow publishes them). Help > Third-Party Licenses names the file for each library; Ubuntu's archive has the Ubuntu
packages as well (`apt-get source <package>=<version>`). The libraries inside the AppImage may be
replaced with other builds of the same version (extract the AppImage with `--appimage-extract`).

Scrivener and Scapple are names of products of Literature & Latte. Quillbinder is an independent
program, not made by or connected with Literature & Latte. It reads and writes their file formats so
that projects can move between the programs; it contains no program code of them.
