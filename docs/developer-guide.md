# Developer Guide: Updates and Add-ons

This guide is for people who build {{APP}} or write extensions for it. It explains how program updates and add-ons work, how they are made and checked, and which tools in the source tree do the work. Writers do not need it: they install packages with **File > Install Update or Add-on** (manual, section 17).

This copy belongs to program version {{VERSION}}, runtime `{{RUNTIME}}`. The source tree has the same text in `docs/developer-guide.md`; what each version brought is in `docs/CHANGES.md`.

---

## 1. The idea in short

{{APP}} is delivered as an **AppImage**: one read-only file that holds the program and the libraries it needs (Qt 6.4.2 of Ubuntu 24.04 and a few more). The user runs the AppImage 0.30.0 alpha (2026-10-25; before it 0.20.0, 0.16.0 and 0.2.0, which have the same libraries). A new AppImage is rare; everything else comes as small **package files** ending in `.qbpkg`:

- a **program update** (kind `core`) is a newer program binary, without libraries. It runs on the libraries inside the user's AppImage;
- an **add-on** (kind `addon`) is content (themes, templates, compile formats, dictionaries, layouts, script formats) and, if needed, plugins and libraries of its own;
- a **bundle** (kind `bundle`) packs several packages that are installed in one step.

Nothing is downloaded by the program: the user installs a package file (offline install). Packages carry SHA-256 sums of their files; they are not signed (decision of 2026-10-03).

---

## 2. The package file

A `.qbpkg` is a ZIP archive:

    manifest.json          what the package is
    payload/...            the files (add-ons and program updates)
    packages/*.qbpkg       only in bundles: the packages inside

`manifest.json` (see `src/core/Addons.h`, struct `Manifest`):

| Key | Meaning |
|-----|---------|
| `format` | 1 |
| `id` | lower case, e.g. `themes-extra`; program updates use `core` |
| `name`, `version`, `description`, `author` | shown in the install window and the Add-on Manager; versions are `1.2` or `1.2.3` |
| `kind` | `addon`, `core` or `bundle` |
| `whatsNew` | list of lines, shown before installing |
| `minApp` | add-on: the lowest program version that can use it |
| `runtime` | program update: the runtime (library set) it was built for |
| `binary` | program update: the name of the program file inside `payload/` |
| `files` | `path` (relative to `payload/`), `sha256`, `size`, `executable` |
| `packages` | bundle: the file names inside `packages/` |

---

## 3. What happens when a package is installed

`addons::install()` in `src/core/Addons.cpp` does this (also from the command line: `quillbinder --install file.qbpkg`):

1. **Inspect.** The manifest is read; every file of `payload/` must be listed, with the right size and SHA-256 sum. A cut-off or changed package is refused.
2. **Fit.** An add-on with a `minApp` above the running version is refused. A program update whose `runtime` differs from the AppImage's (`APP_RUNTIME` in `CMakeLists.txt`, now `{{RUNTIME}}`) is refused: it would not find the libraries it was built against.
3. **Unpack** into a temporary folder `<data>/addons/.new-<id>` or `<data>/core/.new-<version>` (`<data>` is `~/.local/share/Quillbinder`).
4. **Self-test** (program updates only): the new binary is started once, invisibly (`--selftest`, platform `offscreen`), on the AppImage's libraries. If it does not answer "selftest ok", nothing is installed.
5. **Move into place:** `<data>/addons/<id>/` or `<data>/core/<version>/`. A bundle installs its add-ons first and the program update last.
6. **Activate** (program updates): `<data>/core/state.json` gets `active` = the new version, `previous` = the version used before, `attempts` = 0. The user is asked to restart; the open project is saved and reopened.

---

## 4. Starting an installed update (the loader)

The AppImage cannot be changed, so the program built into it acts as a **loader** (`src/core/Launcher.*`, called first in `main()`):

1. At start it reads `state.json`. If a newer program version is active and was built for the same runtime, it starts that binary in its place (`exec`), with the AppImage's libraries and plugins.
2. Every such start is counted in `attempts`. The new version calls `launcher::confirmStart()` once its window is up, which sets the counter back to 0.
3. After **two starts in a row** that did not get that far, the loader switches the update off and the built-in version runs again, with a message for the user. Nothing in the projects changes.
4. **Help > Add-on Manager** lists every program version (`addons::coreVersions()`); the user can go back to an earlier one or remove one. `quillbinder --versions` prints the same list.

---

## 5. Add-ons

Installed add-ons live in `<data>/addons/<id>/`. They can be switched off (they stay installed) or removed in the Add-on Manager. The program looks for content in folders of a fixed name inside every enabled add-on (`addons::contentDirs(kind)`):

| Folder | Content | Used by |
|--------|---------|---------|
| `themes/*.json` | color themes | Window > Themes |
| `layouts/*.json` | window layouts | Window > Layouts |
| `scripts/*.json` | script formats | Format > Scriptwriting |
| `templates/*.zip` | project templates | New Project |
| `compile-formats/*.json` | compile formats | Compile |
| `dictionaries/*.aff, *.dic` | spelling dictionaries | Options > Editing |
| `plugins/*.so, *.json` | plugins (below) | at program start |

An add-on folder in the source tree (`addons/<id>/`) holds `manifest.json` and `payload/`. Extras:

- `outline.json` or `outlines/*.json`: project templates written as outlines; `packaging/build-addons.sh` turns them into template files with `build/qb_template` (`tools/qb-template.cpp`, which uses `src/core/OutlineProject.*` like the welcome project of the program);
- `plugins.txt`: the plugin libraries (built into `build/tool-plugins/`) that are copied into the package;
- `stage.sh`: a script that copies further files in (libraries of the Media Playback add-on, Qt's TLS plugin of the Web Page Import add-on).

---

## 6. Plugins

Plugins are Qt plugins (`QPluginLoader`) found in `plugins/` folders: of the AppImage, of every enabled add-on, of `<data>/plugins` (**Help > Open Plugin Folder**) and of `QB_PLUGIN_PATH` (colon-separated, for developers). They talk to the program only through **interface headers of pure virtual classes**; nothing is linked against the program. A plugin built for another interface version is not loaded.

| Interface | Header | IID | Examples |
|-----------|--------|-----|----------|
| compile output | `src/compile/ExporterApi.h` | `org.quillbinder.ExporterInterface/1.3` | Markdown exporter (part of every program update) |
| writing tools | `src/tools/ToolApi.h` | `org.quillbinder.ToolInterface/1.0` | Name Generator, Scratch Pad, Text Statistics |
| importers | `src/import/ImporterApi.h` | `org.quillbinder.ImporterInterface/1.0` | Word 97–2003, Web Page |
| audio and video | `src/media/MediaApi.h` | `org.quillbinder.MediaEngine/1.0` | Media Playback |

Command plugins (`plugins/command/*.json`) run another program (LibreOffice, Pandoc) for a compile output; they need no code.

Plugins are loaded once at program start, so an add-on with plugins needs a restart after it is installed.

---

## 7. The rules for a program update

- Build on **Ubuntu 24.04 with Ubuntu's Qt 6.4.2**: the update runs on the AppImage's Qt. Use no Qt API newer than 6.4.
- Add **no new library dependency**. Something that needs a library goes into an add-on that brings the library (see `addons/media-playback`, which loads its own FFmpeg and Qt Multimedia by full path).
- `src/text/PageLayout.cpp` calls a private Qt class; the self-test exercises it, so an update built against another Qt is refused at installation.
- Raise `APP_VERSION` in `CMakeLists.txt` for every delivery (corrections: third number; a milestone: second number). The version must be above the version of the user's AppImage (now 0.20.0): an update that is older than the program built into the AppImage is not used. `APP_RUNTIME` changes only when a new AppImage brings other libraries (the AppImages 0.16.0 and 0.20.0 kept `u2404-qt6.4-r1`).
- Projects must stay readable by older versions where possible; a project that needs a newer version names it in the attribute `needs` of the project file, and older versions ask before opening it (from 0.10 on).

---

## 8. Tools in the source tree

| Tool | What it does |
|------|--------------|
| `packaging/build-update.sh "line" ...` | builds a program update from the source: `dist/quillbinder-<version>-update.qbpkg` (own build folder `build-update`); the lines become `whatsNew` |
| `packaging/build-addons.sh [build] [out]` | builds every add-on in `addons/` into `<out>/<id>.qbpkg` |
| `packaging/build-appimage.sh` | builds a new AppImage (rarely; keep `APP_RUNTIME` when the libraries stay the same) |
| `tools/make-package.py addon / core / bundle` | writes `.qbpkg` files with the manifest and the SHA-256 sums; `bundle` packs several packages |
| `tools/e2e-appimage.sh <AppImage> <update.qbpkg>` | end-to-end check with the real AppImage: install, `--versions`, `--selftest`, a cut-off package, the first start with the welcome project and printing its first document, the second start, a Scrivener sample (`QB_SCRIV_SAMPLE`), the way back (`QB_PREV_UPDATE`); `-` instead of the package checks a new AppImage alone |
| `tools/run-all-tests.sh <out> offscreen\|x11` | every test suite, one line each |
| `tools/cloud-setup.sh` | the packages for a fresh Ubuntu 24.04 (build and tests) |
| `tools/get-ffmpeg.sh`, `tools/build-ffmpeg.sh` | the FFmpeg of the Media Playback add-on |
| `tools/qb-template.cpp` | project template files from outlines (the builder is `src/core/OutlineProject.*`, shared with the welcome project) |
| `tools/collect-licenses.py` | the license texts of every library the AppImage carries (`resources/licenses`, Help > Third-Party Licenses); `--check` is run by `packaging/build-appimage.sh` |
| `tools/make-item-icons.py` | draws the document icons of Documents > Change Icon (0.17) |
| `tools/gen-resources.py`, `tools/update-icon-index.sh` | the resource list after adding icons or documents |

Example: a bundle of a program update and an add-on

    packaging/build-update.sh "Mind maps" "Icon bar"
    packaging/build-addons.sh build dist
    tools/make-package.py bundle --id quillbinder-bundle --name "Quillbinder 0.17" --version 0.17.0 \
        dist/quillbinder-0.17.0-update.qbpkg dist/themes-extra.qbpkg dist/bundle.qbpkg

---

## 9. Delivering a version

1. Raise `APP_VERSION`; write what is new in `docs/CHANGES.md` (and the section of the version in the development log `docs/status.md`, where there is one); bring `docs/manual.md` up to date.
2. Run every test suite, offscreen and under X11 (`tools/run-all-tests.sh`); all must end with "0 failure(s)". New functions get checks of their own in a test program under `tests/`; look at the screenshots the tests save.
3. Build the update package (`packaging/build-update.sh`).
4. Run the end-to-end check with the real AppImage (`tools/e2e-appimage.sh`; the AppImage 0.40.0 alpha and earlier packages are on the branch `assets` of the repository; `tools/e2e-old-version.sh` checks that an older AppImage keeps the objects of a project).
5. Commit on a branch, open a pull request, merge it; put the package on the branch `assets` as well.
6. The user installs the package with **File > Install Update or Add-on**.

---

## 10. Reading a crash or freeze report

Since 0.19 the program writes a report into `~/.local/share/Quillbinder/CrashReports` when it ends with a fatal signal (`crash-<unix time>-<pid>.txt`) or when its main thread does not answer for 30 s (`freeze-...`). The user copies it from **Help > Crash Reports**. The code is `src/core/CrashGuard.*` (signal handlers, watchdog, session markers) and `src/ui/CrashDialogs.*` (snapshot, dialogs, recorder of recent actions).

A report has these parts: the header (signal, version, process, time), **Program and system** (the text of Help > System Information, prepared before the crash, with "Things to look at" first), **Process** (memory, threads), **Recent actions** (commands, shortcuts, buttons, windows, keys; typing is only counted), **Recent warnings** (Qt messages), the **Backtrace** (crash) or **Where the main thread is** (freeze), and **Program files in memory**.

Backtrace lines look like `quillbinder(_ZN2qb14TextColumnView4pageEPNS_10TextEditorEb+0x1e)[0x5621a16d853d]`. The program exports its symbols (`ENABLE_EXPORTS`), so most functions are named even in the stripped program of an update; Help > Crash Reports shows the names readable (`qb::TextColumnView::page(qb::TextEditor*, bool)`). A line without a name, such as `quillbinder(+0x55253d)`, is a static function: take the program file of that version with symbols (`dist/quillbinder-<version>-symbols.xz`, kept on the branch `assets` since 0.19; its build ID is the one in the report) and run

    xz -dc quillbinder-0.19.0-symbols.xz > quillbinder-symbols
    addr2line -f -C -e quillbinder-symbols 0x55253d

The offsets always refer to the program file named under **Program** in the report (with its build ID). When an update runs on an AppImage, the backtrace can show the start name `.../AppRun` in front of the parentheses, although the code is that of the update in `~/.local/share/Quillbinder/core/<version>/`; **Program files in memory** shows the real paths. For the program built into an AppImage, extract that AppImage's program file with `--appimage-extract`.

---

## 11. The objects of a project (0.20)

The Objects sheet (manual, section 7) keeps its data in the project folder, beside the project file, never in it: versions before 0.20 read and write the project file as before and leave the folder `objects/` alone. The code is `src/core/Objects.*` (model, templates, storage; `Project::objects()` loads it on first use) and `src/ui/ObjectsUi.*` (inspector tab, window, form, templates dialog).

    objects/schema.json             {"format": 1, "categories": [...], "folders": [...]}
    objects/<category id>/<id>.json one object
    objects/images/                 pictures of objects (<object>-<field>-<random>.png / .jpg)

A category is `{"id", "name", "color": "#rrggbb", "fields": [...]}`; a field is `{"id", "label", "type", "unit"?, "choices"?, "targets"?}` with the types `text`, `longtext`, `number`, `choice`, `yesno`, `date`, `picture`, `objects` (links to objects; `targets` = the category ids it may link to, empty = all) and `documents` (links to binder items). The standard categories have the ids `characters`, `locations`, `environments`, `pois`, `weather`; categories and fields made by the writer get random ids (`cat-...`, `f-...`), so renaming never touches the objects.

An object is `{"format": 1, "id", "category", "name", "folder"?, "color"?, "created", "modified", "fields": {<field id>: value}}`: a string for text, long text (Markdown), number, choice and date, `true` for yes, a list of ids for links, the file name in `objects/images` for a picture. Values of fields that the template no longer has stay in the file. Links inside long texts are Markdown links to `qbobj://<id>` (document links: `qb://<id>`, as in the texts of documents); a text of a document links to an object the same way. Since 0.21.1 a long text can hold text colors, highlights and underlines: such pieces (and pieces with more than one of bold, italic and strikethrough) are written as HTML spans inside the Markdown, for example `<span style="font-weight:700; color:#d62728;">north</span>`, with characters that Markdown or HTML would read as markup written as character references; Qt's Markdown reader shows them (also in 0.20 and 0.21).

Nothing is written until the first object is made or a template is changed. Files are written with `QSaveFile` (a temporary file, then renamed). A file that cannot be read is reported when the project opens and never overwritten; an object of a category that the templates do not have gets a category of that id; a damaged `schema.json` is renamed to `schema.json.damaged-<time>` before a new one is written. The templates for new projects (**Use for New Projects**) are in `~/.local/share/Quillbinder/object-templates.json` (same layout, without folders).

---

## 12. Working with Claude Code

The repository holds a `CLAUDE.md` with the rules above and the user's preferences, so that a Claude Code session (or a person) can take over: set up a fresh Ubuntu 24.04 with `tools/cloud-setup.sh`, build, run the tests, build the package, check it with the real AppImage, and hand it over as described in section 9.
