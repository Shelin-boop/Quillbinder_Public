# Changes

What each version of Quillbinder brought, in short. The user manual (`docs/manual.md`, Help > User
Manual in the program) describes every function.

## 0.60.4 alpha (2026-11-07)

- **Spacing and indents in the format bar**: four new buttons after the list buttons set the space before and
  the space after the paragraph (0, 3, 6, 9, 12 or 18 pt, or any value), the indent of the first line, and the
  indent of the whole paragraph (in cm or inches, as the ruler). They act on the paragraph at the cursor or all
  selected paragraphs, and Edit > Undo takes the change back. A format bar you arranged yourself gets them once.
- Brings the changes of 0.60.3 (below) to everyone: it is the first public version with them.
- The README says that Quillbinder works under Kubuntu 26.04 LTS with Wayland (the sentence added on GitHub on
  2026-11-06 is now part of the source, so publishing keeps it).

## 0.60.3 alpha (2026-11-07)

- **The binder counts a folder in full**: resting the mouse on a folder (or a text with subdocuments) shows the
  words of the folder and everything inside it, as the editor's footer does, and the folder's own words when it
  has text of its own. Before, it showed only the folder's own text (often "0 words").
- **The folder total in the middle of the footer, with details**: on the corkboard and in the outliner the total
  sits in the middle of the footer; a click shows words, characters, characters without spaces, paragraphs and
  pages for the whole folder, as the count of a text does. The same for the total beside a folder shown as text.
- **Inspector Background**: the color "Lists and text fields" (File > Options > Appearance > General Interface)
  is now **Inspector Background** and colors only the inspector's lists and text fields (bookmarks, notes, the
  title, the metadata fields ...), the same in every project; the text there turns light or dark to stay
  readable. The inspector's right-click Appearance menu offers it as "Inspector Background (All Projects)". A
  color you chose for "Lists and text fields" is kept as the Inspector Background. The inspector's own color
  "Background" (Inspector & Notes) is now called **Inspector Strips**: the tab row, the headings and the
  Label/Status row.
- **Find the document you are writing in**: a double-click on the space beside the text (the paper beside the
  column or the area around it) selects that document in the binder, opens the folders above it, and lets its row
  blink twice in orange; the editor keeps what it shows. Navigate > Reveal in Binder (Ctrl+Shift+R) blinks too.
- **Italics from Scrivener in the right font**: Scrivener for Windows names the font of italic text by its face
  (for example "SitkaTextItalic"), which is no font family on Linux, so the text was shown in the desktop's
  fallback font (Noto Sans). Such names (also "Georgia-BoldItalic", "TimesNewRomanPS-ItalicMT", "Sitka Text
  Italic") are now shown in the family they stand for, in italic or bold as the name says; the text keeps the
  name, so Scrivener gets back what it wrote.
- **Toolbars and options in the color of the menus**: the fields on the toolbars (quick search, style, font,
  size, line spacing), the ruler, and the row of pages and the lists on the left of File > Options have the color
  of the window and the menus, no longer the color of lists.

## 0.60.2 alpha (2026-11-06)

- **Three ways to paste in the right-click menu** of the text: Paste, Paste and Match Style, and Paste with
  Formatting, one below the other, with their shortcuts (on a misspelled word under More Commands).

## 0.60.1 alpha (2026-11-06)

- **Paste with Formatting** (Edit menu, **Ctrl+Alt+Shift+V**): text from a word processor or a web page keeps
  its fonts, sizes, colors and the alignment, indents and spacing of its paragraphs, also in Markdown projects.
  Paste itself is unchanged.
- **Show Invisibles** (View > Text Editing, and a ¶ button on the toolbar): dots for spaces, arrows for tabs, a
  circle for a no-break space, a bent arrow for a line break and ¶ at the end of each paragraph, drawn on the
  screen only. If you changed the toolbar yourself, add the button under View > Toolbars > Customize Toolbars.
- **The total of a folder**: the footer of a folder (text, corkboard or outliner) and the inspector's metadata
  show the words of the folder and everything inside it (characters when the footer counts characters).
- **Styles take over the paragraphs**: a paragraph style chosen for a selection over several paragraphs styles
  all of them in full, and fonts, sizes, colors, letter spacing and capitals set by hand in them give way to the
  style; bold, italics, underlining, strikethrough, highlights and character styles stay. A selection that ends
  right at the start of a paragraph leaves that paragraph alone.

## 0.60.0 alpha (2026-10-31)

- **Audio and video in one package**: *media-all-formats.qbpkg* installs the add-ons Media Playback and Extra
  Video and Audio Formats in one step, with every format the program plays (H.264, H.265, AAC and the other
  formats licensed under patents included). *media-playback.qbpkg* alone still plays the free formats, and the
  extra formats can still be switched off or removed on their own in Help > Add-on Manager.
- **The full version**: *Quillbinder-0.60.0-full-alpha-x86_64.AppImage* is the program with all add-ons and the
  libraries they need inside (audio and video in every format, themes, templates, the scriptwriting pack, the
  writing tools, the importers). They are installed at its first start; one download, nothing else to install.
- **One version for everyone**: 0.60.0 alpha brings the public 0.50.0 alpha and the changes of 0.40.1, 0.40.2
  and 0.40.3 together (listed below): fonts with several designs, the format buttons of a selection, a look per
  project, project settings files, titles from the first line, the style list, the alignment of the main text
  and more.
- **About** names FFmpeg, which the audio and video add-ons use, with its license (GNU LGPL 2.1 or later) and
  where its source code is.
- **Character and place sheets** of the fiction templates have new fields of their own (In one sentence, Part
  in the plot, Voice, Goal, Fear, Secret, Ties to other characters ...; for places: Where it is, By day and by
  night, Who comes here ...). Existing projects keep their sheets.
- For developers: the public source is a copy of the development repository's, made by
  `tools/publish-public.sh` after `tools/legal-check.py` has checked names, texts, files and licenses
  (`docs/publishing.md`).

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
## 0.40.3 alpha (2026-10-29)

- **Documents without a name are named by their first line**: a document called "Untitled" (or with no
  title) takes the first line of its text that has words as its title (at most 60 characters, cut at a
  word; inline annotations, inline footnotes and pictures left out). The title follows the first line
  until you rename the document by hand; a document without text is "Untitled" again. Existing
  "Untitled" documents with text get their name when the project opens. It is a real title: saved,
  compiled, written to Scrivener projects.
- **The format bar shows what you see**: Bold, Italic, Underline, Strikethrough, superscript and
  subscript are on when all of the selection looks so, also when a style makes it so (a heading's bold);
  Bold on a bold heading makes it not bold, and it stays so when saved, also in Markdown projects. Font,
  size, alignment, line spacing and style show the whole selection; when they differ within it, the
  field stays empty and no alignment button is on.
- **Styles panel**: **No Style** has **Redefine From Selection** (right-click, or the Redefine button):
  the formatting at the cursor becomes the project's main text style (font, size, line spacing, first
  line indent, space after paragraphs, alignment), and all text without a style in the project follows.
  The dialogs and questions the panel starts now open above the panel.
- **Alignment of the main text**: File > Options > Editing > Formatting and Project > Project Settings >
  Formatting have four buttons (left, centered, right, justified) instead of "Justify text".
- **Style list** of the format bar and of the right-click menus: **Customize Style List** (in the list,
  in Format > Style) chooses which entries show and in what order; with more than ten the list scrolls;
  New Style From Selection, Redefine Style From Selection, Styles Panel and Customize Style List stay
  below it. The choice belongs to the project and goes into project settings files.
- **Spelling**: on a misspelled word the right-click menu is short: the suggestions, Add, Ignore and
  **More Commands** with everything else, so the suggestions open next to the mouse.

## 0.40.2 alpha (2026-10-26)

- **Every project keeps its own look**: the theme, colors and fonts of the binder, editor, corkboard,
  outliner, inspector and composition mode, the text column width and the new border beside the text
  belong to the project. A change in one project, a new standard for new projects, or an update of the
  program no longer changes how an existing project looks. **File > Options > Appearance** shows and
  changes the look of **This project** or of **New projects**; menus and panels stay one look for the
  whole program. A project opened first by 0.40.2 keeps the look it had.
- **Border beside the text**: paper in the paper's color left and right of the text, 10 px for new
  projects (0 to 100 px, File > Options > Appearance > Main Editor), outside the text column, so lines
  break as before. Older projects keep theirs without a border until it is set.
- **Right-click menus** offer the colors, fonts and sizes of what is under the mouse (Appearance): the
  text and the paper beside it, the header bar, the binder, the corkboard, the outliner, the inspector
  and its notes, composition mode.
- **Project > Save Project Settings to File / Load Project Settings from File** (*.qbsettings*): labels,
  status, section types, custom metadata, keywords, formatting, styles, script format, targets, compile
  settings, page and print setup, the look, the object templates. When the project's settings differ
  from the file, a window asks per section: take the file's, keep the project's, or the Quillbinder
  default. What documents use is never lost; the settings as they were are kept to go back.
- **Style changes reach all text of the style**: font, size, bold, italics, color, letter spacing,
  capitals, alignment, indents, spacing, line spacing and tab stops of a changed style are taken over
  also by text of the style that had its own values (set by hand or from an import). A snapshot of each
  changed document is made first.
- Corrected: a click or a right-click in the editor made the text jump (near the top or bottom edge of
  the view; a right-click beside the text even jumped to the end of the document and lost the
  selection). The view now stays where it is; a click beside the text puts the cursor on the line beside
  the click.

## 0.40.1 alpha (2026-10-25)

- **Fonts with several designs** (the optical sizes of Sitka on Windows 11 and similar fonts) are listed
  as one font per design ("Sitka Small", "Sitka Text" ...). Bold and italic now stay within the chosen
  design; before, bold text could come out in another design and look smaller and narrower. The font
  files are not changed.
- Corrected: the Bold, Italic, Underline and Strikethrough buttons did not show the state of the
  selected text when it was selected from right to left (they showed the character in front of the
  selection). They are now on when all of the selection has the format; Bold on a partly bold selection
  makes all of it bold. The same in the text fields of objects and in the fonts window.

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
