# Changes

What each version of Quillbinder brought, in short. The user manual (`docs/manual.md`, Help > User
Manual in the program) describes every function.

## 0.70.0 beta (2026-11-10)

A new AppImage and a program update for the AppImages of the same runtime (0.60.0 alpha and later); Scratch Pad 1.2,
Speech Transcription 1.5 and Word 97-2003 Import 1.2. It fixes what two external audits of 2026-11-10 found, speaks
German if you want it, and is the first beta.

- **The save state is always on screen**. The status bar says Saved, Not saved yet, or, in red, that the last save
  failed and when; the title is marked while saves fail. The message of a failed save no longer takes the keyboard
  (before, the next typed key closed it unread), names the documents, offers Save Texts Elsewhere, and comes again
  after five minutes of failing; the program tries again by itself until a save works.
- **Mind maps are saved like texts**. A board that cannot be saved is told, kept in the window, counted when the
  project closes, and written into the emergency copy; before, its changes were dropped without a word. A board file
  that cannot be read opens read-only instead of being replaced by the first change.
- **Backups per project, and restored by the program**. Each project's backups are in a folder of their own, so a
  copy of a project, a restored backup or another project with the same title can no longer delete them (before,
  backups were told apart by the title). File > Back Up > Restore From Backup lists them and opens one in a new
  folder beside the project. A backup shows its progress and can be cancelled, is on the disk before older ones are
  deleted, and no longer carries the lock file with the computer's and the login's name.
- **Fixed: bold inside italics came back with asterisks** in Markdown storage (the standard). Files written so far
  are read right again, and touching formats are written so that they cannot be misread.
- **Fixed: Merge Documents and Split at Selection lost pictures**; they go along with the text now.
- **Fixed: a damaged RTF file could crash the program, or open empty and be replaced by an empty file**; a text file
  saved in another character encoding lost its umlauts. Such files are shown as far as they can be read, read-only,
  with a message, and never written over.
- **Questions before deleting have Cancel as the default**: Enter or a space typed too early no longer empties the
  Trash or deletes a snapshot, a keyword, a collection, a layout, a style or a note; the button names the step.
- **Safer settings**: File > Options opens for all projects; a project's own settings can make saving safer, never
  less safe; the latest automatic save is 30 seconds after a change whatever the delay; a folder with several
  project files (a sync program's conflict copy) opens the one named like the folder and says so.
- **Damaged and hostile files**: a Word 97 file with wrong lengths, a binder nested thousands of levels deep, two
  items with one id, other XML named .qbx, huge counts in a project file or a board are handled without a crash or
  a loss; long formatted paragraphs and long screenplay dialogue are read much faster.
- **Releases are tested and signed apart**: the release workflow runs the full test suite before anything is
  published, and the signing key is used only in jobs that run no third-party code. An installed program update is
  checked for the project's signature before every start, and a program built as the README says now behaves like
  a release (the test switches are off unless asked for).
- **Deutsch**: File > Options > General > Startup > Language switches the menus, windows and messages to German
  (from the next start). The manual and the welcome project stay in English. Each Alt letter is used once per menu,
  also with entries that are hidden at first or change their name (View > Document or Combined Text).
- **Dark themes get a dark page**: a very dark gray page with white text instead of the light paper (a page color
  you chose yourself stays).

## 0.65.0 alpha (2026-11-10)

A new AppImage and a program update for the AppImages of the same runtime (0.60.0 alpha and later); Scratch Pad 1.1
and Speech Transcription 1.4. It fixes what the external audit of 2026-11-10 found, and signs every package.

- **A save lands as a whole**. When one save writes several files (texts, their pictures, the Objects sheet, the
  project file), they are written beside their places first, listed in a journal, and then put in place together. A
  save cut off in the middle is finished at the next opening, or, if it had not got that far, the project stays as
  it was at the save before; a notice says which. One file that cannot be written stops the whole save, and your
  changes stay in the window for the next try.
- **A list of the documents by title**: *contents-by-title.txt* in the project folder names each document, its place
  in the binder and its files, so that a file can be found outside the program.
- **Signed packages**. The release page's packages and checksum list are signed with the project's key; the program
  installs program updates only with that signature, warns before an add-on without it (Cancel is the default), and
  refuses a package whose signature does not match. On the command line, `--allow-unsigned` installs a package
  without the signature. The manual (section 17) shows how to check a download yourself.
- **Fixed: documents made in Scrivener on another computer were deleted**. A project in Scrivener format that was
  open here while Scrivener added documents to it elsewhere (through a sync folder) lost those documents' folders at
  the next full save. They are kept now, and the binder that names them is not replaced; open the project again to
  see both. Pictures, PDFs, sound and video files and mind maps changed in the *.scriv* elsewhere are no longer
  overwritten with the old version, and they are replaced in one step (before, the file was deleted first, so a
  failed copy left none).
- **Fixed: pictures could be lost without a notice** in projects stored as Markdown: a picture that could not be
  written left the text pointing at a file that was not there. Now the save fails and says so.
- **Fixed: Roll Back could empty a text** when the snapshot could not be read. Now the snapshot is read first, one
  that holds no text replaces the text only after a question, and when the snapshot of the current text was asked
  for but cannot be taken, nothing is rolled back.
- **Fixed: the scratch pad could throw a note away** after a save that failed (switching notes, a new note, another
  notes folder, the end of the program). The note stays open with its text now; at the end its text goes into the
  emergency folder. The same for an edited transcription of Speech Transcription.
- **Backups**: a backup with files missing is named *(incomplete)* and never counts among the backups to keep, so no
  complete backup is deleted for it; the automatic backups when a project opens or closes tell such a backup (and
  the one at opening tells a failure, which it did not).
- **The end by logging out or shutting down**: when the save then fails, the texts not saved go into
  *~/.local/share/Quillbinder/Emergency*, the next start says so, and the program ends with status 1.
- **Closing a project that cannot be saved**: *Keep Open* is the default; *Save Texts Elsewhere…* writes the texts
  not saved into a folder you choose.
- **Also fixed**: Duplicate takes out a copy that is not complete (and says so); an object's old picture is deleted
  only after the object's new file is written; a text read only in part is not saved over; a file changed elsewhere
  with the same size and time is noticed; changing the storage format is all or nothing; a lock that cannot be
  written is told; command exporters check the program's exit status, keep the earlier export when they fail, and
  the window keeps being drawn while they run; a bundle installs all its parts or none; large archives are mapped
  instead of read whole; the view state is written again after a failed write; the test runner ends with status 1
  when a suite failed or was not built.
- **The manual** says that the program does not encrypt projects or backups, and what to use instead.

## 0.64.2 alpha (2026-11-10)

A new AppImage and a program update for the AppImages of the same runtime (0.60.0 alpha and later). It fixes what the
full run of the system test of 0.64.1 found.

- **Fixed: crashes**. Adding a comment or a footnote over text of several formats (seen after a long run of edits and
  undos); choosing an entry of Navigate > Go To, a Quick Reference or a bookmark menu whose document had been deleted
  meanwhile; a speech model download that was cut off and resumed (Speech Transcription 1.3). The same mistake (changing text while walking
  through it) was also taken out of the Scrivener writer, the import options, the objects' texts and the inspector's
  snapshot view.
- **Fixed: a picture cut and pasted was lost when the text was saved**, and letters typed right after a picture
  could take the picture's format and were then saved as pictures, one per letter.
- **Scrivener projects**: a picture in the notes is kept; a synopsis that starts with a space keeps it; a text
  changed in Scrivener (or on another computer) while the project was open here is shown at the next opening (before,
  the old text stayed until the project changed again); a project opened and closed without a change stays as it was
  (the .scrivx was written anew in this program's layout, the label colors, the top folders, the name of the computer
  that wrote it last, and the search index were changed); the leftover of a save that Scrivener did not finish (`content.rtf.XXXXXX`) is no longer imported as a
  document; Scrivener's lock names who has the project open in either form of the file.
- **A project folder or a Scrivener project renamed or moved while open is no longer made anew**. Saving made a new
  folder under the old name that held only the documents changed since; the real project could then not be put back
  under its name. Now saving fails with a message, your changes stay in the program, and the next save after the
  folder is back writes them.
- **The spare copy of a project file opens the project**. Opening `settings/spare-copy.qbx` showed every text empty,
  and a backup made then held only the settings folder.
- **Word counts**: an accent written as a character of its own, letters outside the common Unicode range and numbers
  such as 3,500 or 1.000.000 now count as one word each.
- **Superscript and subscript at the end of a word** act on what you type next (H₂O); before, they changed the word.
- **German quotes while typing**: an apostrophe typed after a word closes an open ‚ with ‘; an apostrophe inside a
  word (geht’s) stays one.
- **Markdown storage**: a paragraph of the style Centered keeps its style (it came back as a paragraph centered by
  hand); a bold or italic run that ends with a space of a format of its own no longer comes back with its asterisks as
  text; a centered paragraph keeps no-break spaces at its ends.
- **Notices that come while you type no longer take the keys**: "Project Opened Elsewhere" and "Other Versions Kept"
  appear beside the window without taking the keyboard, so what you type still goes into the text. Before, the keys
  went into the notice, and a Space could press "Close the Project Here"; the typing after it was lost.
- **A save that fails when you switch to another program is told** (the window saves when it loses the focus); on a
  full disk it failed silently. A Scrivener project's search index that cannot be written fails the save as well.
- **Backspace at the start of a list item** takes the paragraph out of the list with the spacing and indent of its
  style; it kept the list's until the project was opened again.
- **RTF files whose font is named after its italic or bold face** ("SitkaTextItalic") give italic or bold text also
  in projects that take the project's own fonts for imported text.
- **A second start with a project that is open** brings its window to the front (before: the question whether to open
  it anyway, and then a second window on the same project). Another project still opens in a window of its own.
- **The Documents menu** names the label and status lists as the project calls them (Project > Project Settings).
- **File > Options in "This project"**: settings that belong to the program alone (the computer's name and the like)
  changed there are kept (they were dropped).
- **File > Save while Scrivener has the project open** shows the notice once, as the automatic save does, instead of a
  second message.
- **Documents > Snapshots > Take Snapshot** says so when the snapshot cannot be written.
- **Compile through Pandoc (EPUB) without an author** no longer writes an empty author (e-book checkers refuse it).

## 0.64.1 alpha (2026-11-09)

A new AppImage (also as the full version with all add-ons) and a program update for the AppImages of the same runtime
(0.60.0 alpha and later). It fixes what the first run of the system test (crash safety and usability) found, and the
points of an external review of the program's safety (2026-11-09) that are changes of the program.

- **Fixed: a text only looked at could be saved over a newer version from another computer**. Showing a document
  marked it as changed, so a version a sync program had brought in was replaced by this computer's older text (kept
  only as a copy and a snapshot). A document that was not edited here is no longer written over.
- **Fixed: the other version of a changed file could be lost when it could not be kept**. When a file was changed
  elsewhere and here, and its other version could not be kept (a full disk, a folder that cannot be written), this
  computer's version replaced it anyway, and the notice still said it was kept. Now nothing is replaced until the
  other version is kept: saving fails with a message naming the file, and your changes stay unsaved. This holds for
  texts, notes, the project file, and Scrivener projects.
- **Fixed: empty paragraphs in table cells came back as "<p/>" (Markdown storage)**. A cell that began or ended with an
  empty paragraph was read back with the text "<p/>", and the text of a table inside a cell was not saved (in RTF
  storage neither: such a table comes only by pasting from a word processor; its text now stays in the cell). Projects
  saved by 0.62 to 0.64 are read right.
- **Fixed: Split at Selection could lose a table**. Inside a table cell, or at the paragraph right after a table, the
  split took the whole table out of both documents. Inside a table or a picture's caption the split is refused with a
  note now; next to a table it keeps the table.
- **Fixed: a figure cut and pasted lost its picture**. The picture now comes along on the clipboard.
- **Compiling shows its progress**. A long manuscript (with long tables) held the program for half a minute without
  drawing the window, and the program's own watchdog wrote a freeze report. Compile and its preview now show a progress
  window and keep the program responsive while they work.
- **Table cells are no longer justified with the body text**. In the editor, PDF, Word and e-book a narrow cell spread
  its words over its width. An alignment you give a paragraph in a cell stays.
- **Merge also merges the notes**. Before, the notes of the documents after the first one stayed behind in the Trash.
- **Fixed: a crash after Merge Cells, Undo, Redo and Delete Rows**. It happened in a table whose merged cells were empty.
- **Fixed: a save on a full disk could report success and leave a cut-off file**. The last part of a file (and its
  sync to the disk) was written without looking at the result. Now a file replaces the old one only when it was
  written whole; otherwise saving fails with a message and the old file stays. A backup is made only after a
  successful save (before, it went on without the latest changes and reported success).

From the safety review:

- **A project, archive or package from someone else can no longer reach outside its folder**. Names of documents in a
  project file, of files in ZIP archives (templates, backups, add-ons) and of files in Scrivener projects are checked;
  before, a made-up name could make Empty Trash delete a folder beside the project, or an archive write a file
  anywhere. Damaged archive entries (a wrong checksum, cut off) are no longer read as text or written as empty files.
- **Links that lead out of the program are checked**. Web and mail links open at once; a link to a file, a folder, a
  network share or another program is shown in full and opened only when you agree; links to programs, scripts and
  shortcuts are never opened. This holds for texts, mind maps, bookmarks and imported web pages.
- **Scrivener's lock is looked at before every save into a Scrivener project**. When Scrivener opens the project while
  it is open here, nothing more is written into it (both programs used to write); a notice says so, and the changes
  stay in the working copy until Scrivener has closed it.
- **Settings a project may bring are limited**. Programs to start, folders, converters and your contact data are never
  taken from a project, and from an options file only after you have seen them and agreed.
- **A name of your own for this computer in shared folders**. Set it in File > Options > General > Saving; your user
  name is then left out of lock files. Locks tell two computers of the same name apart.
- **Speech models are checked before they are read**. The checksum (now SHA-256) comes first; a model from the
  official source must be the known one, and other addresses must be https.
- **A warning before packages with program code**. Also versioned libraries, plugins and templates count. Damaged Word 97 files and very deeply nested Word or XML files are refused instead of filling the memory or
  crashing. The folder for the links between running programs in /tmp is checked.
- **Safer builds**. The switches of the tests (plugins from other folders, skipped checks) are only in test builds;
  the AppImage tools are pinned to tagged releases with checksums; the release workflow keeps its write permission in
  a publishing job of its own.
- **A privacy section in the manual**. "Privacy: what leaves your computer" (chapter 1).

## 0.64.0 alpha (2026-11-08)

A new AppImage (also as the full version with all add-ons) and a program update for the AppImages of the same runtime
(0.60.0 alpha and later).

- **Safer with a project in a synced folder on several computers**. It works on the level of files, whatever the sync
  program is (OneDrive, Dropbox, Nextcloud, Syncthing ...). The lock in the project names the computer and is renewed
  every five minutes, so another computer shows who has the project open, since when, and its last sign of life (a lock
  not renewed for 15 minutes counts as left over). A notice comes when another computer opens the project while it is
  open here. A file changed elsewhere and here is no longer overwritten silently: the other version is kept as a
  snapshot and as a copy in settings/conflicts (also the project file, and in Scrivener format). Opening and closing
  without a change changes no file (the window layout is kept per computer in settings/ui-<computer>.json; a .scriv is
  no longer rewritten at every close). Texts whose files have not arrived yet are shown read-only and never written.
- **Speech Transcription can record now and write the text later**. With Record only (the add-on Speech Transcription 1.1; File > Options > Speech;
  the standard on Android later) the microphone only records into a sound file beside the transcriptions, which uses little
  power; a Transcribe button in the recording's line (and Transcribe All) writes its text later, on this computer or
  another one. Optionally the recording is deleted once its text is there.
- **Saved more often while you type on**. The first short break once 5 seconds have passed saves (before: only a pause
  of the autosave delay, else every 30 seconds), and 30 seconds stay the latest.
- **Fixed: the spelling menu on words with an apostrophe**. A right-click on "Kayel's" (straight apostrophe) near its
  end gave the long menu without suggestions, and on "D'Nersun" the menu offered to add "Nersun". The menu now takes
  the word the underline marks, at any place of it.
- **Fixed: the corkboard's card size could get lost**. Only a drag with the mouse was saved; a change with the keys,
  the wheel or a click beside the handle went when the settings were read again (for example when a project opened).

## 0.63.0 alpha (2026-11-08)

A new AppImage (also as the full version with all add-ons) and a program update for the AppImages of the same runtime
(0.60.0 alpha and later). It brings together 0.60.5, 0.61.0, 0.61.1 and 0.62.0, which were not published on their
own (their sections in this file say more), and a few fixes:

- **Speech Transcription** (a new add-on, part of the full version): speech becomes text on this computer, with a
  model downloaded once under File > Options > Speech (0.61.0).
- **Tables and pictures**: Insert > Table and Format > Table with ready-made designs or your own, pictures beside the
  text with captions and descriptions, also in compile to PDF, Word, e-book and Markdown (0.62.0).
- **Settings per project**: File > Options > Settings for: This project, and a window at the first start for backups
  and settings (0.62.0).
- **Quote marks of a whole project converted** (File > Options > Corrections) and **Project > Delete Old Snapshots**
  (0.61.1).
- **Undo and Redo** on the main toolbar, and the crash of Split at Selection from the text's right-click menu fixed
  (0.61.0).
- **Large projects in Scrivener format** save much faster while you write (0.60.5).

- **German spelling knows words with umlauts and ß**: the German dictionaries (of LibreOffice and Ubuntu) are
  stored in ISO 8859-1, and the program handed every word to them in UTF-8, so "Straße", "Jänner" or "Wörterbuch"
  were always underlined. Words now go to each dictionary in its own encoding.
- **An update package with add-ons installs on a program that already has its version**: inside a bundle, a
  program update the program already contains is left out (and the bundle says so); the add-ons go in. Before,
  the whole bundle was refused.
- **Add-ons for another system are refused**: a package names the system its libraries were built for; a Windows
  add-on cannot be installed on Linux (and the other way round).

## 0.62.0 alpha (2026-11-08)

A program update for the AppImage 0.61.0 alpha.

- **Settings per project**: File > Options has **Settings for:** at the top, **All projects (the program)** or
  **This project**. What you change for "This project" is kept with the project (in *settings/options.ini* of its
  folder) and applies while it is open; everything it does not set comes from the program's settings. **Use Program
  Settings** removes a project's own settings again; the line below the choice says how many it has and lists them.
  A few settings belong to the program only: the recent projects, reopening the last project, the desktop's file
  dialogs, window places and layouts, crash report times, and the look for new projects (a project's look has been
  its own since 0.40.2).
- **Which settings come first** (File > Options > General > Startup, for all projects): a project's own settings
  first (the standard), or the program's settings always; then projects' own settings are not used (they stay in the
  projects for later), and every project also shows the look for new projects.
- **Project > Save Project Settings to File** has a new section, **Options of This Project**.
- **First start**: a small window asks two things before the first project: backups (on closing, how many, the
  folder) and which settings come first. Everything is filled in; OK keeps it.
- **Tables**: **Insert > Table…** (rows, columns and the design at once), and **Format > Table** (also in the
  right-click menu inside a table): insert rows above or below and columns left or right, delete rows, columns or
  the table, merge and split cells, a cell's own color, and **Table Properties…**. Tab goes to the next cell (in
  the last cell it adds a row), Shift+Tab back.
- **Table design**: five ready-made designs (Grid, Grid with a header row, Striped rows, Lines between the rows, No
  lines) or your own: lines (width, color, only between the rows), space inside and between cells, header rows
  (with a color; printed again on every page), a color for every other row, the width (a share of the text or as
  wide as the contents), column widths, the alignment, and the place: on its own lines, or left or right with the
  text flowing beside it.
- **Pictures** (**Format > Picture…** or right-click a picture): the width, the position (in the line, left or
  right with the text flowing around it, or centered on its own lines), a **caption** under the picture, and a
  **description** of what it shows. The description is not shown in the text; it goes along as the alt text of
  e-books, Word files and Markdown, and screen readers read it.
- Tables, pictures beside the text, captions and descriptions are kept in both storage formats (Markdown and RTF)
  and in Scrivener format (as RTF tables; Scrivener shows pictures beside the text as ordinary paragraphs). Tables
  from RTF files of other programs become tables.
- **Compile**: tables and figures in PDF (a long table goes on over pages with its header rows on each), Word (real
  tables; pictures beside the text in a borderless table with their caption, so Word and LibreOffice let the text
  flow around them), e-books (tables, figures with captions and alt text), and Markdown (pipe tables, alt text).
  Plugins for other output formats see tables and figures through the exporter interface 1.4 (plugins built for 1.3
  must be rebuilt; the Markdown exporter that comes with the program is).

## 0.61.1 alpha (2026-11-08)

A program update for the AppImage 0.61.0 alpha.

- **Convert quote marks** (File > Options > Corrections, under Smart quotes): **Convert to Typographic Quotes…**
  turns the straight " and ' already in the project into the marks of the chosen style (“…” ‘…’, „…“ ‚…‘, and the
  others), opening or closing by their neighbors, an apostrophe inside a word or before a number as ’;
  **Convert to Straight Quotes…** goes back. A window like Project Replace chooses where (titles, text, notes,
  synopses, custom metadata, comments and footnotes) and the scope (the whole project or the selected documents),
  counts first, and takes a snapshot of every text it changes. Formatting stays.
- **Project > Delete Old Snapshots…** (below Empty Trash): deletes every snapshot of the project, also of
  documents in the Trash, so the project gets smaller; the texts stay as they are. It asks first, with the number
  of snapshots and their size.
- **Fixed, Scrivener format**: a snapshot deleted in the program (in the inspector) stayed in the *.scriv*: Scrivener
  still showed it, and it came back whenever the program read the *.scriv* again. Snapshots deleted here now leave the *.scriv* with the next
  full save (switching to another program, File > Save, closing); a snapshot only Scrivener has stays.
- Speech Transcription, tested with real models for the first time (Small and Medium from Hugging Face): German
  speech is written in English; German words inside English sentences come out right when they are listed under
  Hint words (the manual says so now). The checksums of Small and Medium match the files on Hugging Face.

## 0.61.0 alpha (2026-11-08)

- **Speech Transcription** (a new add-on, "Speech Transcription" 1.0): speech becomes text on this computer only.
  A microphone button on the main toolbar starts and stops a recording; the arrow beside it chooses the
  microphone and opens the options. What you say is written into a new transcription file (named by date and
  time, in *Documents/Quillbinder Transcriptions*), shown in the new inspector tab **Transcriptions** (the first
  button of the inspector's row), where it can be edited, renamed, copied and deleted. A pause ends a piece; its
  text follows a few seconds later; a long pause starts a new paragraph. Recognition by whisper.cpp 1.9.5.
- **File > Options > Speech** (a section of its own): the model (Small, Medium (recommended), Large v3, Large v3
  Turbo, each with a note on download size, memory, speed and translation), downloaded once when you choose it
  from whisper.cpp's model files on Hugging Face, or from another address, or a file you downloaded yourself. Before
  a model is used it is checked (its checksum, the model's description in the file, a test transcription of a
  short recording); only then does it replace the model in use, which is deleted. An interrupted download goes on
  where it stopped. Further: the processor or a graphics card (Vulkan; offered when one with enough memory is
  found), processor threads, fast or careful, a speed test, the main language (the text) and a fallback language
  (also spoken: with English as the main language it is written in English), hint words, the microphone,
  sensitivity, the pauses for a piece and a paragraph, the folder, two advanced switches.
- **Undo and Redo** on the main toolbar: in the standard set after Move to Trash (a toolbar you arranged
  yourself keeps what it has).
- **Crash fixed: Split at Selection from the text's right-click menu** (reported on 2026-11-08 with 0.60.5): the
  split loads the new document into the editor, which replaced the editor while its right-click menu was still
  handling the click; the program then used memory that was already given back and crashed. The menu now stays
  until its click is handled. The same could happen with Split with Selection as Title and Open Link of that menu.
  Ctrl+K and the Documents menu were not affected.
- For add-ons: the **tool interface 1.1** lets a writing tool add a tab to the inspector, a button with a menu
  to the main toolbar (a toolbar arranged by the writer gets it once) and a section of its own to File > Options.
  It adds to 1.0 without changing it: the tools built for 1.0 keep working, and an update of such an add-on no
  longer needs a program update.

## 0.60.5 alpha (2026-11-07)

- **No more short stops while writing in a large project in Scrivener format**: the automatic save now writes
  only the texts, notes and synopses you changed into the *.scriv* (and the binder when it changed). Before, every
  automatic save went through the whole project: it read and checked every file on both sides, rewrote Scrivener's
  search index with every word and rewrote the binder file. With a test project of 500,000 words (1,000
  documents) an automatic save took about 250 to 350 ms on the test machine and now takes about 30 to 40 ms.
  Scrivener's search index, snapshots, pictures and the rest follow when you switch to another program, use
  File > Save, or close the project, as before.
- **Counts no longer rewrite the project file every few seconds**: new word counts while you type are written
  with the next full save, or a minute later at the latest (the texts themselves are saved as before, a few
  seconds after you stop). This helps large projects in either format.
- **Opening a dialog or panel saves quickly**: when another window of Quillbinder takes over (a dialog, a panel,
  the composition window), the quick save of the automatic save runs; the full save is kept for switching to
  another program and for closing.
- **Menus without animation**: Qt's own animations of menus, lists and tooltips stay off, whatever the desktop
  theme asks for. (A check with Qt 6.4 and the KDE settings found them off already; now they are off for
  certain.)
- **Spelling suggestions** are remembered for the session: a second right-click on the same misspelled word
  opens the menu at once (the first search of the dictionary took about 50 ms on the test machine).
- For checking speed: started with `QB_TIMING=1`, the program writes the times of each save step and of the
  right-click menu to its error output.

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
