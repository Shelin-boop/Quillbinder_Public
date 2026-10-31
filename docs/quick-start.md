# {{APP}} Quick Start Guide

{{APP}} is a writing program for long texts. If you have worked with other binder-based writing programs, most of it will feel familiar: a binder on the left, the editor in the middle, an inspector on the right, and Compile at the end. This guide shows where the important settings are and sums up what {{APP}} can do. The full manual is under **Help > User Manual** (**F1**); a short tour of the features you may not know yet is in **Help > Welcome Project**.

---

## Starting

- When no project is open, the **start window** offers **New Project** (with templates such as Blank, Novel, Short Story, and Nonfiction Book), **Recent Projects**, **Open Other…**, and **Import Scrivener Project…**.
- {{APP}} saves by itself a few seconds after you stop typing. **Ctrl+S** saves at once.
- The first time {{APP}} starts, it opens the **welcome project**, saved in your *Documents* folder. Delete it whenever you like; **Help > Welcome Project** makes a fresh one.

---

## Where the important settings are

| What you want | Where |
|---------------|-------|
| **Dark mode** or another color theme | **Window > Themes**: Dark Mode, Midnight, Sepia, Slate, Ocean, High Contrast, Light, or Default (follows your desktop). The change takes effect at once. |
| Colors and fonts of each part of the window | **File > Options > Appearance** (or **Window > Themes > Colors and Fonts…**), or a right-click on the part (**Appearance**). Every project keeps its own look; the list at the top chooses this project or new projects. |
| Your own theme | **Window > Themes > Save Current Look as Theme…**; **Manage Themes…** exports and imports *.qbtheme* files. |
| Interface font, toolbar icon size, the full path in the title bar | **File > Options > Appearance > General Interface** |
| The text column: fixed width, paper color, width, border beside the text | **File > Options > Appearance > Main Editor**, or a right-click on the paper |
| The project's settings in a file, and back into this or another project | **Project > Save Project Settings to File…** / **Load Project Settings from File…** |
| Font and paragraph formatting of **new** projects | **File > Options > Editing > Formatting** ("Use Formatting in Current Editor") |
| Font and paragraph formatting of **this** project | **Project > Project Settings > Formatting** |
| Zoom | **Ctrl+mouse wheel** over any part of the window: each part keeps its own zoom. Start values: **File > Options > Editing**. |
| Toolbars | **View > Toolbars > Customize Toolbars…** (or right-click a bar) |
| The icon bar | **View > Toolbars > Icon Bar**; **Choose Icons for the Icon Bar…** |
| Keyboard shortcuts | **File > Options > Keyboard** (search, press new keys, export and import); **Help > Keyboard Shortcuts…** lists them |
| Window layouts | **Window > Layouts** (seven built in; save your own) |
| Autosave, recent projects, author information | **File > Options > General** |
| Backups | **File > Options > Backup** (when, how many, where); **File > Back Up** makes one now |
| Spelling and corrections while typing | **File > Options > Corrections** |
| Where links open, the Return key, composition mode, playback | **File > Options > Behaviors** |
| Import and export settings | **File > Options > Sharing** |
| Mind maps, objects, add-ons, crash reports | **File > Options > Extras** |
| Labels, status, section types, custom metadata, special folders | **Project > Project Settings** |
| Take your settings to another computer | **File > Options > Manage > Save Options to File… / Load Options from File…** |
| Everything back to the standard | **File > Options > Defaults** (your projects, themes, layouts, and shortcuts stay) |

---

## What {{APP}} can do

### Writing and organizing

- **Binder, editor, inspector**: documents and folders of any size, synopsis and notes, labels and status, keywords, custom metadata, bookmarks, snapshots, comments and footnotes (beside the text or inline).
- **Three views** of a folder: **Combined Text** (**Ctrl+1**, all its documents as one continuous text you can edit), **Corkboard** (**Ctrl+2**, index cards, freeform, label threads), and **Outliner** (**Ctrl+3**).
- **Two editors** side by side or one above the other, **copyholders** inside an editor, **Quick Reference** windows, **composition mode** (**F11**), page view, the ruler, styles, revision mode, scriptwriting (screenplay, stage play, comic), targets, statistics, and writing history.
- **Search**: quick search, project search, **Project Replace**, and **Find by Formatting**; saved searches become collections.

### Your projects: two formats

- A **{{APP}} project** is a folder: every document is an ordinary text file, as **Markdown** (the standard) or **RTF**, so your writing stays readable without the program.
- **Import Scrivener Project…** (**File > Import**) makes a {{APP}} project from a Scrivener 3 or 2 project and leaves the original untouched.
- **Working in Scrivener format**: open the *.scrivx* file of a Scrivener 3 project and choose **Work in Scrivener Format**. {{APP}} then saves into the *.scriv* folder, so the project stays readable by Scrivener. **File > Project Format** switches a project between the two formats or saves a copy in the other one.

### Getting text in and out

- **Import**: Word (*.docx*), LibreOffice (*.odt*), RTF, plain text, Markdown, Fountain and Final Draft scripts, pictures, PDF files, audio and video. **Import and Split…** cuts a long file into documents.
- **Compile** (**Ctrl+Shift+E**): Word, PDF, and e-book built in; Fountain and Final Draft for scripts; LibreOffice, Pandoc, and Markdown formats as plugins. Compile formats, section types, front and back matter, replacements, and a preview.
- **Compare and Transfer** (**File > Compare and Transfer**): when a manuscript comes back from an editor as a Word or LibreOffice file, {{APP}} shows what differs and takes over the side you choose, chapter by chapter or change by change. The project gets snapshots first; a Word copy gets tracked changes.
- **Export** documents as files, and **print** texts, index cards, and the outline.

### Beyond the usual

- **Mind maps**: a board of notes, lines, and arrows for every document and folder (the **Mind Map** button of the inspector). **Help > Mind Map Help** shows how it works.
- **Objects**: characters, locations, environments, points of interest, weather, and categories of your own, each with a sheet of fields and links between them (the **Objects** button of the inspector, or **Window > Objects…**).
- **The icon bar**: up to ten document icons, one click each. **Documents > Change Icon** also makes icons from text or from your own pictures.
- **Zoom per part**: binder, editors, inspector parts, corkboard, outliner, and mind maps each keep their own zoom.
- **Places kept**: every document opens where you left the cursor, also after reopening the project.
- **Fonts**: **Navigate > Fonts > Install Font Files…** adds fonts without leaving the program.
- **Safety**: autosave also while you type on, backups, a spare copy of the project file, crash and freeze reports (**Help > Crash Reports…**), and **Help > System Information…**.

### Updates and add-ons

Updates and extensions come as small package files (*.qbpkg*), on the Releases page of the project page (https://github.com/Shelin-boop/Quillbinder_Public/releases) next to the AppImage. Install them with **File > Install Update or Add-on…** (or double-click the file). **Help > Add-on Manager…** switches add-ons on and off and goes back to an earlier program version if an update does not suit you. Add-ons include extra themes, project templates, compile formats, the Name Generator, the Scratch Pad, Text Statistics, audio and video playback (one package for every format, *media-all-formats.qbpkg*; or *media-playback.qbpkg* for the free formats only), and the import of web pages and old Word files.

---

## Help

- **Help > User Manual** (**F1**): every function, with the menu paths.
- **Help > Welcome Project**: a short project that shows the features listed under "Beyond the usual" and lets you try them.
- **Help > Mind Map Help**: notes, lines, arrows, and shapes, with pictures and a practice board.
- **Help > List of All Placeholders** and **Help > Keyboard Shortcuts…**
- **Help > Third-Party Licenses…**: the licenses of the parts of other projects that {{APP}} uses.
- **Help > About {{APP}}**: the version and the license. {{APP}} is free software under the Zero-Clause BSD license (0BSD).
