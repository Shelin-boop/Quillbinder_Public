# Quillbinder: notes for Claude Code

Quillbinder is a writing program for Linux (binder, editor, corkboard, outliner,
inspector, compile), for writers who know Scrivener; it reads and writes Scrivener's
file formats but is an independent program (0BSD license). C++17 and Qt 6.4.2 as
shipped by Ubuntu 24.04. Never copy text, pictures or code of Scrivener, Scapple or
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
- **Runtime**: the current AppImage is 0.63.0 alpha (before it 0.61.0 alpha, 0.60.0 alpha, 0.40.0 alpha / 0.50.0 alpha, 0.30.0 alpha, 0.20.0,
  0.16.0 and 0.2.0, with the same libraries), runtime id `u2404-qt6.4-r1` (`APP_RUNTIME` in CMakeLists.txt; a new
  AppImage with other libraries needs a new id). Program updates (.qbpkg) run on its libraries:
  build on Ubuntu 24.04 with Ubuntu's Qt 6.4.2, use no Qt API newer than 6.4, and add
  **no new library dependencies**. Something that needs a library goes into an add-on
  (`addons/`, see the Media Playback add-on with its own FFmpeg).
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

    tools/cloud-setup.sh                  # once on a fresh Ubuntu 24.04 (as root)
    cmake -S . -B build -G Ninja
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
  writerslogic/scrivener-mcp, tests/sample-project.scriv).
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
  Italic stay within the design. It works on Qt 6.4.2's private `QFontDatabasePrivate` (families, foundries, styles;
  handles are `FontFile` = file + fontconfig index) and registers faces with `QPlatformFontDatabase::registerFont`.
  The faces of a variable font are read from the file's `fvar`/`name`/`OS/2` tables (read only), because Qt 6.4 files
  the font as a whole under a named face, or folds the faces of one weight into it, depending on fontconfig's order.
  The font files are never changed or copied (Windows fonts may not be modified or converted). `QB_KEEP_FONT_FAMILIES=1`
  switches it off. A new Qt version needs a look at this file (the AppImage's Qt is fixed at 6.4.2).
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
- `tools/legal-check.py [--public] [tree]`: LICENSE is 0BSD without a person; README, AppStream text, About and the
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
- The license is 0BSD (`LICENSE`, no person named). Credits: "Main Developer: Claude.AI w. Opus 5.5 Extra" only.
- The view of several documents as one text is called **Combined Text** (`ViewMode::Combined`); the settings keys
  of older versions (`scriveningsTitles`, font `scriveningsTitle`) are still read. Do not use the word
  "Scrivenings" in the program or its texts; "Scrivener" only where the file format or the import is meant.
