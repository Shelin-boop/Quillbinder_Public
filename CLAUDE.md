# Quillbinder: notes for Claude Code

Quillbinder is a writing program for Linux (binder, editor, corkboard, outliner,
inspector, compile), for writers who know Scrivener; it reads and writes Scrivener's
file formats but is an independent program (Apache License 2.0 since 0.80.0; 0BSD before). C++17 and Qt 6.12.0,
The Qt Company's own build (since 0.80.0; Ubuntu 24.04's Qt 6.4.2 before), built on Ubuntu 24.04. Never copy text, pictures or code of Scrivener, Scapple or
their manuals and tutorials into the source; describe functions in words of your own.

`docs/CHANGES.md` lists what each version brought; the details of a new version go there. `docs/manual.md` is
the user's manual (shown inside the program under Help).

**One source, two repositories** (since 0.60): the development repository is private; the public repository
`Shelin-boop/Quillbinder_Public` is a copy of its committed files, made by `tools/publish-public.sh` (one commit
per published version, no shared history). Paths in `tools/public-exclude.txt` and text between the lines
`<!-- private:begin -->` and `<!-- private:end -->` stay private. `tools/legal-check.py` must pass before
anything is published (see "Publishing, legal check (0.60)" below).

## The person you work for

A writer, not a programmer, who wants results, not code. Explain in plain words what
changed and what was checked. Preferences:

- If something is not verified, say "I don't know" first; mark guesses as guesses.
  Say when information may be out of date.
- Dates yyyy-mm-dd, 24 h times, SI units.
- Ask when a request is unclear; do not guess at things that are expensive to redo.
- Longer texts for the user (reports, status) as a .docx file; write "..." and not the
  single ellipsis character.
- American English, Chicago style for texts meant to be read.

## Hard rules

- **One line** (since 0.60.0 alpha, 2026-10-31): the private and the public versions are the same source and the
  same version number. Audio and video for everyone: the add-on "Media Playback" (free formats), the add-on "Extra
  Video and Audio Formats" (formats licensed under patents), and the package `media-all-formats.qbpkg` that installs
  both in one step (built by `packaging/build-addons.sh`).
- **Runtime**: the current AppImage is 0.80.0 beta, runtime id `u2404-qt6.12-r1` (`APP_RUNTIME`, `QB_QT_VERSION`,
  `APP_RUNTIME_SINCE`, `APP_RUNTIME_DATE` in CMakeLists.txt; a new AppImage with other libraries needs a new id).
  The AppImages 0.2.0 to 0.70.0 had `u2404-qt6.4-r1` (Ubuntu's Qt 6.4.2). Program updates (.qbpkg) run on the
  AppImage's libraries: build on Ubuntu 24.04 with Qt 6.12.0 of `tools/get-qt.sh` (`/opt/qt/6.12.0/gcc_64`; CMake
  refuses another Qt version on Linux) and add **no new library dependencies**. Something that needs a library goes
  into an add-on (`addons/`, see the Media Playback add-on with its own FFmpeg). Add-ons with Linux libraries name
  the runtime in their manifest (`tools/make-package.py` adds it, and `minApp` = `APP_RUNTIME_SINCE`); the program
  refuses to install or load one of another runtime (`addons::fitsRuntime`).
- **Version**: `APP_VERSION` in CMakeLists.txt (numbers only: it is compared by the loader and
  the packages; a word such as "alpha" goes into `APP_VERSION_LABEL`, which is only shown);
  raise it for every delivery (fixes:
  third number, a milestone: second number).
- **Checks**: every change gets checks in a test program under `tests/` (scripted UI
  runs with screenshots: see `tests/fixes_tests.cpp` for a small, current example).
  A reported problem gets a check that fails without the fix. Which suites run is set by the
  working rules (default: the changed parts, their neighbors and a random 10 % sample, X11 only;
  the full suite only when the user asks); every suite that runs must end with "0 failure(s)".
- **Menus**: every menu command must work and be enabled only when it can act; the
  menu audit in `tests/m13_tests.cpp` walks all of them.
- Look at screenshots of what you changed (the tests save them); several bugs were
  found only that way.
- Keep `docs/manual.md` and `docs/CHANGES.md` in step with every change; `docs/status.md` stays as it is.

## Build

    tools/cloud-setup.sh                  # once on a fresh Ubuntu 24.04 (as root); fetches Qt 6.12.0 (tools/get-qt.sh)
    cmake -S . -B build -G Ninja -DQB_TEST_SWITCHES=ON   # the switches the suites need (off by default since 0.70.0)
    cmake --build build -j"$(nproc)"

## Tests

    QB_SUITES="v040 m13" QB_SAMPLE=10 tools/run-all-tests.sh build/test-results-x11 x11   # the default run
    tools/run-all-tests.sh build/test-results-x11 x11       # the full suite, only when the user asks
    xvfb-run -a build/tests/qb_fixes_tests /tmp/shots       # one suite by hand

- `QB_SUITES` names suites as the runner prints them (`qb_tests`, `ui`, `m6` ... `m14`, `fixes`, `v017` ...).
  `QB_SAMPLE=10` adds whole suites drawn at random until about 10 % of the checks are covered (counts from
  `tests/check-counts.txt`); it prints the seed and the suites, and `QB_SEED=<n>` repeats a sample. A full X11 run
  writes `tests/check-counts.txt` anew: commit it with the job. Keep logs out of the conversation: read the runner's
  one line per suite and the FAIL lines, not whole logs.

- `qb_tests` (core, formats, compile), `qb_ui_tests`, `qb_m6_tests` ... `qb_m15_tests`
  (one per milestone; m14 printing; m15, the tutorial of 0.15 to 0.30, was removed in 0.40),
  `qb_fixes_tests` (corrections of 0.14.1), `qb_v017_tests` (0.17: icons, icon bar, fonts,
  color buttons, folder comments, mind maps, developer guide), `qb_v018_tests` (0.18: Compare and
  Transfer with Word / LibreOffice files, Mind Map Help and example board; uses LibreOffice and
  pdftoppm when installed), `qb_v019_tests` (0.19: Page Down / Page Up, crash and freeze reports,
  sessions that ended unexpectedly, saving on SIGTERM, Help > System Information and Crash Reports;
  starts `<build>/quillbinder` and ends it with signals), `qb_v020_tests` (0.20: the Objects sheet with
  templates, colors, folders and links; the data-safety fixes: control characters, unreadable text files,
  backups over 4 GiB (a sparse 4.3 GB file, about 30 s; `QB_SKIP_BIG_BACKUP=1` leaves it out), autosave
  while typing, Scrivener-format working copies, the spare copy of the project file, mind map ranges), `qb_v021_tests` (0.21:
  File > Options in Scrivener's layout and what each option does, Ctrl + wheel zoom, the inspector's zoom, mind map
  colors / shapes / icons / line menus, the Compile Overview window), `qb_v0211_tests` (0.21.1: the format bar's target,
  the editable bookmark preview and its mirror (a stress run of 400 edits; `QB_MIRROR_SEED` repeats one, `QB_MIRROR_DEBUG=1` traces),
  a zoom per part, mind map backgrounds per theme kind, places kept with the project, formatted object texts, credits), `qb_v0212_tests`
  (0.21.2: Scrivener style marks and paragraph styles, project bookmarks as Scrivener for Windows writes them, marks left in projects
  imported before 0.14 taken out when they open, the writer's standard settings put once; `QB_USER_SAMPLE=<folder.scriv>` also imports
  a real project), `qb_v040_tests` (0.40: the welcome project, the Quick Start Guide, Third-Party Licenses and About,
  "Combined Text", no tutorial or pen name left, menu places of the guide and the welcome texts), `qb_v0401_tests`
  (0.40.1: a family per design of a font with several designs, the format buttons of a selection), `qb_v0402_tests`
  (0.40.2: no jump on a click or right-click, the look per project and the built-in themes unchanged, the border,
  the Appearance submenus of the right-click menus, project settings saved and loaded with the per-section choice,
  style changes reaching all text of the style), `qb_v0403_tests` (0.40.3: titles from the first line, No Style redefined
  from the panel, the short spelling menu, the format bar showing what is seen, dialogs above the Styles panel, the alignment
  of the main text, the chosen style list), `qb_v0601_tests` (0.60.1: Paste with Formatting, Show Invisibles, the total of a
  folder, paragraph styles over several paragraphs), `qb_v0602_tests` (0.60.2: the three ways to paste in the right-click menu),
  menu audit.
- `qb_v0604_tests` (0.60.4: the paragraph buttons of the format bar, the Wayland line of the README).
- `qb_v0605_tests` (0.60.5: a generated 500,000-word project in Scrivener format: autosave times and what it writes,
  the full save, the fingerprint, files changed in Scrivener; the right-click menu's time and size, Qt's animations off,
  the quick save when a dialog takes over). Times are printed; the limits are generous for a two-core machine.
- `qb_v061_tests` (0.61: tool interface 1.1 with a tool built for 1.0 (`tests/tool10`, the old header), Undo/Redo in the
  standard toolbar, the add-on Speech Transcription: models, headers, languages, pieces, files, the package, the button and
  its menu, the inspector tab, the options, downloads from a local test server, Vulkan through Mesa's lavapipe
  (`GGML_VK_VISIBLE_DEVICES=0`), recordings from a WAV file and a PulseAudio pipe source). Needs `tools/get-whisper.sh` and
  the add-on package; `QB_SPEECH_FAKE=1` writes "Piece n" instead of running a model; `QB_SPEECH_MODEL_FILE=<ggml model>`
  adds checks with real recognition; `QB_SPEECH_OFFICIAL=small` downloads that model from Hugging Face through the add-on
  (allowed in the cloud session since 2026-11-08: `huggingface.co` and its download host `us.aws.cdn.hf.co`);
  `QB_SPEECH_EXTRA="<wav>=<word>|<word>;..."` (with `QB_SPEECH_HINT`) adds recordings that must hold one of the words.
- `qb_v064_tests` (0.64: the spelling menu at every place of words with apostrophes, the autosave while typing on
  (a gap after 5 s, at most 30 s; about 45 s of typing), the card size kept when settings are read again, the lock with
  device and renewal, a lock taken over while open, conflicts kept as snapshot and copy (texts, the project file,
  Scrivener format), opening and closing without a change writes nothing (also a .scriv), texts not arrived yet).
  The speech add-on's Record only and Transcribe / Transcribe All are checked in `qb_v061_tests`.
- `qb_v0641_tests` (0.64.1, the fixes of the system test's first run: Markdown table cells with empty paragraphs and
  tables inside cells, Split at Selection next to and inside tables, a text only shown not written over a newer one,
  nothing replaced when the other version cannot be kept (text, notes, project file), a figure cut and pasted, compile
  progress with the event loop running, cells not justified, Merge with notes, Merge Cells / Undo / Redo / Delete Rows,
  typing in a 1,000-row table against a plain `QTextEdit`; `QB_V0641_ONLY_TYPING=1` runs only that, `QB_V0641_KEYS=<n>`).
- `qb_v0642_tests` (0.64.2, the fixes of the system test's full run of 0.64.1: word counts, a picture's format on
  letters and pasted pictures, italic from RTF face names, German quotes, Markdown paragraphs (leading spaces, the style
  Centered, a bold run ending in a space), the spare copy, project and .scriv folders that are gone, a .scriv as
  Scrivener writes it opened and closed unchanged, pictures in notes, outside changes while open, Scrivener's XML lock,
  linked notes over several formats, H2O by subscript, stale menu entries, the Documents menu, `showIfOpen`).
- `qb_v0650_tests` (0.65.0, the fixes of the external audit of 2026-11-10: safe copies, the save journal and its recovery
  (also a journal naming files outside the project), contents-by-title.txt, pictures that cannot be written, partial reads,
  Roll Back (core and inspector), incomplete backups, Duplicate, object pictures, storage conversion, same size and time,
  a 70 MB archive, command exporters (exit status, event loop), bundles and signatures (RFC 8032 vector, the release key,
  test keys made with openssl, `QB_TEST_SIGNING_KEY`, `QB_REQUIRE_SIGNED`), the scratch pad, the close question, the end by a
  signal, the lock, Scrivener format with a document and a picture changed elsewhere, the manual). Needs
  `<build>/addons-dist/tool-scratch-pad.qbpkg`, python3 and openssl.
- `qb_v0700_tests` (0.70.0 beta, the fixes of the two external audits of 2026-11-10 (report A: 31 faults, report B: 14
  items) and the writer's decisions: Markdown round trips of touching formats (4,000 random sentences), the RTF
  reader's lists, damaged RTF and other encodings read-only, control characters told, the save state and the
  failed-save notice, mind maps that cannot be saved or read, backups per project, Restore, backup progress and
  Cancel, pictures in Merge and Split, questions with a default (a source check), project settings only safer,
  the 30 s latest save, the project file of a folder with several, binder depth, duplicate ids, foreign XML,
  counts, boards, Word 97 damaged copies (needs `plugins/importers/qb_doc_dump`), reading speed, the release
  workflow's jobs, `make-package.py sign`, `launcher::coreOrigin`, the self-test, German, the dark page).
  `QB_V0700_ONLY=markdown,rtf,damaged,saves,backups,pictures,dialogs,hostile,release,german` runs parts.
- `qb_audit1_tests` (0.64.1, the fixes of the external audit of 2026-11-09: ids of a project file and names in archives
  (`core/PathSafety.*`), damaged ZIP entries, a Word 97 file with a looping sector chain (compiles the plugin's `Cfb.cpp`),
  XML nested 20,000 deep, the keys a project may hold and the question of Load Options, the code warning of packages,
  links (`ui/LinkOpen.*`, also in imported web pages), the socket folder, Scrivener's lock before every write and a .scriv
  file whose other version cannot be kept, the computer's name; `QB_RELEASE_BINARY=<files>` (':'-separated) searches a
  release build for the test switches' names).
- `qb_v0611_tests` (0.61.1: the quote rules, Convert Quotes from File > Options > Corrections with every place and the
  scope, Project > Delete Old Snapshots with its question, Scrivener format: deleted snapshots leave the .scriv).
- `qb_v062_tests` (0.62: settings per project: the first start window, File > Options with "Settings for", what goes
  where (also changes made outside the window), Use Program Settings, the switch "which settings come first" and the look
  following it, the Options section of project settings files; tables and pictures: designs, the Markdown and RTF forms
  and RTF of other programs, the editor's table and picture commands in both storage formats, Scrivener format, the
  needed version 0.62.0, compile to PDF, Word, e-book and Markdown; LibreOffice opens the Word file when installed,
  `QB_SKIP_SOFFICE=1` leaves that out). The runner sets `QB_SKIP_FIRST_START=1` for all other suites.
- `qb_w2_tests` (Windows port W2, on Linux: the packages' platform field, the process check, pruning reports with
  their memory dumps; no window).
- `qb_v0603_tests` (0.60.3: folder totals in the binder and the footers, Inspector Background, the color of the toolbars,
  font names that are not families, double-click beside the text reveals the document).
- Printing checks print into PDF files (Print to File) and read them with poppler-utils.
- The add-on Media Playback needs FFmpeg: `tools/get-ffmpeg.sh` once (5 to 10 minutes),
  then the test runner builds the add-ons.
- Scrivener format: `tests/scrivener_checks.cpp` reads a real Scrivener 3 project when
  `QB_SCRIV_SAMPLE=<folder.scriv>` is set (a public sample: GitHub
  writerslogic/scrivener-mcp, tests/sample-project.scriv, pinned to commit d4d42c3 of 2026-11-08 by a hash over its
  files, `kTreeSha256`; since 2026-10-31 it has no docs.checksum, which the program writes only when a .scriv has one).
- Offscreen runs have no window manager: a widget has focus only after
  `activateWindow()`; modal dialogs are answered with `QTimer` + `activeModalWidget()`.
- Never run `pkill -f` / `pgrep -f` with a pattern that also appears in your own
  command line: it kills the shell that runs it.

## Delivery

1. Raise `APP_VERSION`, update `docs/manual.md` and `docs/CHANGES.md`.
2. Tests as the working rules say (see Tests above).
3. Update package: `packaging/build-update.sh "What is new, line 1" "line 2"`
   gives `dist/quillbinder-<version>-update.qbpkg` (separate build dir `build-update`).
4. End-to-end with the real AppImage: `tools/e2e-appimage.sh <AppImage> <update.qbpkg>` (it also checks the first
   start with the welcome project and prints its first document; `-` instead of the package checks a new AppImage
   alone; give the work dir a short path: the instance socket's path may have at most 107 bytes). Only the current
   base AppImage, the update and the way back; older AppImages (`tools/e2e-old-version.sh`) only when asked.
   Add-ons are packed with `packaging/build-addons.sh`; an update and add-ons go to the user as one bundle
   (`tools/make-package.py bundle`).

## The runtime step: Qt 6.12, FFmpeg 9.0.2, fuzz tests, Apache-2.0 (0.80.0 beta)

- **Qt**: The Qt Company's own build of Qt 6.12.0 for Linux (aqtinstall 3.3.0, modules qtmultimedia qtimageformats
  qtpdf). The cloud session cannot reach download.qt.io: `.github/workflows/qt-linux.yml` (private only, in
  `tools/public-exclude.txt`) installs it on GitHub's runner for a pushed branch `qt/<version>` and puts a tarball on
  the pre-release `qt-<version>-linux`; `tools/get-qt.sh` downloads it (SHA-256 pinned in the script) or, where
  download.qt.io answers (GitHub's runners, the public release workflow), installs with aqtinstall. A new Qt version:
  push `qt/<version>`, pin the new sum, change `QB_QT_VERSION`, `APP_RUNTIME`, `APP_RUNTIME_SINCE`, `APP_RUNTIME_DATE`.
  Qt 6.12's Wayland plugin is one file (`libqwayland.so`); its FFmpeg backend links Qt Quick/QML/OpenGL (the media
  add-on carries them in `media/lib`, found through the plugin's RUNPATH `$ORIGIN/../../lib`); there is no GStreamer
  backend (the choice is listed as not available, a saved one falls back to FFmpeg).
- **Licenses**: `tools/get-qt-licenses.sh` (sparse GitHub checkouts of qtbase, qtsvg, qtimageformats, qtmultimedia at
  `v<version>`, qtwebengine-chromium at the commit of qtwebengine's tag or branch, ICU 73.2) then
  `tools/collect-licenses.py --qt-licenses build/qt-licenses <AppDir> resources/licenses` writes the list: Ubuntu
  packages as before, plus "Qt" (files of `/opt/qt`, `qtBuild`), "ICU (Qt's build)", every part of Qt's attribution
  files but those in `QT_LEFT_OUT`, and Qt PDF's parts. Source: `qt-everywhere-src-<version>.tar.xz` (the Qt
  Project's archive, `tools/collect-sources.py`, kind "qt").
- **FFmpeg 9.0.2** (sonames avcodec/avformat 63, avutil 61, swresample 7, swscale 10: what Qt 6.12's plugin needs),
  the original archive from Ubuntu's mirror (`tools/get-ffmpeg.sh`); `--disable-postproc` only where configure has it.
- **Fuzz tests**: `qb_fuzz_tests` (11 checks, about 10 s) and the libFuzzer harness `tests/fuzz/` (`-DQB_FUZZ=ON`, clang).
- **PDF metadata**: Qt 6.12's QPdfWriter writes an empty /Author itself; `addPdfNavigationEx` replaces existing
  entries of the keys it sets.
- **Qt 6.12 pitfalls** (found by the full run): QDom's default invalid-data policy is ReturnNullNode (a text node with
  a control character was left out; `TextUtil.cpp` sets AcceptInvalidChars before `main()`, `xmlSafeBytes` cleans the
  output); `QString::arg(char16_t)` writes the character, not a number (cast with `int(...)`); a dialog whose parent
  is a `Qt::Tool` window gets the main window as transient parent (`InputWatch::aboveToolWindow` sets the tool
  window at Show); `ItemsMimeData::retrieveData` must answer `QList<QUrl>`; line heights differ from Qt 6.4. Qt Multimedia 6.12 tries
  PipeWire first and crashes when libpipewire is there without `client.conf` (this machine): `core/AudioBackend.h`
  (`audiobackend::avoidUnconfiguredPipeWire`, called by both add-on engines) sets `QT_AUDIO_BACKEND=pulseaudio` then;
  a file is read in the background (`QtPlayer::m_playWhenRead`). Qt 6.12's PulseAudio input keeps at most 1024 frames
  in the server: v061 feeds its pipe source in 20 ms pieces (100 ms pieces lost two thirds of the sound).

## Crash and freeze reports (0.19)

The program writes reports into `~/.local/share/Quillbinder/CrashReports` (fatal signal, or the
main thread silent for 30 s); the user copies them from **Help > Crash Reports**. When the user
sends one, read it as described in `docs/developer-guide.md` section 10 ("Reading a crash or freeze
report"); `packaging/build-update.sh` keeps `dist/quillbinder-<version>-symbols.xz` (unstripped
program, same build ID) for `addr2line`: keep it with the package (the release workflow publishes it with each release; in the development repository it goes on the branch `assets`).
`src/core/CrashGuard.*` (handlers, watchdog, sessions) must stay async-signal-safe in the
handlers: no allocation, no locks, no Qt there. Windows (W2): `CrashGuardWin.cpp`, the same rule for its writer thread and
watchdog (fixed buffers, Windows calls only); reports in `%LOCALAPPDATA%\Quillbinder\CrashReports` with a `.dmp` beside.

## Options, zoom, palette (0.21)

File > Options (`src/ui/OptionsDialog.*`) builds every page from `AppSettings` (and `MindMapPrefs`, the zoom
settings) and writes everything back in `apply()`; a new option needs a field in `AppSettings` (load, save,
`resetToDefaults` covers its group), a widget with an object name `opt...`, a line in `apply()`, the code that
acts on it, and a check in `tests/v021_tests.cpp`. Settings the core libraries need are handed over in
`AppSettings::pushToModules()`. Ctrl + wheel: `src/ui/Zoom.*` (mark an area, or give it a handler). Colors offered
to the writer come from `src/ui/ColorPalette.*`. Corrections while typing: `src/text/AutoCorrect.*`.

## Scrivener style marks, standard settings (0.21.2)

- `ScrivenerImporter::stripMarkers` takes Scrivener's `<$Scr_...>` marks out of a text and gives paragraphs their style: a
  heading mark "Heading n", a paragraph style mark the project's style of that name (`<$Scr_Ps::n>` = the n-th id of the
  document's `content.styles`, names in `Files/styles.xml`; `scrivenerStyleName` maps "Centered Text" to "Centered"); marks may
  span paragraphs. `removeLeftoverMarkers` cleans projects imported before 0.14 (called by `MainWindow::adoptProject`; never for
  Scrivener-format working copies).
- The writer's standard (11.0 pt, 1.09, indent 0, 6 pt after; paper #d1d495, beside the column #999999; width 500 px) is the
  members' defaults in `AppSettings` plus `standardOwnColors()`; `load()` puts it once into older settings (key `app/standard0212`,
  removed by Defaults). Scrivener imports take the formatting of new projects through `ScrivenerImporter::setNewProjectFormatting`
  (handed over in `pushToModules`), keeping the Scrivener project's most used font.

## Parts, places, mirrored text (0.21.1)

- Zoom parts: `zoom::kInsp*` and `zoom::kObjectsSheet` in `src/ui/Zoom.*`; a part is a marked widget inside an area
  (`zoom::mark` + `attachFonts`); a root always gets its font set, so it does not take the zoomed font of an area around it;
  widgets made later in a zoomed area are scaled when Qt polishes them (`Filter::adopt`). `QB_ZOOM_DEBUG=1` traces scaling.
- `src/ui/DocMirror.*` keeps a copy of a document in step both ways (the inspector's bookmark preview); it copies changed
  spans piece by piece (not `insertFragment`, which adds paragraphs around lists), ignores Qt's "layout created"
  notifications, and rebuilds the copy only as a last resort.
- `src/ui/ViewMemory.*`: places kept in `Project::uiState` (settings/ui.json): text places per document or combined-text group
  (`TextColumnView::rememberPlace / restorePlace`), board views per board and kind (`MindMapView::setViewMemory`), picture
  and PDF zoom and page (`MediaView`), the inspector (`Inspector::saveState / restoreState`). Nothing here marks the project
  dirty; `MainWindow::saveUiState` writes them at close.
- Mind map backgrounds: `Settings::ownLight / ownDark` (file element `QuillbinderBackground`), `MindMapPrefs::boardBackground`
  / `boardBackgroundDark`, `MindMapDoc::applyBackground` works out the color shown; `BackgroundColor` stays for older readers.
- The format bar's target: `MainWindow::editor()` (TextEditor) and `objectTextTarget()` (`ObjectTextEdit`), both following
  `m_lastTextField` while the keyboard is on the format bar or in a popup.

## Objects (0.20)

The Objects sheet keeps its data in `objects/` of the project folder (one JSON file per object,
`objects/schema.json` for categories, templates, colors and folders), never in the `.qbx`, so older
versions keep working with the project; see `docs/developer-guide.md` section 11. Core: `src/core/Objects.*`
(`Project::objects()`), UI: `src/ui/ObjectsUi.*` (inspector tab `Inspector::Objects`, `ObjectsWindow`,
`ObjectTemplatesDialog`). Object links are `qbobj://<id>` (document links `qb://<id>`).

## Where things are

- `src/core`: project, binder model, document store (Markdown or RTF files), snapshots,
  search, backups, package installer. `src/text`: formatting (`DirectFormat` = what
  was set by hand, `Styles`, `DisplayFormat`), Markdown and RTF reading/writing.
  `src/ui`: main window (split over `MainWindow*.cpp`), editor, inspector, dialogs.
  `src/compile`: compile, built-in PDF/EPUB/DOCX. `src/import`: file import, Scrivener,
  Compare and Transfer (`Compare.*` engine, `CompareFiles.*` Word/LibreOffice writers, `MiniXml.*`).
- `addons/`: add-on packages; `packaging/`: AppImage, update and add-on build scripts;
  `tools/`: helper scripts; `resources/`: icons (`qbicons` themes), templates, manual.

## Tool interface 1.1, Speech Transcription (0.61)

- `src/tools/ToolApi.h` 1.1 adds to 1.0 without changing it: three functions at the end of `ToolHost` (a tool calls them
  only when `apiVersion() >= 110`), and the plugin-side interface `ToolExtension` (own IID; `toolhost::Host::extension()`
  asks the plugin object): inspector tabs (`Inspector::addExtraTab`, after Objects in the stack, buttons at the left of the
  row, kept in the state by key `toolTab`), toolbar buttons (`toolbars::setToolButtons`: part of the standard set; a
  toolbar the writer arranged gets each once, setting `toolbars/toolButtonsPlaced`; the arrow's menu set in
  `MainWindow::placeToolButtons` after every rebuild), a section in File > Options (`optionsPage()` "Section",
  `OptionsDialog::showToolSection`). Never change a 1.0 struct or function: the program reads tools built long ago.
- `plugins/tools/speech-transcription`: `libqb-speech.so` (the tool: commands, `TranscriptPanel`, the options, models and
  downloads with the checks) and `libqb-speech-impl.so` (`impl/SpeechEngine.cpp`: Qt Multimedia's `QAudioSource`,
  `SpeechLogic`'s `Segmenter`, whisper.cpp in a thread; `SpeechApi.h` between the two). `SpeechLogic.*` is Qt Core only
  (the tests compile it). whisper.cpp: `tools/get-whisper.sh` (v1.9.5, shared libraries, `GGML_BACKEND_DL` with all CPU
  variants and Vulkan; about 7 minutes on 2 cores) into `build/whisper`; the engine loads the compute modules from its
  `lib/` with `ggml_backend_load_all_from_path`. Translation: main language English and a fallback: every piece runs
  with `translate` (whisper translates only into English); the language of a piece is found first only for Careful,
  "keep the original" or another main language (an extra encoder run). Models: `SpeechLogic.cpp`'s catalog (SHA-1 of
  whisper.cpp's list; Small's and Medium's checked against the files on Hugging Face on 2026-11-08), saved in
  `<data>/tools/speech-transcription/models`; `QB_SPEECH_SOURCE` points the official source elsewhere (tests).
- The add-on brings Qt Multimedia (only `libQt6Multimedia.so.6`: the microphone needs no Qt Multimedia plugin) and Qt's TLS
  plugin; `addons/speech-transcription/stage.sh` exits with 3 when whisper is not built, and `packaging/build-addons.sh`
  then leaves the add-on out. Icons of the add-on are in the program's icon set (Breeze: microphone, record, list).

## The two external audits' fixes (0.70.0 beta, reports A and B of 2026-11-10)

- **Markdown emphasis** (A F-01, `MarkdownIO.cpp`): the reader takes runs of up to six marks, closes before it opens,
  follows CommonMark's rule of three, and lets the two kinds of one "***" run close in either order; the writer
  opens the longer-lasting format first and writes `<b>`/`<i>` for a bold or italic run whose marks would be
  ambiguous (a run that closes and opens, marks next to a space on the wrong side). `startsAt` uses a view (it copied
  the rest of the paragraph per character). Property check in v0700 (4,000 random sentences).
- **Readers**: `QPointer<QTextList>` for cached lists in the RTF and Markdown readers (Qt deletes empty lists; B F-1),
  list levels 0..8; `rtf::wellFormed` / `rtf::mended`; `DocumentStore::decodeStored` (UTF-8 strictly, UTF-16, zero
  bytes); a damaged or other-encoding file -> `m_damaged` + `m_unreadable` (shown as read, read-only, never
  written), signal `damagedFile` -> notice `damagedDocumentNotice`; `charactersLeftOut` once per document.
- **Failed saves** (A S-01): `MainWindow::noteSaveFailed` / `noteSaved` / `retrySave` / `updateSaveState`: status-bar
  label `saveState` (Saved / Not saved yet / red failure), `setWindowModified` with "[*]" in the title, retries every
  15 s for 2 min then 60 s, notice `saveFailedNotice` through `showWithoutTakingKeys` (again after 5 min), button
  `saveFailedElsewhere`. `Project::setUnsavedExtra` / `unsavedTitles` / `emergencyExtra`; `m_saveDelay` and the 30 s
  latest save whatever the delay (A S-09).
- **Mind maps** (A U-01, U-02): `MindMapDoc::saveOrTell` (emits the project's `saveFailed`, retries in 15 s),
  `mindmaps::saveAll(p, &err)` checked by File > Save, close and the end; `closeAll(p, discard)`;
  `mindmaps::writeUnsaved` for the emergency copy; a board that cannot be read is `isUnreadable()`, `locked()`, never
  written (`mindMapUnreadableNotice`).
- **Backups** (A F-02, U-05, U-06, U-09, F-11, U-10): `backup::projectFolder` ("<title> <key>", key = SHA-1 of the
  project folder's canonical place), `Where::ProjectFolder|AsChosen`, `backupsFor(Project*)`; packing and checking in a
  `QThread::create` worker with `setProgressHandler` (the window's QProgressDialog `backupProgress`, Cancel); fsync of
  the archive and `savebatch::syncDir` before the clean-up and of staged folders before the journal; ZipWriter leaves
  out `settings/lock`; `backup::restore` + `MainWindow::restoreBackup` / `restoreBackupFile` (File > Back Up > Restore
  From Backup).
- **Pictures** (A U-03): `DocumentStore::carryPictures(namesIn, from, to)` adds a picture under the target's resolved
  name too (QTextDocument looks resources up by the resolved URL); Merge, Split and every `setContent`.
- **Questions** (A F-03, B S-3): `ui/Ask.h` (`ask::question` with a default and an action name, `ask::confirm`); every
  `QMessageBox::question` names its default (v0700's source check).
- **Settings**: `AppSettings::keepSavingSafe` (a project may only make saving safer; ranges), Options opens for all
  projects (A S-05), `app/language` program-only.
- **Project file** (A F-06, F-07, F-08, B F-2, L-1): root element and version checked; nesting refused above 5,000
  (checked with QXmlStreamReader before QDom), flattened above 200 (`readItem` without recursion); duplicate ids get
  new ids with a copy of the shared text folder; counts saturate; `resolveProjectFile` prefers the .qbx named like the
  folder, else the newest, and `open()` tells about the others. Boards: ids, places and sizes bounded.
- **Word 97** (A U-11): pieces bounded by the stream, character counts by the pieces (16 million at most).
- **Build and release** (A S-04, S-03, S-02, U-07): `QB_TEST_SWITCHES` off by default (the runner refuses a build
  without it); the release workflow's jobs build (no key), tests (full suite but v060, one retry; since 2026-11-11 the release does not wait for it, the writer's decision), sign-packages
  (`make-package.py sign`), assemble (the full version), sign-checksums, publish; the installer keeps
  `package-manifest.json`/`.sig`, and `launcher::coreOrigin` checks them before every start (Linux; unsigned only with
  `QB_ALLOW_UNSIGNED_CORE`, which `tools/e2e-appimage.sh` sets). The first run on GitHub's container failed for the
  machine, not the program: no `unzip` (now in `tools/cloud-setup.sh`) and no sound server (PulseAudio refuses the job's
  home folder, owned by another user; the runner now starts it with folders of its own, `-n` with a null sink only, log
  `pulseaudio.log`, and prints "(sound server: ...)"). On a second failure the workflow prints the failed checks with
  their details into the job's log, which the session can read (`gh api .../actions/jobs/<id>/logs`; the artifacts
  are on a host the session cannot reach). `ZipWriter` counts progress and sees Cancel per megabyte of a file.
- **German** (S-06): `ui/Language.*` (QTranslator from `:/translations/quillbinder_de.qm` and `qt_de.qm`, setting
  `app/language`, `QB_LANGUAGE`), `translations/README.md` for lupdate/lrelease. **Dark page** (S-07): in
  `theme::resolvedColors`, a dark theme with the standard own paper shows #1e1e1e with white text (or the theme's
  own page). **Self-test** (S-08): the font resolver. **Crash reports** (Info-1): a note about personal names.

## The external audit's fixes (0.65.0, report of 2026-11-10)

- **Save journal** (`src/core/SaveBatch.*`, audit F19): `Project::saveImpl` runs a `savebatch::Batch` (`Scope` makes it
  `current()`); `savebatch::put(path, data, error, title, place, done)` stages `<file>.qbstage` (synced) in a batch, else
  writes at once; `done` runs once the file is in place (marks, stamps, `savedImages`). `commit`: with 2 files or more the
  journal `settings/journal.json` (target, staged, sha256, replaces, title, place, device, key), then POSIX `rename`
  (`savebatch::replace`, MoveFileEx on Windows), then the journal goes. A staging failure stops the whole save
  (`batch.drop()`, entries stay dirty). `savebatch::recover` at `Project::open` and before each save: a committed journal
  is finished (target unchanged since: renamed; changed: the staged file kept in `settings/conflicts`; missing: listed as
  lost), stray `.qbstage` files without a journal are removed; only files inside the project, and only their own staged
  name. `Project::saveRecoveryText` -> `MainWindow::m_lastSaveRecovery` (notice `saveRecoveryNotice`). Backups skip
  `.qbstage` files and the journal. Writers on the batch: `DocumentStore::writeEntry` and its pictures, `Objects::writeJson`,
  `Project::saveIndex`, `contents-by-title.txt` (`Project::titleListText`, Quillbinder format only), `convertStorage`
  (its own batch). Not on it: the ui state, mind maps, snapshots, the Scrivener write.
- **Pictures** (F04): `DocumentStore::serialize` encodes PNG into memory and writes it through `put`; a failure sets
  `serializeError()`, and `writeEntry` fails. `importImage` writes checked (the resource stays for the save).
- **Reads** (F23): `readFile` treats a read error or fewer bytes than the file's size as unreadable (`m_unreadable`).
- **Same size and time** (F14): `writeEntry` compares the file with `Entry::read`, `saveIndex` with `m_indexSha`.
- **Snapshots** (F02): `snapshots::load(..., &error)` and `rollBack(p, s, error, allowEmpty)`; the inspector reads first,
  asks for an empty snapshot, stops when the "before" snapshot fails (`snapshotRollBack`, `snapshotList`,
  property `qbLastRollBackError` with `QB_NO_DIALOGS`).
- **Backups** (F03): skipped files -> `... (incomplete).zip`, true with *error; retention counts complete backups only and
  removes incomplete ones older than the oldest kept. `MainWindow::tellBackupProblem` (open, close). Test switch
  `QB_TEST_UNREADABLE=<path suffix>` in `ZipWriter::addDirectory`.
- **Safe copies** (`safesave::copyFile`, `safesave::writeFile` in `SafeSave.h`, header-only, also for plugins): used by the
  Scrivener media/icons/web page files, `copyDocument` (F06, `duplicateItems` removes a partial duplicate), the command
  exporter (F11, also exit status; F24: waits in 100 ms steps with `progress::tick`).
- **Scrivener format** (F10, F05, F25): the removed-document sweep knows only `state.theirs`; documents of the .scrivx on
  disk this program never knew (`madeElsewhere`) keep their folders, and the .scrivx is then not written (`binderKept`,
  `keptTheirs`, one notice per stamp: `Project::scrivenerBinderTold`). `SyncState::binder`/`binderKnown` ("binder" in
  scrivener-sync.json): the UUIDs of the .scrivx at the last read or write; `mergeFromOld` keeps working-copy documents
  not among them (states of older versions: `theirs` as before). Media and mind maps: changed only in the .scriv ->
  kept; both -> `keepTheirs` first; copies through `safesave::copyFile`; a mind map or icon failure fails the sync.
- **Objects** (F09): removed files and orphan pictures are deleted in the batch's `after` step, only when all writes
  worked; pictures that cannot be removed stay listed.
- **The end** (F07): `saveForTermination` on failure: `Project::writeEmergencyCopy` (RTF of every dirty entry,
  `DocumentStore::writeUnsaved`, `<data>/Emergency/<title> <time>/` with README.txt), `MainWindow::writeEmergencyNotice`
  (`<data>/Emergency/notice.txt`), `crash::setTerminationSaveFailed()` -> exit status 1; `showEmergencyNotice` at the
  next start (main.cpp).
- **Close question** (F26): `closeUnsavedQuestion` with Keep Open (default), `closeSaveElsewhere`, `closeLoseChanges`.
- **Scratch Pad 1.1** (F01): `save()` returns bool (safesave), switching notes / New Note / folder / rename / send refuse
  after a failed save (`tellUnsaved`), `saveEmergencyCopy` at the end. **Speech Transcription 1.4**: `TranscriptPanel::save`
  bool, `saveOrKeep` (emergency copy) before a recording and at the end.
- **Lock** (F13): `MainWindow::lockNotWritten` (notice `lockWriteNotice`, once per opening).
- **Bundles** (F18): `Addons.cpp` `prepare` (every part into `.new-<name>`, the program's self-test), then all renamed,
  the placed ones put back when one fails, then `finish`.
- **ZIP reader** (F17): archives above 64 MiB are mapped (`ZipReader::m_file`), 2 GiB and more refused.
- **Signatures** (F12, `src/core/Signing.*`, Monocypher 4.0.3 unchanged in `src/core/third_party/monocypher`, BSD-2-Clause,
  listed in resources/licenses and licenses-windows): `manifest.sig` = Ed25519 signature of manifest.json
  (`{"algorithm","key","signature","signs"}`); `signing::checkManifest`, `trustedKeys()` (the release key `kReleaseKey`,
  `packaging/quillbinder-signing-public.pem`; test builds also `QB_TEST_SIGNING_KEY`). `PackageInfo::signature`,
  `trusted()` (a bundle: every installed part), `addons::trustProblem(info, Unsigned::Refuse|AllowAddons|AllowAll)`:
  a mismatching signature never; a program update only signed; an add-on after the window's warning
  (`addonSignedNote` / `addonCodeWarning`, Cancel the default). `--install <file> --allow-unsigned` on the command line
  (e2e-appimage.sh, the full AppImage's AppRun, win-check*.sh pass it). Test builds allow everything unless
  `QB_REQUIRE_SIGNED` is set. `tools/make-package.py` signs with `QB_SIGNING_KEY_FILE` (openssl). The release workflow:
  step "The signing key" (secret `QB_SIGNING_KEY`, checked against `kReleaseKey`, else the run stops), the packages
  signed while they are made, "Checksums and their signature" (SHA256SUMS.txt + .sig + the public key, after the
  third-party sources), `publish-release.sh` checks the signed list instead of making it again.
- **Runner** (F08): `tools/run-all-tests.sh` ends with 1 and `failed: ...` / `not built: ...` (`runx` for suites that may
  be missing; the line of the long-gone `menu_audit` was removed).

## Fixes of the system test's full run (0.64.2, 2026-11-10)

- **Formats changed while walking fragments**: never change a char format inside a `QTextBlock::iterator` loop (Qt
  invalidates the iterator: SIGSEGV in S-W14); collect (start, end, format) first, then apply. Done in
  `TextEditor::addLinkedNote`, `scrivRtf` (links, note anchors), `FileImporter`'s `applyImportOptions`, `ObjectsUi`,
  `Inspector` (snapshot zoom).
- **Menus filled in `aboutToShow`** (Go To, Quick Reference, bookmarks): the lambdas capture the item's id and look it
  up (`m_project->item(id)`); a pointer captured there crashed after the item was deleted (S-E04).
- **Pictures**: `text::withoutPicture` / `text::pictureSafePieces` (a picture's format on letters is not a picture; one
  picture per U+FFFC) in `insertSemantic`, `RtfIO`'s runs, `MarkdownIO`'s segments; `TextEditor` drops the picture
  format from the current char format after a picture. `insertSemantic` copies the source document's picture resources
  (RTF on the clipboard names them `rtfimage://...`; the pasted picture was lost at the save).
  `DocumentStore::setNotesContent` copies resources as `setContent` does (pictures in notes of a .scriv).
- **Markdown**: the style Centered is written `::Centered::` (`->text<-` stays for centering by hand; `toStandardMarkdown`
  turns the style back into `->text<-`); `trimmedAscii` for centered paragraphs (no-break spaces at the ends stay);
  white space at the end of a run moves out of its markers also when it is a text item of its own (`**ca **` was read
  back as asterisks).
- **Notices during typing**: `MainWindow::showWithoutTakingKeys` (not modal, `WA_ShowWithoutActivating`, the safe
  button focused) for `lockTakenNotice` (Keep Working the default and escape button) and `conflictNotice`.
  `changeEvent`'s full save on the way to another program emits `saveFailed` on failure (not the quick save when a window of the program takes over: m9 hung on a second modal box over Close Project's question). `TextEditor::leaveList` (Backspace at a list
  item's start, a list switched off): the style's block format with the paragraph's own direct spec.
- **Scrivener format**: `sameExceptStamps` compares the .scrivx as XML (`canonicalScrivx`: elements, sorted attributes
  without the root's Modified/ModID/Device, text, white space only in leaf elements), else bytes; a .scriv opened and
  closed without a change stays byte for byte (label colors kept as written
  when the 8-bit color is the same, `IncludeInCompile` and `MetaData` not added to elements that had none, `TextSettings`
  only for new items, `Device` ignored by `sameExceptStamps`, `search.indexes` made only with a change or for a new
  .scriv); a failed `search.indexes` write fails the sync; `sync` saves the state also when `write` fails (what was
  written is known); `write` refuses a .scriv that is gone (`state.theirs` known, same path); files kept as theirs
  (`keptTheirs`) clear the fingerprint so the next opening reads the .scriv (S-D02); the importer skips QSaveFile
  leftovers (`isSaveLeftover`) and keeps a synopsis' leading spaces; `lockedByScrivener` reads key=value and XML.
- **Projects**: `Project::resolveProjectFile` maps `settings/spare-copy.qbx` to the project's own file; `saveImpl`
  refuses when the project folder is gone (no folder made anew with only the changed documents).
- **Second start**: `main.cpp` asks the running programs (`showProject` through the tool host's link,
  `MainWindow::showIfOpen`) before making a window; the program that has the project open raises it.
- Also: `SettingsLayer::takeProjectValues` writes program-only keys changed in "This project" scope to the program
  file; `manualSave` with Scrivener's lock emits `saveFailed` (one notice); Take Snapshot reports failures; command
  exporters drop an argument whose `{author}` / `{title}` is empty with its option; word counts
  (`belongsToWord`); AutoCorrect's `smartQuote(typed, textBefore, style)` and `apostropheBeforeLetter`; the RTF reader
  takes italic and bold from a face name also without kept fonts; `mergeFormat` changes the whole word only inside it;
  `SpeechTool`'s download handlers hold their own reply (a resumed download's old reply no longer reaches the new one).
- **Test switch**: `PluginManager::setMediaOffForTests` (QB_TEST_SWITCHES builds) acts as if Media Playback were not
  installed (a plugin loaded in the process stays loaded when its add-on is removed).
- **System test (harness)**: S-T01's typing gate in the 1,000-row table is gone (the writer's decision of 2026-11-10:
  such a table is a spreadsheet's job); Scrivener format keeps inline notes as linked notes (decision "1A": the model
  `scrivenerText`); `showDoc`, `cursorTo`, `toEnd` activate the window (focus, Edit commands and shortcuts need it
  without a window manager); `formatRuns` ignores bold/italic/underline/strike on white space and the formatting of an
  empty text; `fileDialogLater` sets the focused name field; S-Y04 syncs through `SyncSim` (conflicted copies).

## The external audit's fixes (0.64.1, report of 2026-11-09)

- **Names from outside** (`src/core/PathSafety.*`): `isSafeId` for every id that becomes a folder name (`Project::readItem`
  gives an unsafe id a new one; `contentDir` / `snapshotsDir` map one anyway to `_invalid-<hash>`), `isSafeExtension`
  (`mediaFilePath`: else `file.bin`), `isSafeRelative` + `isInside` for archive and package paths (`ZipReader::extractAll`,
  `Addons`), `removeTreeInside` for every recursive delete of a project's folder (a link is removed, not followed).
  `ZipReader::fileData(name, &ok)`: CRC, exact size, bounds; a damaged entry is never written. `Cfb.cpp`: DIFAT loop and
  sectors outside the file. `MiniXml`: depth limit 1000.
- **Settings a project may hold**: `settingslayer::programOnly` is an allow list (see `SettingsLayer.cpp`);
  `namesProgramOrPlace` keys are never a project's and Load Options asks (`loadOptionsPlaces`).
- **Links** (`src/ui/LinkOpen.*`): `links::open` (http/https/mailto at once, `linkQuestion` for other targets,
  `linkRefused` for program files: `isProgramFile`, last ending or an executable with "#!", ELF, "MZ"), `links::openFile`
  for a project's file in another program; `setOpenerForTests`. Used by `MainWindow::openLink`, mind maps, bookmarks
  (both lists and the menu), `MediaView` (the web page's `QTextBrowser` has `openLinks` off and goes through it).
- **Scrivener's lock** (`scrivener::scrivenerLockStamp`): recorded in `Project::scrivenerLockAtOpen` by `scrivener::open`;
  `sync` refuses to write while another one is there (`scrivenerLockRefused`, `scrivenerLockWho`; the window's
  `saveFailed` handler shows `scrivenerLockNotice` once per lock, `closeProject` asks with its own text).
- **The computer's name** (`AppSettings::deviceName`, `app/deviceName`, `optDeviceName` on General > Saving,
  `syncsafe::setDeviceName`): replaces the host name and leaves `user=` empty; locks carry `key=` (`syncsafe::machineKey`,
  a hash of the machine id; `judge` compares it when both have one). `QB_DEVICE_NAME` (tests) still wins.
- **Test switches** (`QB_TEST_SWITCHES`, CMake option, default = `QB_BUILD_TESTS`): `QB_PLUGIN_PATH`, `QB_SPEECH_SOURCE`,
  `QB_SPEECH_ACCEPT_CHANGED`, `QB_SPEECH_SKIP_TEST` work only in builds with the tests; packages are built with
  `-DQB_BUILD_TESTS=OFF` (build-appimage, build-update). The Windows runner builds with the tests (switches on there).
- **Speech models**: `CatalogEntry::sha256` (Hugging Face's Git LFS ids, 2026-11-09), `sumsMatch`, `catalogEntryBySums`,
  `addressAllowed` (https, http only on loopback); `SpeechTool::checkAndInstall` hashes first (official source: must
  match; otherwise the question before reading), then `checkModelFile` (description, test transcription).
- **Socket folder**: `toolhost::Host::linkDirSafe` (lstat: a folder of this user, closed to others), else
  `~/.cache/quillbinder/link`.
- **Pipeline**: `packaging/build-appimage.sh` pins linuxdeploy 1-alpha-20251107-1, linuxdeploy-plugin-qt 1-alpha-20250213-1,
  appimagetool 1.9.1 and type2-runtime 20251108 with SHA-256 (the 0.64.0 AppImage had the "continuous" runtime). The
  release workflow: `contents: read` for all, `publish` alone writes; actions pinned to commits (tags in comments).
- Not changed (and why): the comments that name Scrivener's windows (L-3) describe where a layout came from; they stay
  until the layouts were compared and reviewed by a lawyer (a comment changed alone would hide it, not change it).

## Fixes of the system test's first run (0.64.1)

- **Saving** (`src/core/SafeSave.h`, `safesave::commit(QSaveFile&)`): every QSaveFile of project data, settings files,
  compile output and the like is written with `safesave::write` (a short write, which Qt reports as no error, cancels
  the file) and committed through it (flush and `error()` checked, `fsync` checked, a `dup` of the descriptor closed
  and checked, then `commit()`): Qt 6.4's `commit()` ignores a failed last write and a failed sync (its own sync runs
  after the close), and its temporary file engine closes the file only after the rename, unchecked.
  `backup::backupProject` stops when its save fails. A failed docs.checksum write fails the Scrivener sync.
  Checked with `tests/faultshim.cpp` (LD_PRELOAD library `qbfaultshim`: `QB_SHIM_DIR`, `QB_SHIM_MODE=write|fsync|close`) in
  `qb_audit1_tests`' child runs (`--child <folder> save|backup:<dir>`).

- **Shown, not edited** (`DocumentStore::Entry::read` / `readRtf`: the bytes as read or last written): when the file
  changed elsewhere, `writeEntry` asks `unchangedSinceRead` (the read bytes read again and serialized the same way equal
  what would be written) and then writes nothing (showing a text emits `contentsChanged`, which marks it dirty).
- **Keep or do not write**: `DocumentStore::keepOtherVersion` returns false when neither a copy nor a snapshot was kept;
  `writeEntry` then fails with an error and leaves the entry dirty. The same for the project file (`Project::saveIndex`),
  and in Scrivener format (`keepTheirs` -> `mustWrite` false, `.scrivx` not written, `WriteReport::keepFailed`, `sync`
  returns false with the error after saving the state). `syncsafe::keepCopy` returns "" when `mkpath` fails too. Tests
  block `settings/conflicts` and `snapshots/<id>` with a plain file (the session's root ignores permissions).
- **Table cells**: `props::InTableCell` (block, never saved) is set on every cell paragraph by `tables::apply`;
  `display::blockFormatFor` keeps it and gives a cell paragraph left instead of the justified body alignment (unless set
  by hand or by its style). Compile: `PdfExporter`'s `cellParagraph`, `DocxWriter` (`w:jc left` in cells), the e-book CSS.
  Markdown: `cellParagraphs()` splits at " <p/> " also at a trimmed edge; the writer takes every block of a cell (also of
  a table or figure inside it). RTF (`RtfIO.cpp`, `table()`): a frame inside a cell is written as its paragraphs too.
- **Deleted for good**: `BinderModel::itemsAboutToBeDeleted` (from `deletePermanently`); the window clears every pane
  and copyholder showing one (S-T06 emptied the Trash past the window; a pane kept a deleted document and crashed at
  close).
- **Split at Selection** (`MainWindow::splitDocument`) refuses inside a frame (table, figure) and never lets its two
  clean-up steps select into one (Qt widens such a selection to the whole frame).
- **Compile progress**: `compile/Progress.*` (`progress::tick`, `progress::Scope`); `Compiler::build` and the built-in
  exporters tick in their loops; `CompileDialog`'s `CompileProgress` shows a modal `QProgressDialog` and runs
  `processEvents(ExcludeUserInputEvents)` at most every 100 ms, so `CrashGuard`'s heartbeat timer keeps beating.
  `PdfExporter`'s `Builder::build` makes and fills each table inside one edit block (`tableOpen`; laid out once, not per
  cell: 27 s to 0.4 s for the v0641 table); one edit block over the whole build left the last rows of v062's long table
  out of the pages (cause not found).
- **Copy**: `TextEditor::createMimeDataFromSelection` copies the pictures' resources into the temporary document, so the
  RTF on the clipboard carries them. **Merge** joins the notes too (same separator as the text).

## Synced folders, Record only, autosave in typing gaps (0.64)

- **Sync safety** (`src/core/SyncSafety.*`, Qt Core; `src/ui/MainWindowSync.cpp`): works on files only, whatever the
  sync program. The lock (`settings/lock`; for Scrivener format also `<x>.scriv/quillbinder.lock`): first line
  "<host> <pid>" as before (older versions read it), then `device=`, `user=`, `instance=` (one per running program),
  `program=`, `opened=`, `alive=` (UTC); `MainWindow::takeLocks` at adoption, renewed every 5 min (`QB_LOCK_RENEW_MS`),
  checked after every save (`Project::saved` -> `renewLocks(false)`): another computer's fresh lock -> notice
  `lockTakenNotice`, no more renewals; `releaseLocks` removes only our own lock. `confirmLock` before opening:
  `judge()` -> other window / other device (renewed within 15 min, `QB_LOCK_STALE_S`) / left over (Open is the
  default). `QB_DEVICE_NAME` fakes the computer's name (tests). No read-only mode for a whole project.
- **Stamps** (size + time): `DocumentStore::Entry::stamp` (set in `load()`, after each `writeEntry`), `Project::m_indexMark`
  (the project file), `SyncState::theirs[".scrivx"]` ("size:time"). A changed stamp before a write = someone else wrote
  the file: `DocumentStore::keepOtherVersion` / `syncsafe::keepCopy` keep it (copy in `settings/conflicts`, for a text
  also a snapshot "Other version (changed elsewhere, ...)") and `Project::conflictKept` -> `conflictNotice`. Scrivener
  format: `mustWrite` keeps theirs when both sides changed (before: ours won silently).
- **Nothing written without a change**: the UI state goes to `settings/ui-<computer>.json` (`Project::uiStateFile`,
  `markUiStateDirty`; `ui.json` of older versions is still read), `saveUiState` no longer marks the project file dirty;
  the spare copy only when it differs; the .scrivx only when more than its `Modified`/`ModID` differs
  (`sameExceptStamps`), docs.checksum only when it differs.
- **Texts not arrived**: a text with `charCount > 0` in the project file and no file -> `DocumentStore::m_missing` (+
  `m_unreadable`: read-only, never written, not recounted), `missingFile` signal; `textsNotArrived()` -> notice
  `notArrivedNotice` after opening. Not for Scrivener format (the working copy is local).
- **Autosave** (`Project::scheduleSave`): the 2 s pause timer as before; once the changes are 5 s old
  (`m_gapSaveAfter`, or the delay when longer) every change restarts `m_gapTimer` (300 ms): the first gap saves;
  `m_maxSaveWait` 30 s stays the latest. `autosaveCount()` for the tests. Replaces PR #35 (flat 5 s).
- **Speech, Record only** (setting `mode` = `record` / `live`; standard `record` only on Android): `EngineSettings::recordOnly`
  + `recordFile` -> `Engine::startRecordOnly` (no model, no Segmenter; `speech::WavWriter` 16 kHz mono 16 bit, header
  updated every second). `speech::transcriptsIn` lists `.wav` without `.txt` as pending (`Transcript::audio`,
  `pending()`). `SpeechTool::transcribeRecording` runs the WAV with `fileAsFast` (fed as fast as the worker takes it:
  at most 2 pieces waiting, the feed timer stops meanwhile) into `<name>.txt.part`, renamed at the end
  (`finishRecordingText`; stopped: the part file goes, the recording stays pending); a queue for Transcribe All;
  `deleteRecording` moves the WAV to the trash afterwards. Panel: item widget with `speechTranscribeNow`,
  `speechTranscribeAll`; rename and delete take the WAV along. States "recording", "reading <percent>".
- **Spelling menu**: `SpellChecker::checkedWordAt` (the highlighter's regex and rules) gives the menu its word at the
  click (Qt's WordUnderCursor ends at a straight apostrophe).
- **Card size slider**: saved on every change unless the handle is held, follows `AppSettings::changed`.

## Settings per project, first start, tables and pictures (0.62)

- **Settings layer** (`src/core/SettingsLayer.*`): every `QSettings` of the program reads a merged copy (the program's
  INI plus the project's `settings/options.ini`; QSettings' default path is pointed at it with `setPath`), and
  `settingslayer::sync()` (called by `AppSettings::save` and at exit) sends each change to the project file when the
  project holds that key, else to the program file. `programOnly()` lists the keys that never go to a project (recent
  projects, window/, layouts/, the look for new projects, crash watchdog, the switch itself ...): add a key there when
  a new setting must stay program-wide. `activate()` from `MainWindow::adoptProject` / `closeProject`; the switch
  `projectFirst` (General > Startup, `optProjectFirst`) off means the layer never merges a project, and
  `look::setProgramSettingsAlways` makes every project show the look for new projects (`look::setFollowSettings` stays the hook of the tests written before 0.40.2). File > Options in "This project"
  scope (`OptionsDialog::run`, `optScope`): `beginProjectEdit` keeps the values the dialog started from,
  `takeProjectValues` stores in the project what was changed (and what the project held); in "All projects" scope
  with a project open the layer is suspended while the dialog runs. Defaults and Load Options are off in project scope.
  Settings files: section `Options` (`sf::Options`).
- **First start** (`src/ui/FirstStartDialog.*`, from `main.cpp` before the desktop question): backups and the switch;
  `app/firstStartDone` in the program file. Skipped with `QB_NO_DIALOGS` or `QB_SKIP_FIRST_START` (the test runner sets it);
  `tools/e2e-appimage.sh` answers it and checks it came.
- **Tables and figures in the text** (`src/text/Tables.*`, see the comment at its top): `tables::Spec` as JSON in
  `props::TableSpec`, `tables::apply` after every change; figures are `QTextFrame`s with `props::Figure`; the description
  is `props::ImageDescription`. Qt keeps a block before and after every frame; Qt 6.4 crashes when rows or columns under a
  merged cell are removed, so `TextEditorTables.cpp` splits those cells first. Storage: Markdown `:::table {json}` rows
  `:::` and `:::figure {json}` ... `:::` (`MarkdownIO.cpp`), RTF `\trowd ... \row` with `{\*\qbtable json}`,
  `{\*\qbfigure json}`, `{\*\qbfigureend}`, `{\*\qbalt text}` (`RtfIO.cpp`). `DocumentStore::writeEntry` sets
  `Project::needsVersion` to 0.62.0 when a document has a frame. Dialogs: `src/ui/TableDialogs.*`; commands `insertTable`,
  `table*`, `picture` (Format > Table, right-click menu).
- **Compile** (exporter interface 1.4, `ExporterApi.h`): `Compiler::appendDocument` fills `Manuscript::tables` and the
  paragraphs' `table`, `cell`, `figure`, `figureCaption`, `Run::imageDescription`. PDF: tables and figures are rebuilt as
  frames of the layout document and placed as regions (`Region`, painted with `drawContents` clipped), a table in row bands
  with its header rows again on a new page; floats do not move `cur`, and something on its own lines after a float starts
  below it. Frame measures (borders, padding, margins, fixed widths) are given in 96-dpi pixels: Qt scales them to the
  1200-dpi layout device. Word: `w:tbl` (floating with `w:tblpPr`), a figure beside the text as a borderless floating table
  of one cell with its caption, `descr` on `wp:docPr`. EPUB: `<table class="qbtable">` and `<figure class="qbfigure">`.
  Markdown (the plugin and `writeMarkdown` for Pandoc): pipe tables.

## Quote conversion, Delete Old Snapshots (0.61.1)

- `autocorrect::convertQuotes(text, typographic, style)` (pure, per paragraph) gives the positions and new characters;
  `replace::convertQuotes` runs it through the project with Project Replace's engine: `replace::run(p, where, finder,
  mayChange, ...)` takes any `ChangeFinder` (the search's finder is one); the old regex functions are wrappers.
  `ConvertQuotesDialog` (`SearchTools.*`) is opened by `MainWindow::convertQuotes` from the buttons
  `optConvertQuotesSmart` / `optConvertQuotesStraight` on File > Options > Corrections (the Options dialog's parent
  must be the main window); afterwards `MainWindow::afterProjectWideChange` (shared with Project Replace) refreshes and saves.
- `snapshots::totals / removeAll` work on the whole `snapshots/` folder (also folders of documents no longer there);
  the action `deleteSnapshots` (`MainWindow::deleteOldSnapshots`, `updateSnapshotAction` on the Project menu's
  `aboutToShow` and in `updateActions`) asks with `deleteSnapshotsQuestion` and saves in full at once.
- Scrivener format: the full write (`scrivener::write`, not quick) takes snapshots out of the .scriv whose keys are in
  `SyncState::snapshots` but no longer among the document's snapshots; files and index.xml entries are matched by
  their moment (`snapshotMoment`, with the time zone of the name; a key's name is ours, the .scriv's file may carry
  another offset). Snapshots the state does not know (Scrivener's own, never read) stay.


## Speed with large projects (0.60.5)

- **Autosave vs. full save**: `Project::autosave()` (the timers of `scheduleSave`, and `MainWindow::changeEvent` when
  another window of the program becomes active) runs `saveImpl(quick = true)`; `save()` (switching to another program,
  File > Save, closing, every other caller) is the full save as before.
- **Quick Scrivener write** (`scrivener::sync(p, err, quick)` / `write(..., quick)`): texts, notes, synopses that changed
  (our file's SHA-1 comes from `knownSha1File`, read again only when size or time changed; Scrivener's file is read only
  when ours changed), docs.checksum, and the .scrivx only when `Project::binderChangedSinceSync` (set by `markIndexDirty`,
  cleared by a write of the project's own .scriv). Snapshots, icons, media, mind maps, removed documents and
  `Files/search.indexes` wait for the next full write; `SyncState::indexStale` (in scrivener-sync.json) makes the full write
  write the index even when it finds nothing new. The fingerprint after a quick write comes from `FingerprintCache` (filled
  by every `fingerprint()` call, lines of the files written updated); files changed elsewhere are not in it, so the next
  opening reads the .scriv again (the safe way round). A quick write without a .scrivx is a full one.
- **Counts**: `DocumentStore::recount` calls `Project::notifyItemCounted` (modified date, `m_countsDirty`, `itemChanged`),
  not `notifyItemChanged`: the project file is written with the next full save or a minute after its last write
  (`m_lastIndexWrite`); `hasUnsavedChanges()` does not count it.
- **Timing**: `QB_TIMING=1` prints `timing: ...` lines (`src/core/Timing.h`, `timing::Laps`) for the save steps, the
  Scrivener sync and write, and the editor's right-click menu.
- **Menus**: `noUiAnimations()` in `MainWindow.cpp` (constructor, and on `QEvent::ThemeChange` in `InputWatch`) switches
  off Qt's menu, combo, tooltip and tool box effects. `SpellChecker::suggestions` keeps the words looked up
  (`s_suggested`, emptied by `setLanguage`, `addToPersonal`, `setPersonalWords`, and above 500 words).
- Measured on the cloud machine (2 cores) with the v0605 project: autosave 250 to 350 ms before, 30 to 40 ms after; the
  right-click menu about 10 ms (60 ms the first time on a misspelled word: Hunspell's suggestions). Not done: the
  Combined Text view of a large folder (one editor per document, full layout and spell check of every document) and
  unloading documents that are no longer shown.

## Paragraph buttons of the format bar (0.60.4)

- Specials `spaceBefore`, `spaceAfter`, `indentFirst`, `indentLeft` (`toolbars::specials(Bar::Format)`, standard set after
  `numbers`): `QToolButton`s `para_<id>` with the menus `paraMenu_<id>`, filled in `MainWindow::fillParagraphMenu` (values in pt for
  spacing, `units::` for indents, the value at the cursor ticked, `paraOther` asks with `QInputDialog::getDouble`); they apply a
  `direct::ParaSpec` with one bit through `TextEditor::applyParaSpec` (selected paragraphs, one undo step). Enabled with the
  other format controls in `updateFormatBar`. Icons drawn in code (`icons::kSpaceBefore` ... , `ParagraphEngine` in
  `ItemIcons.cpp`). `AppSettings::load` adds them once to a format bar the writer arranged (key `toolbars/paraButtons0604`).

## Folder totals in binder and footers, Inspector Background (0.60.3)

- **Binder tool tip** (`BinderModel::data`, `Qt::ToolTipRole`): a container shows `totalWordCount()` and, with text of its
  own and children, "(its own text: ...)".
- **Footers**: `folderCount`, `folderCountCorkboard`, `folderCountOutliner` are `QToolButton`s; a click runs
  `EditorPane::showCountDetails(button, folderCountDetails())` (texts counted again with `text::count`; the document count
  uses the same popup, `countLines`). Corkboard and outliner footers use `centeredFooter`: two side widgets with stretch 1
  and the total between them, so it stays in the middle.
- **Colors**: `theme::Role::InspectorFields` (key `inspectorFields`, label "Inspector Background", `Area::General`, defaults
  to `base`; `NotesPaper` defaults to it). `Inspector::applyTheme` puts it into the inspector's Base / AlternateBase with
  `theme::readableOn` as Text; it does not count as a change of the window palette in `theme::apply()`. `base` is no
  longer offered (General Interface lists `inspectorFields` instead); `AppSettings::load` moves an own `base` to
  `inspectorFields`. `InspectorBackground` (the strips) is labelled "Inspector Strips". `theme::panelFields(w)` gives a
  widget Base = Window and Text = WindowText (only those roles; refreshed on `Notifier::changed`): the three main toolbars
  and the option dialog's `optionPages`, sub-page lists and `appearanceAreas`. The ruler draws the column as a frame on
  Window. `lookmenu::fill` adds `lookColor_inspectorFields` for `Part::Inspector` (writes `AppSettings::ownColors`).
  `tests/v0402_tests.cpp` leaves `inspectorFields` out of the theme digest; `tests/v0603_tests.cpp` checks it equals `base`.
- **Double-click beside the text**: `TextColumnView::besideTextDoubleClicked` -> `EditorPane::revealRequested` -> the action
  `reveal` (Reveal in Binder) for that pane; `BinderPanel::flash` blinks the row twice (widget `binderFlash` over the row,
  `flashStep`, 220 ms). A pane has a second, hidden `TextColumnView` (the copyholder): tests pick the visible one.
- **Font names that are not families** (`fontfaces::resolve / substituteUnknown`, `src/text/FontFaces.cpp`): face and
  PostScript names ("SitkaTextItalic", "Georgia-BoldItalic") are matched to an installed family (style words and "MT"/"PS"
  taken off the end, spaces and case ignored, then shorter CamelCase stems) and given to Qt as a font substitution
  (`QFont::insertSubstitution`), so the text keeps the name. Called for RTF font tables (the RTF reader also takes italic
  and bold from the name) and for `font-family` in Markdown spans; results are cached until the fonts change.

## Paste with formatting, invisibles, folder totals, styles over paragraphs (0.60.1)

- **Edit > Paste with Formatting** (`pasteKeep`, Ctrl+Alt+Shift+V): `TextEditor::pasteKeepingFormatting()` sets
  `m_pasteKeepAll` around `paste()`; `insertFromMimeData` then reads RTF with `keepFormatting` and HTML with fonts, and
  `insertSemantic(..., keepParagraphs)` gives each pasted paragraph `direct::withParaSpec(effectiveParaSpec(source))` (the
  first one only when it lands in an empty paragraph). Plain Paste is unchanged. 0.60.2: `TextEditor::contextMenuEvent` puts
  `ctx-pastePlain` (`pasteMatchingStyle()`, also behind Edit > Paste and Match Style) and `ctx-pasteKeep` right after Qt's
  `edit-paste` action, with the shortcuts of `commands::shortcutOf`.
- **Show Invisibles** (`showInvisibles`, `AppSettings::showInvisibles`, toolbar standard set, View button menu):
  `TextEditor::paintInvisibles` draws over the text from the block layouts (not in notes editors); the document's
  text option is never touched, so prints, compile and mirrors stay clean. `blockBoundingRect(b).topLeft()` is the
  layout origin (Qt moves the rect there). The toolbar icon is drawn in code (`icons::kShowInvisibles`, `PilcrowEngine`
  in `ItemIcons.cpp`): neither Breeze nor the bundled set has a pilcrow.
- **Folder totals**: `BinderItem::totalWordCount / totalCharCount`; `EditorPane::folderCountText()` (labels
  `folderCount`, `folderCountCorkboard`, `folderCountOutliner`; refreshed in `updateCounts` and `refresh`), the
  inspector's `inspectorFolderCount` row (`Inspector::refreshTotal`, on `countsChanged` and settings changes). Counts
  follow edits through `DocumentStore`'s 600 ms count timer, not through saving.
- **Styles over paragraphs**: `display::applyParagraphStyle(..., StyleOver::Paragraphs)` (used by
  `TextEditor::setParagraphStyle`, i.e. every style command) clears the char spec set by hand (family, size, weight
  other than bold, color, letter spacing, capitals) before the style; bold, italic, underline, strikethrough,
  highlights and character styles stay; a selection ending at a paragraph's start leaves it out. Paste Formatting and
  script elements keep `StyleOver::KeepOwn`.

## Titles, format bar, style list, alignment (0.40.3)

- **Titles from the first line** (`src/ui/MainWindowTitles.cpp`): a text item titled "" or "Untitled" (`text::isUntitled`)
  gets `text::firstLineTitle` (first line with words, no inline notes or pictures, at most 60 characters) through
  `DocumentStore::textEdited` and a 400 ms timer; `BinderItem::autoTitle` (attribute `autoTitle` in the project file) keeps
  it following; `BinderModel::setData` (every rename by hand) clears it. `queueUntitledDocuments` at `adoptProject` names
  older "Untitled" documents that have text (by the counts kept in the project file).
- **Format bar = what is seen**: `TextEditor::barState()` (toggles, super/subscript, font, size, alignment, line spacing over
  the whole selection; empty when mixed) and `paragraphStylesMixed()`; `toggleState(true)` (by hand) is no longer what the
  bar shows. Not bold / not italic inside a bold / italic style: `props::NotBold` / `NotItalic` (set by `setBold(false)`,
  honored by `styles::applyChars`), in Markdown a span `font-weight:normal` / `font-style:normal`.
- **Alignment of the main text**: `Formatting::align` (was `bool justify`; project file and settings files write `align` and
  still `justify` for older versions; `AppSettings::newAlign`, key `newAlign` plus `newJustify`); `ui/AlignButtons.*`.
  Compile's exporter interface still has `justify` only: centered or right body text comes through the paragraphs.
- **Style list** (`src/ui/StyleList.*`): `Project::styleListOrder / styleListHidden` (element `StyleList` in the project file,
  `list` / `hidden` in the styles section of settings files), `stylelist::addTo` (plain entries up to ten, else a scrolling
  `QListWidget` in a `QWidgetAction`), `StyleCombo` (the format bar's combo box with this popup; its items hold all styles so
  it can name a hidden one), `StyleListDialog`. `MainWindow::fillStyleList` builds the popup and the right-click Style menu.
- **Styles panel**: dialogs started from it get `styleUiParent()` (the panel) as parent; No Style's Redefine From Selection is
  `MainWindow::redefineNoStyleFromSelection` (project formatting + `formatchange::followProject`, own formatting too).
- **Spelling menu**: on a misspelled word `TextEditor::contextMenuEvent` puts everything else into the submenu `spellingMore`.

## The look per project, settings files, style changes, no jump (0.40.2)

- **Look per project** (`src/ui/ProjectLook.*`, `look::`): theme, own colors and fonts of the areas (everything but
  `theme::Area::General` and the interface font), text column width and the border beside the text are stored in
  `settings/look.json` of the project. `MainWindow::adoptProject(p, isNew)` calls `look::settle` (a project without the
  file gets the look for new projects = `AppSettings`; an existing one without a border) and `look::activate`, which
  hands it to `theme::setAreaLook` before `theme::apply()`. `AppSettings::theme / ownColors / ownFonts / editorWidth /
  textBorder` are the look for **new projects** (and the program's menus and panels). Never change a built-in theme's
  colors in an update: `tests/v0402_tests.cpp` keeps a digest of them; a new look goes into a new theme or into the
  look for new projects. File > Options > Appearance has `optLookScope` (this project / new projects); Window > Themes
  (`themeui::choose`) sets both.
- **Right-click "Appearance"** (`src/ui/LookMenu.*`, `lookmenu::add / popup`, parts Editor, HeaderBar, Binder,
  Corkboard, Outliner, Inspector, Composition): changes go through `look::change` (the open project's look).
- **Settings files** (`src/core/SettingsFile.*`: sections, capture by names, compare, `applyData`;
  `src/ui/SettingsTransfer.*`: the per-section window; `src/ui/MainWindowSettings.cpp`: Project > Save / Load Project
  Settings). What documents use is never dropped; the settings before loading are kept in
  `settings/settings-before-loading-<date>.qbsettings`.
- **Style changes** reach all text of the style: `direct::takeStyle` (members and paragraph bits that changed, also
  where set by hand), called from `MainWindow::commitStyles` through `formatchange::takeStyle` (snapshot first).
- **No jump on click / right-click**: `TextEditor::cursorMovedByMouse()` makes `TextColumnView::ensureCursorVisible`
  scroll only when the cursor is out of sight; non-left presses are accepted in the editor so they do not reach the
  paper's click handler; `TextColumnView::clickBesideText` puts the cursor on the line beside a click.

## Fonts with several designs, the format buttons (0.40.1)

- `src/text/FontFaces.*`: `fontfaces::install()` (called by `main.cpp` right after the application object; again
  when Qt reads its fonts anew) gives each design of a family that holds several (optical sizes such as Windows 11's
  Sitka, widths) a family of its own in Qt's font list ("Sitka Text" with only the Text faces), so that Bold and
  Italic stay within the design. It works on Qt's private `QFontDatabasePrivate` (families, foundries, styles;
  handles are `FontFile` = file + fontconfig index) and registers faces with `QPlatformFontDatabase::registerFont`.
  The faces of a variable font are read from the file's `fvar`/`name`/`OS/2` tables (read only), because Qt 6.4 files
  the font as a whole under a named face, or folds the faces of one weight into it, depending on fontconfig's order.
  The font files are never changed or copied (Windows fonts may not be modified or converted). `QB_KEEP_FONT_FAMILIES=1`
  switches it off. A new Qt version needs a look at this file (0.80.0, Qt 6.12: `registerFont` takes `colorFont`,
  taken from the family; the separation works as on 6.4, `qb_v0401_tests` passes).
- The Bold / Italic / Underline / Strikethrough buttons and commands work on the whole selection:
  `toggleStateOf` / `TextEditor::toggleState(byHand)` and `setBold` ... in `src/ui/TextEditor.*`; also
  `ObjectTextEdit::isOn` and the fonts window. `TextColumnView` refreshes the format bar on selection changes
  (only for a selection there is); `MainWindow`'s destructor disconnects its panes first.
- `tests/v0401_tests.cpp` uses the free Source Serif 4 variable font in `tests/fonts` (OFL, unchanged; through a
  fontconfig file of the test's own), which is built like Sitka.
## Release, sources, media formats (0.50)

- Project page: `APP_HOMEPAGE` in CMakeLists.txt (`QB_APP_HOMEPAGE`, `QB_SOURCES_URL` in AppInfo.h). The release
  `third-party-sources` there holds one `.src.tar` per third-party part (`tools/collect-sources.py`); every entry of
  `resources/licenses/index.json` names its file (`sourceFile`, `source`), and the add-ons' `stage.sh` write license
  notes that name theirs. `tools/collect-licenses.py --refresh` (run by `build-appimage.sh`) brings the list to the
  package versions of the build system and the runtime's commit; `--check` fails when the AppDir differs.
- Media: `tools/build-ffmpeg.sh <src> <prefix> base|full`; `tools/get-ffmpeg.sh` builds both (`build/ffmpeg`,
  `build/ffmpeg-full`). "Media Playback" 1.1 carries "base" (free formats only, list in the script); the add-on
  `media-extra-formats` ("Extra Video and Audio Formats") carries "full" in `media-extra/lib`, which
  `MediaEngine::preload` loads instead when it is installed and not disabled (`<addons>/state.json`). The player
  (`impl/QtPlayer.cpp`) looks at a file's streams with FFmpeg first and shows which formats need the extra add-on.
  `qb_m11_tests` installs both add-ons; `qb_v050_tests` checks the split (child runs: base, extra, off, removed).
- Releases are built and published by `.github/workflows/release.yml` in the public repository only (the job
  `version` has `if: github.repository == 'Shelin-boop/Quillbinder_Public'`).

## Publishing, legal check (0.60)

- `README.md` starts with direct download links of the current version (`releases/download/v<version>-<label>/...`
  for the AppImage and `media-all-formats.qbpkg`): change them with every new version; `tools/publish-public.sh`
  refuses to publish otherwise. Releases are full releases marked "Latest" (`tools/publish-release.sh`).
- **Full version** (0.60): `packaging/build-full-appimage.sh [AppImage build dir] [add-on packages dir]` makes
  `<Name>-<version>-full-<label>-x86_64.AppImage`: the AppImage extracted, every `addons/*` package in
  `usr/share/quillbinder/full-addons/`, and a start script (`AppRun`) that runs `quillbinder --install` for each at the
  first start of that version (marker `~/.local/share/Quillbinder/full-addons-installed-<version>`), then the program.
  No program change; the release workflow builds and checks it, and builds a release again when it lacks one.
- `tools/publish-public.sh` (see "One source" above) and `tools/public-exclude.txt`. Never publish the history of
  the development repository: it holds texts that were taken out (the old tutorial, a pen name).
- `tools/legal-check.py [--public] [tree]`: LICENSE is the Apache License 2.0 and NOTICE names the project and its
  source, no person (0.80.0; 0BSD before); README, AppStream text, About and the
  release text say Quillbinder is not made by or connected with Literature & Latte; no wording of a copy, a
  replacement or a link with Scrivener ("Scrivenings" only as the old settings keys); nothing of the old tutorial or
  the pen name; no Scrivener or Scapple files; every picture, sound, video, font and document file with a note on
  where it comes from (README / LICENSE / COPYING in its folder or above); fonts with their license beside them;
  FFmpeg without GPL or non-free parts and named with its LGPL in About and the add-ons; with `--public` no private
  files or blocks. `qb_v060_tests` runs it and the publish script on a test clone.
- Texts and pictures of Scrivener, Scapple and their manuals, templates and tutorials are never copied, not even as
  a list of fields or a layout taken from a screenshot; describe and design in words and choices of your own. The
  built-in character and place sheets were written anew in 0.60 for this reason.

## Welcome project, licenses, names (0.40)

- The welcome project is an outline (`resources/welcome/outline.json` with Markdown files in `docs/`), made into a
  project by `src/core/OutlineProject.*` (also used by `tools/qb-template.cpp`; the outline can carry a mind map per
  item and objects). `MainWindow::openWelcomeOnFirstStart` (called by `main.cpp` when no project file was given)
  opens it once (`AppSettings::welcomeShown`); **Help > Welcome Project** opens it again or makes a fresh copy.
  Keep its texts short; `tests/v040_tests.cpp` checks every menu place they name. After changing files in
  `resources/welcome` or `resources/licenses`, run `python3 tools/gen-resources.py`.
- `docs/quick-start.md` is **Help > Quick Start Guide**; keep it in step with the manual.
- **Help > Third-Party Licenses** (`src/ui/LicensesDialog.*`) shows `resources/licenses` (made by
  `tools/collect-licenses.py` from the Ubuntu packages of the AppImage's libraries; `tools/licenses-extra.json` for
  the parts that are no Ubuntu package). `packaging/build-appimage.sh` fails when a library has no license text.
- The license is the Apache License 2.0 since 0.80.0 (`LICENSE` unchanged from Apache, `NOTICE` with "Copyright 2026
  The Quillbinder project" and the link to the public source; no person named; 0BSD up to 0.70.0). Credits:
  "Main Developer: Claude.AI w. Opus 5.5 Extra" only.
- The view of several documents as one text is called **Combined Text** (`ViewMode::Combined`); the settings keys
  of older versions (`scriveningsTitles`, font `scriveningsTitle`) are still read. Do not use the word
  "Scrivenings" in the program or its texts; "Scrivener" only where the file format or the import is meant.
