# Changes

What each version of Quillbinder brought, in short. The user manual (`docs/manual.md`, Help > User
Manual in the program) describes every function.

## 0.50.0 alpha (2026-10-25)

- **Download from the project page**: the AppImage, the program update and every add-on are
  published on the Releases page of https://github.com/Shelin-boop/Quillbinder_Public, built by GitHub
  from the public source code. Each release lists SHA-256 checksums of its files.
- **Source code of every library**: the release "Third-party source code" holds the source of each
  library the AppImage and the add-ons carry, exactly in the version used (Ubuntu's source packages,
  FFmpeg with the build scripts, the AppImage runtime). Help > Third-Party Licenses names the file for
  each library, and the add-ons' license notes do the same.
- **Media Playback 1.1** decodes free formats only (MP3, Ogg Vorbis, Opus, FLAC, WAV, WebM with VP8 or
  VP9, Theora ...). H.264, H.265, AAC, AC-3 and the other formats licensed under patents are in the
  new, separate add-on **Extra Video and Audio Formats**; while it is installed and switched on, Media
  Playback uses it. Without it, a file in such a format says which formats it uses and what to do.
- An AppStream description for software centers; the Add-on Manager names the contents of the new
  add-on.

## 0.40.0 alpha (2026-10-25)

- **Welcome project**: the first start opens a short project that shows, one document each, what
  Quillbinder has beyond the usual: the icon bar, a zoom for every part of the window, mind maps (with an
  example board), object sheets (with linked example objects), the comments of a whole folder, Compare
  and Transfer, Scrivener projects, themes, updates and add-ons, and what protects your text.
  **Help > Welcome Project** opens it again or makes a fresh copy.
- **Help > Quick Start Guide**: where the important settings are, and what the program can do.
- **Help > Third-Party Licenses** (also a button of Help > About): every part of other projects the
  AppImage carries, with its full license text and where its source code is; the texts are also in the
  AppImage (`usr/share/doc/quillbinder/licenses`).
- **License**: Quillbinder is now under the Zero-Clause BSD license (0BSD).
- The view of several documents as one text is called **Combined Text**. Settings and theme files of
  older versions keep working.
- The interactive tutorial of 0.15 to 0.30 was removed.
- Corrected: Page Down and Page Up in the combined text skipped the documents between the first and
  the last one when the cursor landed between two documents (seen in 0.30.0 alpha).
- Corrected: a crash, now and then, when a project was closed ("corrupted double-linked list"), if a
  picture or PDF file of a project closed earlier had been shown in the same window.

## 0.30.0 alpha (2026-10-25)

- The AppImage with the program of 0.21.2; a version label ("alpha") in Help > About and the file name.

## 0.21.x (2026-10-22 to 2026-10-24)

- File > Options and the compile window reorganized, with options for the program's own features
  (Extras); Ctrl + mouse wheel zooms the part under the mouse everywhere; a zoom per part of the
  inspector; mind map colors, shapes, icons on notes and lines; places kept with the project; Scrivener
  headings, paragraph styles and project bookmarks on import; the standard formatting of new projects.

## 0.20.0 (2026-10-19)

- The Objects sheet: characters, locations, environments, points of interest, weather and categories of
  your own, with links; data-safety fixes (control characters, unreadable files, backups over 4 GiB,
  autosave while typing, the spare copy of the project file).

## 0.17.0 to 0.19.0 (2026-10-19)

- Document icons, the icon bar, Icon from Text, installing fonts, color buttons, the comments of a
  folder, mind maps; Compare and Transfer with Word and LibreOffice files; Mind Map Help; crash and
  freeze reports, Help > System Information.

## 0.14.0 to 0.16.0 (2026-10-17 to 2026-10-18)

- Projects in Scrivener 3 format; printing; the AppImage 0.16.0.

## 0.2.0 to 0.13.0 (2026-10-03 to 2026-10-16)

- Updates and add-ons as packages, fonts and paragraphs, styles, themes, menus, toolbars, shortcuts and
  layouts, page view and copyholders, drag and drop and Quick Reference, search and replace,
  scriptwriting, writing tools (Name Generator, Scratch Pad, Text Statistics), audio and video, import of
  old Word files and web pages.

## 0.1.0 (2026-09-26)

- Projects and templates, binder, editor, corkboard, outliner, inspector, snapshots, targets, composition
  mode, Markdown or RTF storage, compile to DOCX (PDF, ODT and EPUB through plugins), Scrivener import,
  backups.
