# Quillbinder

A writing studio for long texts (binder, corkboard, outliner, combined text,
snapshots, targets, composition mode, mind maps, object sheets, compile to DOCX,
PDF and EPUB, projects in its own format or in Scrivener 3 format), written in
C++17 with Qt 6 for Ubuntu 24.04 LTS and KDE Plasma. "Quillbinder" is a working
name; it is set once in `CMakeLists.txt` (`APP_NAME`, `APP_ID`).

Quillbinder is an independent program. It is not made by or connected with Literature & Latte,
the maker of Scrivener and Scapple; it reads and writes their file formats so that projects can move
between the programs. Scrivener and Scapple are names of products of Literature & Latte.

The user manual is `docs/manual.md` (shown in the program with F1); `docs/quick-start.md` is the
Quick Start Guide (Help > Quick Start Guide); the welcome project that opens at the first start is
written in `resources/welcome`. `docs/CHANGES.md` lists what each version brought; the detailed development log (`docs/status.md`)
is kept in the development repository only. Working with Claude Code: `CLAUDE.md`.

## Build (Ubuntu 24.04)

```
sudo apt install build-essential cmake qt6-base-dev qt6-base-private-dev qt6-svg-dev qt6-pdf-dev \
     qt6-wayland qt6-xdgdesktopportal-platformtheme libhunspell-dev zlib1g-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/quillbinder
```

## Tests

```
tools/cloud-setup.sh                         # (as root) everything to build and test on a fresh Ubuntu 24.04
tools/run-all-tests.sh build/test-results offscreen   # every suite, one line each; "x11" instead of offscreen for a virtual display
tools/get-ffmpeg.sh                          # once: FFmpeg for the Media Playback add-on (the runner builds the add-ons then)
tools/e2e-appimage.sh <AppImage> dist/quillbinder-<version>-update.qbpkg   # the update in the real AppImage
QT_QPA_PLATFORM=offscreen ./build/tests/qb_tests               # formats, compile, import
QT_QPA_PLATFORM=offscreen ./build/tests/qb_ui_tests /tmp/shots  # scripted UI run + screenshots
QT_QPA_PLATFORM=offscreen ./build/tests/qb_m6_tests /tmp/shots  # page view and copyholders
QT_QPA_PLATFORM=offscreen ./build/tests/qb_m7_tests /tmp/shots  # drag and drop, navigation, Quick Reference panels
QT_QPA_PLATFORM=offscreen ./build/tests/qb_m8_tests /tmp/shots  # search and replace, filters, find by formatting
QT_QPA_PLATFORM=offscreen ./build/tests/qb_m9_tests /tmp/shots  # scriptwriting (the add-on part needs packaging/build-addons.sh build build/addons-dist)
QT_QPA_PLATFORM=offscreen ./build/tests/qb_m10_tests /tmp/shots # writing tools (installs build/addons-dist/tool-*.qbpkg; build them first as above)
QT_QPA_PLATFORM=offscreen ./build/tests/qb_m11_tests /tmp/shots # audio and video (installs build/addons-dist/media-playback.qbpkg; needs a sound server, see below)
QT_QPA_PLATFORM=offscreen ./build/tests/qb_m12_tests /tmp/shots # .doc and web page import, export options (installs build/addons-dist/import-*.qbpkg; serves its test pages itself)
QT_QPA_PLATFORM=offscreen ./build/tests/qb_m13_tests /tmp/shots # Scrivener project format, compile window and format editor, PDF / EPUB, menu audit (installs build/addons-dist/*.qbpkg when built)
QT_QPA_PLATFORM=offscreen ./build/tests/qb_fixes_tests /tmp/shots # corrections of 0.14.1: fonts project-wide, label tint, bookmarks, icons, search marks
QT_QPA_PLATFORM=offscreen ./build/tests/qb_v020_tests /tmp/shots # 0.20: the Objects sheet and the data-safety fixes (QB_SKIP_BIG_BACKUP=1: without the 4.3 GB backup)
QT_QPA_PLATFORM=offscreen ./build/tests/qb_v040_tests /tmp/shots # 0.40: welcome project, Quick Start Guide, third-party licenses, About
tools/e2e-old-version.sh <older AppImage>    # 0.20: a project with objects opened and saved by an older version keeps them
QB_SCRIV_SAMPLE=/path/to/sample.scriv QT_QPA_PLATFORM=offscreen ./build/tests/qb_tests   # also round-trips a real Scrivener 3 project
# under X11 the M6 and M7 runs also drag with the mouse: xvfb-run -a -s "-screen 0 1920x1080x24" ./build/tests/qb_m7_tests /tmp/shots
```

## AppImage

```
packaging/build-appimage.sh      # downloads linuxdeploy/appimagetool on first run
```

The result is `build-appimage/Quillbinder-<version>-x86_64.AppImage`. It uses
the static AppImage runtime, so it does not need libfuse2. The script checks with
`tools/collect-licenses.py --check` that every library it carries has its license text, and puts the
texts into the AppImage (`usr/share/doc/quillbinder/licenses`).

## Updates and add-ons

Updates and extensions are delivered as `.qbpkg` packages (see `src/core/Addons.h` and section 17 of the manual).

```
packaging/build-update.sh "What is new ..."   # program update -> dist/quillbinder-<version>-update.qbpkg
packaging/build-addons.sh build dist          # add-ons in addons/ -> dist/<id>.qbpkg
tools/make-package.py bundle --id ... --name ... --version ... a.qbpkg b.qbpkg out.qbpkg
```

`src/text/PageLayout.cpp` calls a private Qt class (page margins without an entry in the undo history). The program must
therefore run on the Qt it was built with, which is the case for the AppImage and its program updates (same `APP_RUNTIME`);
`--selftest` tries the call, so an update that does not fit is refused when it is installed.

A program update is the program binary only; it runs on the libraries inside the AppImage. `APP_RUNTIME` in
`CMakeLists.txt` names that library set: change it (and ship a new AppImage) whenever the bundled libraries change.
Build updates on the same system as the AppImage (Ubuntu 24.04).

## Writing tools (add-ons with a plugin)

The name generator, the scratch pad and text statistics are Qt plugins that implement `src/tools/ToolApi.h`
(a header of pure virtual classes: the program gives a tool a `ToolHost`; nothing is linked against the
program). They are built into `build/tool-plugins/` and delivered as add-ons (`addons/tool-*`, whose
`plugins.txt` names the library that `packaging/build-addons.sh` copies in). To try them from the build folder:

```
QB_PLUGIN_PATH=build/tool-plugins QB_TOOL_DATA=addons/tool-name-generator/payload ./build/quillbinder
./build/quillbinder --scratch-pad      # the scratch pad without a main window (--tool <command key>)
```

The name lists are made from the Python package Faker (MIT licence) by `tools/make-name-lists.py`.
Running instances find each other through local sockets in `$XDG_RUNTIME_DIR/quillbinder/` (`src/ui/ToolHost.h`).

## Audio and video (add-on with libraries of its own)

Playback is the add-on "Media Playback" (`addons/media-playback`). The program only knows the interface
`src/media/MediaApi.h` and draws the controls (`src/ui/MediaPlayback.*`, `MediaView`, `MainWindowMedia.cpp`);
the add-on brings two libraries from `plugins/media` (a small plugin the program loads at start, and the
player built on Qt Multimedia, loaded with the first audio or video file) plus Qt Multimedia 6.4.2 of
Ubuntu 24.04 and an FFmpeg 6.1.1 that decodes only. To build the add-on:

```
sudo apt install qt6-multimedia-dev nasm          # build time only; the program does not link Qt Multimedia
tools/get-ffmpeg.sh                                # Ubuntu's FFmpeg 6.1.1 source -> tools/build-ffmpeg.sh -> build/ffmpeg
                                                   # (applies tools/ffmpeg-patches, about 8 minutes on 2 cores)
packaging/build-addons.sh build build/addons-dist  # addons/media-playback/stage.sh copies the libraries in
```

Why the parts are as they are (details in `plugins/media/MediaEngine.h` and `tools/build-ffmpeg.sh`):
the add-on's libraries are loaded by their full path before Qt Multimedia, so the system's FFmpeg or Qt do
not matter; FFmpeg is loaded only for the built-in decoding (the system's GStreamer has its own);
two FFmpeg decoders are left out because Qt 6.4.2's backend crashes on them; one FFmpeg patch gives
Matroska streams a duration (else no seeking in .mkv / .webm); without a sound device the player gets no
audio output (Qt 6.4.2's FFmpeg backend crashes otherwise). The test run needs a sound server; a null
sink is enough: `pulseaudio -D --exit-idle-time=-1`, then `PULSE_SERVER=unix:/tmp/pulse-*/native`.
From the build folder: `QB_PLUGIN_PATH=build/tool-plugins ./build/quillbinder` (uses the system's Qt
Multimedia and FFmpeg).

## Importers: old Word files and web pages (add-ons with a plugin)

`src/import/ImporterApi.h` (interface 1.0) lets an add-on read files by suffix and import web pages. An importer
hands back plain structs (`Content`: paragraphs with runs, notes, pictures); the program makes the document
(`src/import/ImportedContent.*`), so styles, lists, notes and pictures follow the project. Both importers are
built into `build/tool-plugins/` and delivered as add-ons:

- `plugins/importers/word97` → add-on "Word 97–2003 Import" (`addons/import-word97`): a reader of the Word binary
  format (compound file, piece table, character / paragraph / style properties, lists, tables, fields, footnotes,
  endnotes, comments, inline pictures) without another program; LibreOffice (if installed) for Word 6.0 / 95.
  `build/plugins/importers/qb_doc_dump file.doc` prints what the reader makes of a file.
- `plugins/importers/web` → add-on "Web Page Import" (`addons/import-web`): fetching with Qt Network, a forgiving
  HTML parser (`Html.*`), the article finder (`Article.*`, the approach of "readability" tools) and the window.
  The add-on brings Qt's TLS plugin of Ubuntu 24.04 (`addons/import-web/stage.sh`), because the AppImage has none;
  OpenSSL 3 comes from the system. `build/plugins/importers/qb_web_extract page.html [address]` prints what the
  article finder makes of a saved page.

To try them from the build folder: `QB_PLUGIN_PATH=build/tool-plugins ./build/quillbinder` (HTTPS then uses the
system's Qt TLS plugin).

## Projects in Scrivener 3 format

`src/import/ScrivenerFormat.*` reads and writes `.scriv` folders (File > Project Format, New Project > Format, opening a
`.scrivx`). The window works on a working copy (an ordinary project in `~/.local/share/Quillbinder/ScrivenerProjects/`)
whose `Project::scrivenerPath` names the `.scriv`; `Project::save` then calls `scrivener::sync`, which writes only what
changed (state in `settings/scrivener-sync.json`: hashes of both sides, so files Scrivener changed meanwhile are not
overwritten). What is written follows a real Scrivener 3.4 project (the sample of github.com/writerslogic/scrivener-mcp,
`tests/sample-project.scriv`); the header of `ScrivenerFormat.h` lists what is and is not written. `tests/scrivener_checks.cpp`
checks it (with `QB_SCRIV_SAMPLE` also against the real sample: an unchanged save leaves every file byte for byte as it was).

## Compile part 2: format editor, PDF, EPUB

`src/compile/PdfExporter.*` (own paginator on top of Qt's text layout: footnotes at the page bottom, headers and footers,
right-hand chapter pages, widows and orphans, a contents page with links, PDF bookmarks written into the file by an
incremental update), `src/compile/EpubExporter.*` (EPUB 3 with NCX, checked with epubcheck), `src/compile/Look.*` (the
look of a paragraph, shared by both and matching the DOCX writer), `src/ui/FormatEditor.cpp` (the nine pages of the
format editor). Style overrides, replacements, transformations and the contents settings are part of
`compile::Format` (`CompileFormat.h`); the compiler applies them (`Compiler::finish`). The exporter interface is 1.3
(`ExporterApi.h`): plugins built for 1.2 must be rebuilt (the bundled Markdown plugin is part of the update).

## Layout

| Folder | Contents |
|--------|----------|
| `src/core` | project model, binder, document store, snapshots, backups, search, templates |
| `src/text` | script formats and their elements (`Script`), Fountain and Final Draft files (`ScriptIO`), Markdown dialect and RTF reader/writer, display formats, formatting set by hand (`DirectFormat`), paragraph and character styles (`Styles`), pages of the page view (`PageLayout`, the one file that uses private Qt headers) |
| `src/compile` | compile model, formats, DOCX writer, PDF and EPUB writers (`PdfExporter`, `EpubExporter`, `Look`), Fountain and Final Draft exporters (`ScriptExporters`), plugin manager, exporter API |
| `src/import` | file import (txt, md, rtf, docx, odt, fountain, fdx, images, PDF), Scrivener import, projects in Scrivener 3 format (`ScrivenerFormat`), the importer interface for add-ons (`ImporterApi.h`) and what the program makes of their content (`ImportedContent`) |
| `src/ui` | all windows and dialogs; the program's side of the writing tools and the link between running instances (`ToolHost`, `MainWindowTools`); themes (`Theme`: colors and fonts per area, theme files; `ThemeDialogs`: preview, theme list); commands and shortcuts (`Commands`, `KeyboardPage`); menus, toolbars and layouts of the main window (`MainWindowBars`, `ToolbarConfig`, `Layouts`); copyholders (`Copyholder`); page size dialog (`PageDialogs`); scriptwriting (`ScriptEditing`: Tab, Enter and auto-completion in the text editor; `MainWindowScript`: commands; `ScriptDialogs`: Script Settings) |
| `src/tools` | `ToolApi.h`: the interface between the program and writing tools (plugins) |
| `src/media` | `MediaApi.h`: the interface between the program and a media engine (add-on "Media Playback") |
| `plugins/` | example C++ exporter (Markdown), JSON command plugins (LibreOffice, Pandoc), writing tools (`plugins/tools/`: name generator, scratch pad, text statistics), media engine (`plugins/media/`), importers (`plugins/importers/`: Word 97–2003, web pages) |
| `resources/` | app icon, bundled Breeze icon subset (LGPL-3.0-or-later, see `resources/icons/COPYING-BREEZE`) |
| `addons/` | sources of the add-ons (themes, project templates, compile format, scriptwriting pack, the three writing tools, media playback, the two importers) |
| `tools/` | `make-package.py` (builds .qbpkg), `qb-template.cpp` (project template from an outline; `--dump-script` prints a script format), `make-name-lists.py` (name lists from Faker), `build-ffmpeg.sh` and `ffmpeg-patches/` (the add-on's FFmpeg), `gen-resources.py`, `update-icon-index.sh` |

## Credits

- Main Developer: Claude.AI w. Opus 5.5 Extra

## License

Quillbinder is under the Zero-Clause BSD license (0BSD, `LICENSE`): anyone may use, copy, change and
pass it on, with or without fee, without conditions. The parts of other projects it uses, and their
licenses, are listed in `THIRD-PARTY.md`; their full license texts are in `resources/licenses` and in
the program under Help > Third-Party Licenses (made by `tools/collect-licenses.py`).
