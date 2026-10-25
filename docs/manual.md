# {{APP}} User Manual

{{APP}} is a writing studio for long texts: novels, screenplays, plays, comic scripts, theses, and nonfiction books. You write in small pieces (scenes, sections, notes), arrange them freely, and at the end *compile* them into one manuscript.

This manual covers version {{VERSION}}. Press **F1** at any time to open it.

---

## 1. First steps

### Getting the program

{{APP}} is downloaded from the **Releases** page of its project page, https://github.com/Shelin-boop/Quillbinder_Public/releases. Each release has the program as one file (*{{APP}}-<version>-x86_64.AppImage*), a program update for those who already have an AppImage of {{APP}} (section 17), and the add-ons (*.qbpkg* files, section 17). Make the AppImage executable (right-click > **Properties** > **Permissions** > *Is executable*, or `chmod +x` in a terminal) and double-click it; **Help > Add to Application Menu** puts it into the menu of your desktop. The file *SHA256SUMS.txt* of each release lists the checksums of its files.

**{{APP}} is alpha software.** Keep backups of your projects (section 15) and a copy of your manuscripts outside the program.

### Starting a project

When {{APP}} starts without a project, the **start window** opens:

- **New Project**: choose a category and a template (for example Blank, Novel, Novel with Parts, or Short Story), type a name, choose where to save, and click **Create**. **Format** decides how the project is stored: as a {{APP}} project (the standard) or as a Scrivener 3 project (*.scriv*), so that you can also open it in Scrivener (section 14, *Working in Scrivener format*).
- **Recent Projects**: double-click a project you worked on before.
- **Open Other…**: find a project on your disk.
- **Import Scrivener Project…**: make a {{APP}} project from a Scrivener project (see section 14). To keep working on the Scrivener project itself, open its *.scrivx* file with **Open Other…** instead.

{{APP}} reopens the last project when it starts (you can switch this off under **File > Options > General**).

### The welcome project and the Quick Start Guide

The first time {{APP}} starts, it opens the **welcome project**: a short project that shows, one document each, what {{APP}} has beyond the usual (the icon bar, a zoom for every part of the window, mind maps, object sheets, the comments of a whole folder, Compare and Transfer, Scrivener projects, themes, updates and add-ons, and what protects your text). Begin with the document **Welcome** at the top of the binder; its links lead to every stop of the tour. Nothing you do in the welcome project can harm your own projects. (A project given on the command line, or a double-clicked project file, opens instead; the welcome project then comes at the next start.)

The welcome project is saved as "Welcome to {{APP}}" in your *Documents* folder. **Help > Welcome Project…** opens it again as you left it, or makes a fresh copy to start over (**Make a Fresh Copy**; the old copy goes to the desktop's trash). Delete it whenever you like; the command makes a new one.

**Help > Quick Start Guide** shows on one page where the important settings are (themes and dark mode, colors and fonts, formatting, zoom, toolbars, shortcuts, backups ...) and sums up what {{APP}} can do.

### Saving

You never need to save by hand. {{APP}} saves automatically a few seconds after you stop typing, when you switch to another program, and when you close the project. If you type on without a pause, it saves at least every 30 seconds all the same. **Ctrl+S** saves immediately if you want to be sure.

If you log out or shut the computer down while {{APP}} is open, it saves everything before it ends (no backup is made then). If it ends unexpectedly, the next start tells you so (section 21, *Crash reports*).

### Where your work lives

A project is a folder named *YourTitle*{{SUFFIX}}. Inside it, every document is an ordinary text file, so your writing stays readable even without {{APP}}. Double-click the *YourTitle*{{FILE}} file inside the folder to open the project. Do not rename or move files inside the project folder by hand.

A project should be open in only one window at a time. If it is already open in another window or on another computer (for example through a synced folder), {{APP}} warns you before opening it.

A project in **Scrivener format** is a *YourTitle.scriv* folder instead; open it with its *.scrivx* file. {{APP}} saves your changes into it, so the project stays a Scrivener project. **File > Project Format** switches a project between the two formats and saves copies in either format (section 14).

### Adding {{APP}} to the application menu

The first time you start the program file ({{APP}}.AppImage), {{APP}} offers to add itself to the application menu. After that you can start it like any other program, and double-clicking a project opens it. If you move the program file to another folder, choose **Help > Add to Application Menu** again.

---

## 2. The main window

The window has three columns:

| Area | What it does |
|------|--------------|
| **Binder** (left) | The table of contents of your whole project: every document and folder. |
| **Editor** (middle) | Shows what you selected in the binder: text, a corkboard of index cards, or an outline. |
| **Inspector** (right) | Information about the current document: synopsis, notes, bookmarks, metadata, snapshots, and comments. |

Hide the binder with **Ctrl+Alt+B** and the inspector with **Ctrl+Alt+I** to get more room. The colors and fonts of all three, and of the corkboard, outliner, and composition mode, come from the theme (**Window > Themes**) and can be changed area by area (section 16).

The **toolbar** at the top holds, from left to right: the **View** button (binder, collections, inspector, layouts), new document (with a menu for folders, templates, and import), new folder, trash, the **search** button (project search; the arrow beside it lists the other search tools: Search in Document, Project Replace, Find by Formatting), the **quick search** field, the three view buttons (text, corkboard, outliner), project bookmarks, quick reference, comments and footnotes, compile, composition mode, and the inspector button. You decide what it holds (see "Toolbars of your own" below).

The **format bar** below it holds the style, font, size, line spacing, default text formatting, the fonts window, bold, italic, underline, strikethrough, highlight, text color, alignment, lists, comments, footnotes, inline annotations, and images. Show or hide the two bars with **View > Toolbars**.

The **footer** of each editor shows the word and character count (select text to count only the selection) and the target for the document. Click the count for details.

### The menus

| Menu | What it holds |
|------|---------------|
| **File** | Projects, saving, the project format ({{APP}} or Scrivener), backups, import and export, compare and transfer with a Word or LibreOffice file, compile, printing, file conversion, updates and add-ons, options |
| **Edit** | Undo, clipboard, selecting, finding and replacing (in the text, in the project, by formatting), moving documents in the binder, writing tools, links |
| **Insert** | Images, comments, footnotes, media timestamps, auto numbers, placeholders, special characters, date and time |
| **View** | The three view modes, the split editor, binder, collections and inspector, toolbars, text editing options (page view, ruler, typewriter scrolling), corkboard and outliner options, label colors, zoom, composition mode, full screen |
| **Navigate** | Back and forward, previous and next document, opening documents in the other editor, in the copyholder, or as quick reference, the editor lock, which editor the binder loads into, the inspector tabs |
| **Project** | New documents, project settings, targets, keywords, statistics, writing history, trash |
| **Documents** | Snapshots, renaming, duplicating, splitting, merging, converting, icon, label, status, bookmarks |
| **Format** | Font, paragraph, style, lists, highlight, copying formatting, revision mode |
| **Window** | Themes, layouts, scratch pad, quick reference windows |
| **Help** | This manual, the Quick Start Guide, the welcome project, the help for mind maps, the list of all placeholders, the list of keyboard shortcuts, the Add-on Manager, the developer guide, adding {{APP}} to the application menu, system information and crash reports, the third-party licenses, About |

A submenu shows short names: **File > Import > Files**, **Documents > Split > at Selection**, **Navigate > Open > as Quick Reference**. The shortcut of a command is shown next to it and can be changed (section 19). Every entry of a menu has a letter of its own (underlined when you hold **Alt**), so **Alt** and the letters reach every command.

Without an open project, the menus **Insert**, **Navigate**, **Project**, **Documents**, and **Format** are grayed out, and so are the commands of the other menus that need a text or a binder (Find, Zoom, the editor layout, Focus Binder ...). What works without a project: creating, opening, and importing projects, converting files, installing updates and add-ons, the options, themes and layouts, the writing tools that need no project, and Help.

### Layouts

A layout remembers how the window is arranged: whether binder, collections, inspector, toolbar, and format bar are shown, how wide the columns are, how the editor is split, which view each editor uses, and which editor the binder loads into. It says nothing about documents, so every layout works in every project.

Choose a layout under **Window > Layouts** or from the **View** button of the toolbar. Seven layouts are built in:

| Layout | Arrangement |
|--------|-------------|
| **Default** | Binder, one editor, inspector |
| **Binder and Editor** | Binder and text, no inspector |
| **Editor Only** | Just the text with the two bars |
| **Outliner and Editor** | Outliner on the left, text on the right. The binder loads groups into the outliner; a click on a row opens that document on the right |
| **Corkboard and Editor** | Corkboard above, text below, connected in the same way |
| **Two Editors** | Two texts side by side |
| **Planning** | Binder, corkboard, and the inspector with synopsis and notes |

**Window > Layouts > Save Current Layout** stores the window as it is now under a name of your choice; tick "Also remember the size of the window" if the layout should resize the window. **Manage Layouts** shows all layouts with a small picture and lets you use, update (replace with the current window), rename, delete, export, and import your own layouts. Built-in layouts cannot be changed; save a changed window under a new name instead. Every layout can have a keyboard shortcut (section 19, group "Layouts").

### Toolbars of your own

**View > Toolbars > Customize Toolbars** (or a right-click on a bar) opens a window with two lists: the available commands on the left, the contents of the bar on the right. Choose the main toolbar or the format bar at the top.

- Add a command with the arrow button or a double-click. Every command of the menus is available; the search field above the list finds it.
- Remove a button with the other arrow or a double-click, and move it with the up and down buttons or by dragging.
- **Separator** and **Flexible Space** can be used as often as you like. A flexible space pushes what follows to the right; two of them center what stands between them.
- Some entries are more than one command: **View**, **Add**, **Search**, the **Quick Search field**, **View Mode**, **Project Bookmarks**, **Comments and Footnotes**, and **Layouts** for the main toolbar; the style, font, size, and line spacing lists for the format bar.
- Below the lists you choose small, medium, or large icons and whether the main toolbar shows text beside or under the icons.
- **Standard Set** puts the buttons the program came with back into the bar.

If a bar holds more than fits into the window, an arrow at its end opens the rest. An empty bar is not possible: hide the bar with **View > Toolbars** instead.

### The icon bar

The **icon bar** holds up to ten document icons you use often. A click on one of them gives that icon to the selected documents and folders (in the binder, or in the corkboard or outliner you are working in); the **↺** button after them gives them their standard icon back, and the last button chooses the icons of the bar. Show or hide the bar with **View > Toolbars > Icon Bar**.

**View > Toolbars > Choose Icons for the Icon Bar…** (also in the right-click menu of the bar) opens a window with all icons on the left, including your own project icons, and the icons of the bar on the right. Add with **Add →** or a double-click, remove with **← Remove** or a double-click, and change the order with **Up** and **Down**. **Standard Set** puts back the ten icons the program came with (Green Book, Proofread, Ticked, Unticked, Question Mark, Red Flag, Yellow Notepad, Character Sheet, Location Sheet, Warning).

---

## 3. The binder

Every project has three special folders:

- **Draft** (in some templates *Manuscript*): everything that will become your book. Only what is inside the Draft is compiled.
- **Research**: notes, character sheets, pictures, PDFs, and anything else you collect.
- **Trash**: deleted items wait here until you choose **Project > Empty Trash**.

### Working with items

- **New document**: **Ctrl+N** (or the ➕ button). It is added below the selected item.
- **New folder**: **Alt+Shift+N**.
- **Rename**: **F2**, or double-click slowly on the title.
- **Move**: drag items with the mouse, or use **Ctrl+Alt+Arrow keys**: Up and Down move within the folder; Left moves out of the folder; Right moves into the item above. Dropped *between* two items, a document goes there; dropped *onto* an item, it becomes its subdocument. If you nest documents by accident, switch off dropping onto text documents under **File > Options > Behaviors** (folders always take drops).
- **Open or close a whole branch**: hold **Alt** and click the arrow of a folder or group.
- **Delete**: press **Delete**. The item goes to the Trash.
- **Duplicate**: **Ctrl+D**.
- **Change the icon**: right-click > **Change Icon** (or **Documents > Change Icon**), or the icon bar (section 2). See "Icons" below.

A document can have subdocuments, just like a folder. Folders and documents behave the same way; folders simply look different and open as a corkboard by default.

A click in the binder loads the document into the editor. After **Back**, a document link, or with a locked editor, the editor can show another document than the one selected in the binder: a **gray bar** then marks the editor's document in the binder. **Navigate > Reveal in Binder** (**Ctrl+Shift+R**) selects it.

**Narrow binder, deep folders.** Each level of folders is indented by 12 px (half of what it used to be; change it under **File > Options > Appearance > Binder > Indentation per level**). When titles are longer than the binder is wide, a scroll bar appears at the bottom of the binder, so you can read the end of long titles without widening the binder.

### Icons

**Documents > Change Icon** (also in the right-click menu of the binder, the corkboard, and the outliner) is built like the icon menu of Scrivener 3 for Windows:

- **Reset Icon to Default**: the standard icon of a document, folder, or file comes back.
- **Icon from Text…**: makes an icon of up to three characters (for example *PR*, *Ch1*, or *A*). Choose the color of the box, of the letters, and of the border; the small picture shows the result. The icon is kept in the project and given to the selected documents at once.
- **Manage Icons…**: the icons of this project. **Add Picture…** turns pictures (PNG, SVG, JPEG and others) into icons, **From Text…** is the same as above, **Rename…** and **Delete** change the list (documents with a deleted icon get their standard icon back), **Use for Selected Documents** gives the chosen icon to the selected documents. The icons are stored in the project (folder *icons*), so they travel with it.
- **Recent**: the last five icons you chose.
- **Book**, **Characters**, **Flag**, **Locations**, **Notes**, **To Do**, **TV**, **Way-Station**: submenus with the icons of each group in several colors (for example Blue, Green, Red, and Yellow Book; seven flags; five notepads; Ticked and Unticked, Pass, Fail, and Unclear in To Do).
- Then the single icons: Back Matter, Blackboard, Calendar, Clapper Board, Clock, Cog, Eye, Figures and Graphs, Filing Cabinet, Front Matter, Information, Lectern, Light Bulb, Magnifying Glass, Music, Musical Notes, Outline, Pencil, Proofread, Question Mark, Reviewer Comments, Scissors, Speech Bubble, Speech Marks, Statistics, Sync Folder, Tables, Test Tube, Thought Bubble, and Warning.
- **Project Icons**: your own icons (when the project has some), and **More Icons (Desktop)**: the icons of the desktop theme that earlier versions offered.

**Label color in icons.** The white parts of many icons (the pages of a notepad, the cloth of a white flag, the paper of a sheet, the box of an icon made from text) take on the label color of the document. Switch this on or off with **View > Use Label Color In > Icons** (or **File > Options > Appearance > Binder**).

**Icons and Scrivener.** {{APP}}'s icons are drawn anew, but every one of them carries the name of the Scrivener icon it stands for. A project in Scrivener format (section 14) keeps these names, so Scrivener shows its own icon for them; icons made in Scrivener come back here as the matching icon. Your own icons go into the *Icons* folder of the *.scriv*. An icon this program has no picture for keeps its name and is shown as the nearest icon here.

### Labels, status, and "Include in Compile"

Right-click an item to set its **label** (a color, for example for the point-of-view character) and its **status** (for example *First Draft* or *Done*). Change the lists under **Project > Project Settings**.

A document or folder with a label shows its color in the inspector as well: the synopsis card and the document notes take a light tint of the label color (15 % of its saturation), so you see at a glance which label the item has. Items without a label, or with a gray one, keep the normal card color.

Items with **Include in Compile** switched off stay in the Draft but are left out when you compile.

### Collections

Collections are extra lists of documents, for example "Needs research" or "Scenes with Anna". Show them with the **Collections** button. Use the ➕ button above the binder to create a collection from the selected documents, or right-click documents and choose **Add to Collection**. Removing a document from a collection does not delete it. Saved searches (section 9) are also collections; they update themselves.

- **Add documents** by dragging them from the binder (or from an editor's header icon, a corkboard, an outliner) onto the tab of the collection.
- **Inside the list** of a collection, drag documents into the order you like (the binder order does not change), drop documents in at a place, and press **Delete** to take the selected documents out of the collection. Search collections cannot be changed this way.
- **Drag the tabs** to put the collections into another order.
- Documents in the list can be dragged like those in the binder, and **Space** opens the selected ones as Quick Reference panels.

---

## 4. Writing

### Text formatting

| Action | Shortcut |
|--------|----------|
| Bold / italic / underline | Ctrl+B / Ctrl+I / Ctrl+U |
| Highlight | Ctrl+Shift+H |
| Superscript / subscript | Ctrl+Shift++ / Ctrl+= |
| Align left / center / right | Ctrl+{ / Ctrl+\| / Ctrl+} |
| Fonts window | Ctrl+T |
| Font size bigger / smaller | Ctrl+> / Ctrl+< |
| Paragraph window | Ctrl+Alt+P |
| Copy formatting / paste formatting | Ctrl+Alt+C / Ctrl+Alt+V |
| Zoom in / zoom out / reset zoom | Ctrl++ / Ctrl+- / Ctrl+0 |
| Zoom the area under the mouse | Ctrl+mouse wheel |
| Show or hide the ruler | Ctrl+Alt+R |
| Paste without formatting | Ctrl+Shift+V |
| Undo / redo | Ctrl+Z / Ctrl+Shift+Z |

**Styles** (Heading 1–6, Title, Subtitle, Block Quote, and your own) are chosen from the list at the left of the format bar. Styles tell Compile what a paragraph is, so use them instead of making text large and bold by hand. See "Styles" below.

**Lists**: use the list buttons, or type "- " or "1. " at the start of a paragraph.

**Smart typography**: straight quotes become curly quotes, and two hyphens become an em dash as you type. Switch this off under **File > Options > Editing**.

**Revision mode** (**Format > Revision Mode**): everything you type is colored with one of five revision colors, so you can see what changed in an edit round. Choose **Off** to stop.

**Text color and highlight in the format bar.** The two buttons with a colored bar (a pen for highlighting, a letter *T* for the text color) work like those of Scrivener and Word: a click applies the color shown in the bar to the selected text, or, without a selection, to what you type next. Rest the mouse on a button for a moment and a small window with **red, blue, yellow, green, white, and black** opens below it, plus **No Highlight** (or **Automatic** for the text color) and **…** for any other color. A click on a color applies it and makes it the color of the button, which keeps it for the next click (also after a restart).

### Fonts

All text of a project has one look: the **default formatting** (see below). On top of it you can give any piece of text a font of its own.

- **Format bar**: the font list and the size field change the selected text at once. Without a selection they change the **whole paragraph** that holds the cursor (and what you type next there). The arrows of the size field change the text with each click; a size you type takes effect with **Enter**. The format bar, the Format menu and their shortcuts act on the text the cursor was in last: an editor, the notes in the inspector, the bookmarked text in the inspector, or a long text field of an object (there: bold, italic, underline, strikethrough, text color, and highlight). A click in the size field does not change that.
- After a change in the format bar, a bar above the editor offers the same for the whole project: in a paragraph with a style (a heading, for example) **Change the Style “Heading 1”** gives the style the new font or size, and all text of that style in every document follows; in a paragraph without a style **Use for All Text** makes it the project font or size (see "Default formatting" below). Click **×**, or simply go on writing elsewhere, to keep the change where it is. Every document that changes gets a snapshot first.
- **Format > Font > Show Fonts…** (**Ctrl+T**) opens the fonts window: font, typeface (light, regular, bold, italic, and whatever else the font offers), size, underline, strikethrough, capitals (small capitals or all capitals), letter spacing in percent, and text color. Only what you change in the window is applied, so a selection with bold and plain words keeps them. **Project Font** takes the text back to the font, size, and color of the project.
- **Format > Font > Bigger / Smaller** (**Ctrl+>** / **Ctrl+<**) change the size by one point; every part of the selection keeps its own size and grows or shrinks.
- **Format > Font > Text Color…** and **Automatic Text Color**.

Choosing the project's own font or size is stored as "no font of its own": such text follows the project when you change the default formatting later.

**Installing fonts.** **Navigate > Fonts** has the commands for fonts that are not on your computer yet:

- **Install Font Files…**: choose font files (TTF, OTF, TTC, PFB, WOFF, or a ZIP file that holds them). They are copied into your own font folder (*~/.local/share/fonts*), and the font lists of {{APP}} know them at once; other programs see them after their next start. No password is needed, and the fonts are for your user only.
- **Open Font Folder**: opens that folder in the file manager; fonts you put there yourself are found after **Reload Font List**.
- **Open Font Manager of the Desktop**: opens KDE's font manager (System Settings > Text & Fonts > Font Management) or the font program of another desktop, where you can also install fonts for all users and remove fonts.
- **Reload Font List**: looks for new fonts and names them.

### Paragraph formatting

**Format > Paragraph > Paragraph…** (**Ctrl+Alt+P**) sets, for the paragraphs that hold the cursor or the selection: alignment, left and right indent, first line indent (a negative value gives a hanging indent), space before and after, line spacing (a multiple of the line height, an exact height, or a minimum height), and tab stops (left, centered, right, decimal point). Only what you change is applied. **Reset to Default** removes the formatting you set by hand; the paragraph follows its style and the project again.

Quick ways: the alignment buttons, the line spacing list in the format bar, **Format > Paragraph > Line Spacing**, and **Increase Indent / Decrease Indent**.

Applying a paragraph style replaces the paragraph formatting set by hand; fonts set by hand stay.

**Format > Copy Formatting** (**Ctrl+Alt+C**) remembers the font and paragraph formatting at the cursor; **Paste Formatting** (**Ctrl+Alt+V**) applies it to the selection, or to the whole paragraph when nothing is selected.

### The ruler

**View > Text Editing > Show Ruler** (**Ctrl+Alt+R**) shows a ruler above the text. It belongs to the paragraph that holds the cursor.

- The marker at the top is the **first line indent**, the markers at the bottom are the **left indent** and the **right indent**. Drag them. The left indent takes the first line with it.
- **Click** in the lower half of the ruler to add a tab stop. Drag a tab stop to move it; drag it up or down off the ruler to remove it. **Right-click** a tab stop to make it left, centered, right, or decimal.
- Markers snap to quarters of a centimeter (sixteenths of an inch); hold **Shift** to place them freely.
- **Double-click** the ruler for the paragraph window. The right-click menu switches between centimeters and inches (also under **File > Options > Editing**).

Tab stops are measured from the start of the paragraph's first line. Without tab stops of its own, a paragraph has a tab position every 2.1 cm.

The ruler shows real lengths. Zoom enlarges the letters on screen but not indents and tab stops, so compare distances at 100 %.

### Default formatting

- **This project**: **Project > Project Settings > Formatting** (also the sliders button in the format bar) sets font, size, line spacing, first line indent, space after paragraphs, and justification of the text of the whole project. **Apply** changes all documents at once and leaves the window open, so you can see the result behind it; **OK** does the same and closes the window. **Use Current** takes the values from the paragraph that holds the cursor.
  - With **Change text with a font, size or spacing of its own as well** (on by default), text that was formatted by hand or came with its own font from an import follows too, as long as it has the main font, size, and spacing of its document. What differs from the main text stays as it is: a larger heading line, a passage in another font, a paragraph without indent. Switch the box off to leave all text formatted by hand alone.
  - Every document that changes gets a snapshot first (inspector, snapshots tab, "Before the project formatting changed"), so you can go back. Script documents keep their own format.
- **New projects**: **File > Options > Editing > Formatting of new projects**, with **Use Formatting in Current Editor**. Existing projects keep their formatting. The standard is 11.0 pt, line spacing 1.09, no first line indent, and 6 pt after paragraphs. Projects imported from Scrivener start with these values as well (with the font most used in the Scrivener project).
- **Back to the default**: select documents in the binder and choose **Documents > Convert > Text to Default Formatting…**. You decide whether fonts and colors, indents and spacing, alignment, and tab stops are removed. Bold, italics, styles, notes, and links always stay. A snapshot of each document is taken first (inspector, snapshots tab), so you can go back.

### Styles

A style saves formatting under a name. The text remembers the name, so when you change the style, all text that uses it changes with it, in the whole project.

There are two kinds:

- **Paragraph styles** apply to whole paragraphs. They hold paragraph formatting (alignment, indents, spacing, line spacing, tab stops) and, if you want, character formatting for the paragraph (font, size, bold, italics, color). In lists they are marked "¶", or "¶a" when they include character formatting.
- **Character styles** apply to the selected text, or to the word at the cursor. They hold character formatting only. They are marked "a".

Leave body text at **No Style**. It follows the project's default formatting, and Compile can give it any look. Use styles only for text that should look different from body text.

**Applying a style**: choose it in the list at the left of the format bar, under **Format > Style**, in the menu of a right-click on the text, or in the **Styles Panel** (**Format > Style > Styles Panel**), a small window that stays open next to the text; a double-click on a row applies the style. **No Style** removes a character style from the selection; where there is none, it removes the paragraph style.

**Creating a style**:

1. Format some text the way the style should look, and click into it or select some of it.
2. Choose **Format > Style > New Style From Selection…** (or the plus button of the Styles Panel).
3. Enter a name and choose what to save: all formatting (a paragraph style with character formatting), the paragraph formatting only, or the character attributes (a character style).
4. Decide whether the font and the font size are part of the style. Without them the style keeps the font of the text around it.
5. For paragraph styles, choose which style the next paragraph gets when you press Enter at the end.

A paragraph style stores only what differs from body text. Everything else keeps following the project's formatting, so a new default font reaches styled text as well.

**Changing a style**:

- **Format > Style > Redefine Style From Selection…** takes the formatting of the text at the cursor, as above. A paragraph style stays a paragraph style, a character style a character style.
- **Format > Style > Edit Style** (or the pencil button of the Styles Panel) opens a window with the name, **Font…**, **Paragraph…**, and the style of the following paragraph. A preview shows the result.

Afterwards {{APP}} goes through the whole project and updates the text. This cannot be undone with **Edit > Undo**; redefine the style again, or use **Reset to the Built-in Definition** in the Styles Panel for a built-in style.

**Renaming and deleting**: rename a style in **Edit Style**. The headings, Title, Subtitle, Block Quote, Centered, and Caption keep their names, because Compile and the Markdown files know them by name. When you delete a style (**Format > Style > Delete Style**), text that used it keeps its look as formatting set by hand.

**Other projects**: **Format > Style > Import Styles…** reads the styles of another project. **Use These Styles in New Projects** makes the styles of the open project the starting set of every new project; **File > Options > Editing** goes back to the built-in styles.

In the Styles Panel, the arrow buttons change the order of the styles in all lists.

### Markdown or RTF: how documents are stored

Each project stores its documents in one of two formats. You choose when you create or import a project, and you can switch later under **Project > Project Settings > Formatting**.

- **Markdown** (recommended): plain text files that any program can open, now and in fifty years. The look of the text comes from the default formatting. Fonts and paragraph formatting that you set by hand are written into the text as small markers, only where you used them (section 18); everything else stays plain.
- **RTF**: rich text files like a word processor. Every paragraph carries its complete formatting, so a change of the default formatting does not reach existing text.

Switching converts every document. From RTF to Markdown, fonts, colors, alignment, left and right indents, and tab stops are kept. Line spacing, first line indent, and paragraph spacing of text that came from other programs follow the project afterwards. Take a backup first (**File > Back Up > Back Up Now**).

Text pasted from other programs into a Markdown project takes the look of the text around it (bold, italics, and links stay). Text copied inside {{APP}} keeps its fonts.

To convert a single file outside a project, use **File > Convert Markdown ⇄ RTF File…**.

### Comments and footnotes

- **Comment** (**Shift+F2**): select some text and add a comment. The text is highlighted and the comment appears in the inspector.
- **Footnote** (**Ctrl+Shift+F2**): select the word the footnote belongs to; write the footnote in the inspector.
- **Inline annotation** (**Ctrl+Shift+A**): a note written directly in the text, shown in color inside a bubble. Annotations are removed when you compile (unless you choose to export them as comments).
- **Inline footnote** (**Ctrl+Alt+F**): a footnote written directly in the text.

To change the color of an inline annotation, select it (**Edit > Select > Select Annotation or Note** selects the whole note at the cursor) and choose a text color; the annotation keeps that color. **Format > Font > Automatic Text Color** gives it the usual red again.

Click a comment in the inspector to jump to it. Right-click it to change its color, convert it into another kind of note, or delete it.

A click on commented text opens the inspector with that comment. **Alt+click** opens the comment or footnote in a small window at the mouse instead, where you can read and change it without the inspector; click elsewhere or press **Esc** to close it.

### Images and links

- **Insert > Image From File…** puts a picture into the text; you can also drag a picture into the text or paste one. Right-click the picture and choose **Image Size…** to change how large it appears.
- **Edit > Link to Document…** (**Ctrl+L**) links the selected text to another document in the project. Clicking the link opens that document (in the other editor, the same editor, its copyholder, or a Quick Reference panel; see **File > Options > Behaviors**).
- **Edit > Insert Web Link…** links to a web page.

### Find and replace

- **Ctrl+F** (**Edit > Find > Find**) opens the find bar above the text. It shows how many matches there are and highlights them; **F3** and **Shift+F3** go to the next and previous match. **Match case** and **Whole words** narrow the search.
- **Ctrl+H** adds the replace row: **Replace** changes the selected match and goes to the next one, **Replace All** changes every match in the text you see (one step for **Undo**). The new text takes over the formatting of the text it replaces, and comments stay attached.
- **Ctrl+E** (**Use Selection for Find**) puts the selected text into the find bar.
- In the combined text, find and replace work through all the documents shown.
- While the keyboard is in a **copyholder**, Ctrl+F searches the copyholder's document.
- **Ctrl+Shift+F** searches the whole project, **Edit > Find > Project Replace** replaces in the whole project, and **Edit > Find > Find by Formatting** looks for highlights, comments, colors, and styles (all in section 9).
- On the corkboard and in the outliner, Ctrl+F opens the **filter bar** instead (section 5).

### Spelling

Misspelled words get a red wavy underline; right-click for suggestions or to add the word to your dictionary ("Learn Spelling"). The program's own name and terms ({{APP}}, outliner, subdocument, copyholder) always count as correct. Choose the language under **File > Options > Corrections** (page Spelling); there you also choose the underline (waves, dots, or a straight line), whether words with numbers ("Route66") are checked, and **View Personal Word List...** shows the words you added and removes the ones you added by mistake. Dictionaries come from your system. On Ubuntu and Kubuntu, English is installed; for other languages install the matching *hunspell* package in Discover (for example "hunspell-de-de" for German).

### Special insertions

The **Insert** menu also offers the current date and time, special characters (em dash, en dash, ellipsis, nonbreaking space, section sign, bullet), **auto numbers**, and **placeholders**, which Compile replaces (section 13).

### Corrections while typing

**File > Options > Corrections** sets what is corrected while you type. A word is corrected when you finish it, that is when you type a space, a Return, or a punctuation mark after it; **Ctrl+Z** right after takes back the correction alone.

- **Fix capitalization of sentences**: the first word of a sentence gets a capital letter (not after abbreviations such as "Dr." or "e.g."). **Capitalize "i"**: "i" and "i'm" become "I" and "I'm". Both are off at first.
- **Smart quotes** in the style you choose (“English”, „German“, »German«, «French», ”Swedish”); an apostrophe inside a word is always ’. Two hyphens become an em dash (—); three periods can become one ellipsis character (off at first). In script mode these can be switched off.
- **Data detection**: a typed web address becomes a link; **[[Title]]** becomes a link to the document with that title, and if there is none, a new empty document of that name is made next to the text (Scrivener's "document links").
- **Additional substitutions**: a list of what you type and what it becomes, for example "(c)" and ©, "->" and →. **Edit Substitutions...** changes the list; **Standard Substitutions** brings back the list the program comes with.
- **Auto-completion**: in script mode, the list of character names and scene headings offered while you type.

### Zoom, typewriter scrolling, and fixed width

- **Ctrl+mouse wheel** zooms the area under the mouse, wherever it is: the editors, the binder, the inspector, the outliner, the corkboard (cards and their text together), a mind map, composition mode, and windows of their own (Quick Reference, the writing tools); the keyboard can be somewhere else. Each area keeps its zoom until you change it again.
- **Ctrl++ / Ctrl+-** or the zoom box in the footer enlarge the text on screen without changing the font size. **View > Reset Zoom** (**Ctrl+0**) goes back to the default text zoom.
- **File > Options > Editing** sets the zoom every area starts with ("Default text zoom", for each area of the window and each part with a zoom of its own: the synopsis, the notes, the bookmark lists, the bookmarked text, the snapshot list and text, the object list and sheet in the inspector and in the Objects window, new mind maps) and whether the editors reopen with the zoom they had last ("Reload the main editors' last selected zoom").
- **Where you were.** {{APP}} remembers, with the project, the place of the cursor and the part of the text on screen for every document: shown again (from the binder, a link, Back and Forward, in the copyholder, a Quick Reference panel, or after reopening the project), a document opens where you left it. A folder in the combined text remembers which of its documents had the cursor. A search result or a link that points to a certain place goes there instead. Pictures keep their zoom and PDF files their zoom and page.
- **Typewriter scrolling** (**View > Text Editing**) keeps the line you are typing in the middle of the screen.
- **Fixed width editor** shows the text as a centered column of comfortable width (500 px at 100 % unless you change it), however wide the window is. Switch it off, or change the width, under **File > Options > Appearance > Main Editor** ("Use fixed width editor", "Default editor width"). The column's paper is #d1d495 and the area beside it #999999 as standard ("Paper" and "Beside the text column" among the colors of the Main Editor); they stay the same when you choose another theme, until you change or reset them.

### Page view

**View > Text Editing > Page View** (also in the menu of the **View** toolbar button) shows your text on sheets of paper instead of one endless column. The footer then shows the page of the cursor and the number of pages, for example "Page 3 of 12". Choose the command again to return to the column.

The pages are a way of looking at your text, nothing more. They do not show what a compiled or printed manuscript will look like: you write in pieces, and titles, fonts, and page size are decided when you compile (section 13). Nothing about the pages is stored in your documents.

- **View > Text Editing > Page Size and Margins…** sets the paper (A4, A5, US Letter, and others, or a size of your own), portrait or landscape, and the four margins. The setting belongs to the project; tick **Use this page for new projects as well** to make it your standard.
- Zoom enlarges sheets, margins, and letters together. A sheet that is wider than the editor can be scrolled sideways. As in the column view, indents and tab stops keep their real length, so line breaks can differ a little between zoom levels.
- In the combined text, every document starts on a sheet of its own, and its last sheet ends just below the text, so short documents do not fill the screen with empty paper.
- Page view applies to everything that shows your texts in the main window and in quick reference windows: both editors and the copyholders. Notes keep their plain look, and composition mode has its own paper.
- The color around the sheets is **Around the pages (page view)** under **File > Options > Appearance > Main Editor**.

### Scriptwriting

Screenplays, stage plays, and comic scripts are written in **script mode**. A script is made of *elements*: scene heading, action, character, parenthetical, dialogue, transition, and so on. Each element has its place on the page, and in script mode the **Tab** and **Enter** keys take you from one element to the next, so you never set an indent by hand.

**Switching it on.** Click into a document and choose **Format > Scriptwriting > Script Mode** (**Ctrl+8**). The menu entry names the script format of the project, for example *Script Mode - Screenplay*. Script mode belongs to the document: scripts and ordinary texts live side by side in one project, and scripts have a **yellow icon** in the binder. Choose the command again to switch it off; the text keeps its look. A new document created among scripts (in a folder whose texts are all scripts) is a script as well.

**The footer.** In a script, the footer of the editor shows, in place of the word count, what **Tab** and **Enter** will do at the cursor, and on the right a pop-up menu with the element of the current paragraph. Choose another element from that menu and the paragraph is reformatted. Text that is not an element of the script is shown as **General Text**.

**Tab and Enter.**

- **Enter** at the end of a paragraph starts the element that usually follows: action after a scene heading, dialogue after a character name, and so on.
- **Enter** on an empty line opens the list of elements at the cursor; press the letter that is underlined in the list (S for scene heading, C for character ...). **Format > Scriptwriting > Show the List of Elements** (**Ctrl+\\**) opens the same list at any time.
- **Tab** on an empty line changes the line to another element; press it again for the next one, **Shift+Tab** to go back.
- **Tab** at the end of a paragraph does what suits the element: after a character name or a line of dialogue it starts a parenthetical, and in a scene heading it types " - ", ready for the time of day.
- **Enter** in the middle of a paragraph splits it; both halves keep the element. **Shift+Enter** is a line break inside the paragraph.
- Elements written in capitals (scene headings, character names, transitions) turn what you type into capitals. Parentheses and prefixes such as "SFX: " are typed for you.

**Auto-completion.** While you type, a small list offers what fits the element: INT. and EXT. at the start of a scene heading, the times of day after its " - ", the places and character names already used in the document, and stock transitions such as CUT TO:. **Enter** or **Tab** takes the marked entry, the arrow keys choose another, **Esc** closes the list.

**Changing elements.** **Format > Scriptwriting > Change Element To** changes the paragraph with the cursor, or all selected paragraphs. Choosing an element there in an ordinary document makes the document a script. Every element is a command, so you can give your favorites a shortcut under **File > Options > Keyboard**.

**Script formats.** A project uses one script format for all its scripts. **Format > Scriptwriting > Script Format** lists *Screenplay*, *Stage Play*, and *Comic Script*, the formats of add-ons, and your own. When you choose another format, the scripts of the project get the new layout: every element becomes the element of the same kind in the new format (action becomes a stage direction, for example). An element without a counterpart becomes general text that keeps its look; the question before the change names these elements.

**Script Settings.** **Format > Scriptwriting > Script Settings…** changes the format of the project or builds a new one:

- the name of the format, its font (by default the typewriter font of your system: Courier Prime, Courier New, or a similar font) and size, and whether lengths are in inches or centimeters;
- the list of elements: add, remove, and reorder them (the order is the order of the list of elements and of **Tab** on an empty line);
- for each element: name, kind (what it is to other programs: scene heading, action, character ...), and its letter in the list; bold, italic, underlined, capitals, and color; alignment, indents from both margins, empty lines before it, and line spacing; what **Enter** and **Tab** do; a text that is typed before and after the paragraph; and the lists for auto-completion;
- a preview of all elements.

A renamed element keeps its paragraphs in every script of the project. The **Manage** button starts from another format, loads or saves a format file (*.qbscript*), and saves the format for use in other projects.

**Good to know.**

- The keys and the lists work the same in the combined text, in the second editor, in copyholders, in Quick Reference panels, and in composition mode. Only the editors of the main window have the footer with the hints; elsewhere **Ctrl+\\** shows the list of elements.
- Page view (above) shows a script on sheets; with the usual typewriter font at 12 points, a page of a screenplay is roughly a minute of film, but the pages on screen are not the pages of the compiled file.
- The font and the indents of the elements belong to the script format, not to the document. The files of your scripts contain only the names of the elements (section 18).
- Getting scripts in and out, in the formats of other screenwriting programs: section 14. Compiling a whole script: section 13.
- Scripts need version 0.10 or later. A project that contains scripts says so in its project file, and from this version on {{APP}} asks before it opens a project that needs a newer version than the one running. Versions 0.9 and older do not ask: they show scripts as ordinary text and lose the elements of every script you look at there.

---

## 5. Three ways to look at your project

Choose the view with the three toolbar buttons or with **Ctrl+1**, **Ctrl+2**, and **Ctrl+3**.

### Combined Text (Ctrl+1): many documents as one text

Select several documents in the binder (Ctrl+click or Shift+click) or a folder, and press **Ctrl+1**. All the texts appear one after another, separated by a thin dashed line, and you can write in all of them as if they were one document. Changes go into the right documents automatically. Show the document titles between them with **View > Text Editing > Show Titles in Combined Text**.

### Corkboard (Ctrl+2): index cards

Each document is an index card showing its title and synopsis; a document without a synopsis shows the first lines of its text instead (faded, in italics). The card of a picture shows the picture, and so does the card of a document whose synopsis is a picture (section 7). Drag cards to reorder the documents. Double-click a card to edit its title or synopsis. Press **Enter** to open the selected cards as text.

In the corkboard footer:

- **Grid** or **freeform**: in freeform mode you can place cards anywhere, like on a real corkboard. **Commit Order** then reorders the documents in the binder to match the cards from left to right, top to bottom.
- **Label threads**: arranges the cards in rows by label.
- **Card size** of the cards (**Ctrl+mouse wheel** over the corkboard zooms the cards and their text together, like the other areas, see section 4).

A double-click on an empty place of the corkboard makes a new card (**File > Options > Behaviors > Double-Clicking**). When you edit a synopsis on a card, **Return** ends the editing and **Shift+Return** makes a new line (**File > Options > Behaviors > Return Key**, where Return can also be set to make a new card or outliner row after the selected one).

Status stamps, freeform mode, and the arrangement by label are also under **View > Corkboard Options**; label tints for cards and binder under **View > Use Label Color In**. The columns of the outliner are also listed under **View > Outliner Options**.

### Outliner (Ctrl+3): a table

Shows the documents as a table with title, synopsis, label, status, word counts, targets, dates, and your custom metadata. Click the column button in the footer to choose columns. Click a column header to sort (for viewing only; the binder order does not change). Drag rows to reorder documents.

### Filtering the corkboard and the outliner

While the corkboard or the outliner has the keyboard, **Ctrl+F** (**Edit > Find > Find**) opens a **filter bar** at the top of the editor. As you type, only the cards or rows that match stay; the bar says how many ("2 of 9"). In the outliner, the groups of matching documents stay so that you see where they belong.

- Click the **triangle** left of the field for the options: where to look (**Titles**, **Synopses**, **Keywords**, **Text**), a **label**, a **status**, a **keyword**, whether the document is **included in compile**, and **Show what does not match**. Several choices together narrow the list further.
- The filter stays while you open other folders, so you can walk through the project looking only at, say, one character's scenes.
- **Done** removes the filter and shows everything again.

Dragging cards still reorders the documents: a card dropped between two cards you see lands beside them in the binder. **Commit Order** of the freeform corkboard is switched off while a filter hides cards.

---

## 6. Two editors, bookmarks, and quick reference

### Split editor

Click the split button at the right of the editor header to show a second editor. Alt+click splits in the other direction. **View > Editor Layout** offers the same choices. **F6** jumps between the editors. Clicking in the binder shows the document in the editor you used last; **Navigate > Binder Selection Affects** changes that to the left or top editor, the right or bottom editor, or the other editor, whichever one you are writing in.

In a corkboard or outliner, the footer button **Selection affects the other editor** (also in **View > Corkboard Options** and **Outliner Options**) makes the other editor show the card you click. This is a quick way to browse scenes while keeping the chapter overview.

**Navigate > Editor > Lock in Place** keeps an editor on its document, even when you click elsewhere in the binder.

### Copyholders

Each editor has a **copyholder**: a second, plain view inside the editor that holds one other document for reference, such as notes or a character sheet. With two editors and their copyholders, you can see four documents at once.

- **To open it**, hold **Alt** and drag a document from the binder, a corkboard, or an outliner onto the header bar of the editor. Without Alt, the drop loads the document into the editor itself. **Navigate > Open > in Copyholder** and the right-click menu of a document do the same for the selected document.
- The copyholder has a narrow header bar with the title and no footer. It shows the text of one document (or a picture or PDF); it has no corkboard, outliner, or combined text. You can write and format in it as in any editor.
- **To move it**, right-click its header bar and choose left, right, top, or bottom. The first time, it opens beside a wide editor and below a narrow one; after that, where you last put it. Drag the line between editor and copyholder to change their sizes.
- **To show another document**, drop it on the copyholder's header bar, or Alt+drag it onto the editor's header bar again.
- **To close it**, click the **X** at the right of its header bar, or choose **Navigate > Editor > Close Copyholder**. The button to the left of the X closes it and opens the document as a quick reference window instead.

**Navigating with a copyholder.** While an editor shows a corkboard or an outliner and has its copyholder open, the **Selection affects** button in the footer has three positions. The first click makes your selection open in the other editor, as usual. The second click turns the arrow so that it points into the box: now the card or row you click opens in the copyholder. A third click switches it off. A useful arrangement: the manuscript in the left editor, an outliner of your research in the right editor with its copyholder below, and **Navigate > Binder Selection Affects > Left or Top Editor**. The binder then drives the manuscript, and the outliner drives the copyholder.

The copyholder, its place, and its document are remembered with the project.

### The editor header

The header above each editor shows the document title and holds the **back** and **forward** buttons (**Ctrl+[** and **Ctrl+]**), a menu of the document's sections, and buttons for the previous and next document (**Alt+Shift+Up / Down**).

- **History**: each editor remembers the documents it has shown. Hold the back or forward button down (or right-click it) for a list and pick a document to go straight to it; **Navigate > Back To** and **Forward To** show the same lists. The history is kept with the project.
- **The icon** beside the title stands for the document. Click it for a menu (Lock in Place, Reveal in Binder ...). Drag it like a document in the binder: onto the other editor's header bar, into the binder, onto a collection tab, onto the Quick Reference or Bookmarks button of the toolbar, or onto text to make a link.
- **Navigate > Go To** lists the whole binder as a menu and loads the chosen document into the active editor. **Navigate > Enclosing Group** shows the folder or group the document belongs to, with the document selected there.
- A **double click on the divider** between two editors makes them the same size.

### Dragging and dropping at a glance

| Drag ... | onto ... | and ... |
|---|---|---|
| a document (binder, collection, corkboard, outliner, header icon, bookmark) | an editor's header bar | it is shown in that editor; with **Alt**: in its copyholder |
| | text in an editor | a link to the document; on selected text, that text becomes the link |
| | text, with **Alt** held down | the text of the document is inserted |
| a picture from the binder | text in an editor | the picture is inserted |
| a document | the **Quick Reference** button of the toolbar | each document opens in a panel of its own |
| | the **Bookmarks** button of the toolbar | it is added to the project bookmarks |
| | a collection tab, a bookmark list | it is added |
| | the header bar of a Quick Reference panel | it is shown in that panel |
| | the file manager or the desktop | it is saved there as a file (see "Exporting documents") |
| selected text | the binder | a new document with that text; the text stays where it was |
| files from the file manager | the binder or a corkboard | they are imported |
| a web link from a browser | a folder in the binder, the corkboard, the outliner | the page is imported (add-on Web Page Import, section 14) |
| a picture file, or a picture from the binder | the index card in the inspector (picture mode) | it becomes the picture of the card |
| keywords from **Project > Project Keywords** | a document in the binder, a collection, the corkboard, the outliner; the keyword list of the inspector | they are assigned; with **Alt**: together with the group they belong to |

Some desktops use Alt+drag to move windows. If Alt+drag does not arrive in the program, change the modifier key for moving windows in the settings of your desktop (KDE: System Settings, window behavior; the exact place differs between versions), or use the menu commands instead (**Navigate > Open > in Copyholder**, **Project > Project Keywords > Assign to Selected Documents**).

### Quick Reference panels

A Quick Reference panel is a document in a window of its own, for example to keep a character sheet in view while you write. Text in a panel can be edited and formatted like in the editor (the shortcuts of the Format and Insert menus work there).

**To open panels:**

- **Navigate > Open > as Quick Reference**, or the Quick Reference button of the toolbar, for the selected documents.
- Press **Space** in the binder, in a collection or search result list, on the corkboard, or in the outliner.
- Drag documents onto the **Quick Reference button** of the toolbar: each one opens in a panel of its own, and the editor keeps its document.
- **Navigate > Open Quick Reference** shows the whole binder as a menu, so you can open a document without selecting it first.

**In a panel:**

- The **title** in the header bar can be edited (it renames the document). The **icon** beside it opens a menu (Reveal in Binder, Open in the Editor, add to or remove from the project bookmarks) and can be dragged like the document itself. Drop a document on the header bar to show it in this panel.
- The button at the **top right** shows the **synopsis**, the **notes**, the **keywords**, or the **metadata** of the document below its text. What you change there changes the document.
- The **footer bar** shows the word count and lets you set **label** and **status**.
- The **bookmark button** at the left of the footer bar opens the **project bookmarks** beside the document. A click on a bookmarked document shows it in the panel, so one panel is enough to move between your notes. Drag bookmarks into another order, drop documents in, and remove a bookmark with the ➖ button (the document stays in the binder). Files and web links open with a double click.
- The ➕ button below the bookmarks creates a **new document** and bookmarks it. The first time, you are asked for the folder (for example a "Notes" folder in Research); tick **Don't ask again** to use that folder from then on. Change it under **Project > Project Settings > Special Folders**.
- The **Float** button keeps the panel above the main window; **Window > Float Quick Reference Panels** does that for all of them.

The Window menu lists the open panels and closes them all at once. The panels that are open when you close a project come back when you open it again, with their place, size, lower pane, and sidebar (switch this off under **File > Options > Behaviors**).

### Audio and video

Audio and video files in the binder (for example interviews in the Research folder) play inside {{APP}} when the add-on **Media Playback** is installed (section 17; it comes with update 0.12). Without it, such a file shows **Open with Default Application**, as before.

**Playing.** Click the file in the binder. A video shows its picture; an audio file shows its title, artist, and length. The **footer bar** of the editor has the play controls:

- the **pause and rewind** button at the left (see below),
- **go to the start**, **skip back**, **play / pause**, **skip forward**,
- the **position**: drag the slider, or click beside its handle,
- the time, the **speed** (0.5× to 2×; slower speech is easier to type; the voice gets lower or higher with the speed, like a tape), and the **volume** (click: mute; the arrow: a slider).

A click on the picture of a video plays or pauses it; a right-click offers **Open with Default Application** as well. With the keyboard in the player: **Space** plays or pauses, the **left** and **right arrow keys** skip, **Home** goes to the start.

**Transcribing.** Split the editor, show the audio file in one editor and your transcript in the other, and keep typing: the commands of **Navigate > Media** work wherever the keyboard is, also in Quick Reference panels and composition mode.

| Command | Shortcut |
|---------|----------|
| Play / Pause | Ctrl+Return |
| Skip Back / Skip Forward (5 seconds) | Ctrl+Alt+, / Ctrl+Alt+. |
| Insert Timestamp (also **Insert > Media Timestamp**) | Ctrl+Shift+Return |
| Stop, Go to the Start, Pause and Rewind, Faster, Slower, Normal Speed, Volume Up, Volume Down, Mute, Go to Timestamp at Cursor | no shortcut (give them one under **File > Options > Keyboard**) |

The commands act on the file you opened or used last (in either editor, a copyholder, or a Quick Reference panel).

**Pause and rewind.** Switched on (the button looks pressed), every pause goes back a little, by 2 seconds unless you set another value under **File > Options > Behaviors > Playback**. When you press Ctrl+Return to type what you heard, you hear the last words again when you go on.

**Timestamps.** **Insert Timestamp** types the time of the player at the cursor, such as `[03:07]` (with hours from one hour on: `[1:02:03]`). **Go to Timestamp at Cursor** does the opposite: it takes the timestamp in the line of the cursor (the nearest one before or around it) and moves the player there. Timestamps are ordinary text; they can be written by hand as well (`3:07`, `[03:07]`, `1:02:03.5`).

**In a copyholder or a Quick Reference panel** the player has its play controls at its bottom. An audio file in the copyholder of the transcript's editor is a good arrangement for one editor only.

**Where it was.** When you open an audio or video file again, it continues where you paused it (switch this off under **File > Options > Behaviors > Playback**). Showing another document in the editor stops the file.

**What plays.** The add-on *Media Playback* (1.1) plays formats that are free to use: MP3, Ogg Vorbis, Opus, FLAC, ALAC, WAV, AIFF and WavPack audio; WebM, MKV and Ogg video with VP8, VP9 or Theora pictures (also MJPEG and other free formats). The add-on brings everything it needs.

**More formats.** H.264, H.265 (HEVC), AAC (M4A), AC-3, E-AC-3, DTS, MPEG-1/2/4 video, VC-1 and WMV, WMA and AMR are licensed under patents in some countries; they come with the separate add-on **Extra Video and Audio Formats** (a download of its own on the Releases page, installed like every add-on, section 17). While it is installed and switched on, *Media Playback* uses it, and MP4/M4V/MOV, AVI, MPEG, WMV, FLV and 3GP files play as well (not WMA Pro and WMV 9). Without it, such a file shows a message under the player that names its formats and this add-on; everything else works as before. AV1 video does not play with the built-in decoding.

**File > Options > Behaviors > Playback > Decoding** can switch to the **GStreamer** of the system instead, which plays what the codec packages of your system play (restart {{APP}} after changing it).

### Bookmarks

**Document bookmarks** (inspector, bookmark tab) link a document to other documents, files on your disk, or web pages. **Project bookmarks** are available from every document, from the bookmark button in the toolbar, and from the sidebar of Quick Reference panels. The bookmark tab of the inspector shows both lists, one above the other: **Document Bookmarks** and **Project Bookmarks**, each with its own ➕ and ➖ buttons. A click on a bookmark shows the document below the lists; a document that the editor shows already appears there as a read-only copy (write in the editor), so that the editor keeps its line width. A double-click opens the bookmark. Drag documents, files, or web links into the bookmark list, or use the ➕ button; a link dragged from a web browser keeps the title of the page. Drag a bookmark up or down to change the order. Documents dropped on the **Bookmarks button** of the toolbar become project bookmarks; so do the selected documents with **Documents > Add to Project Bookmarks**; the inspector then shows its bookmark tab with the new entry selected.

---

## 7. The inspector

The buttons at the top of the inspector switch between:

- **Synopsis and notes**: the synopsis is a short summary that appears on the index card. You can use a picture instead of text: click the picture button of the card and drop an image file or a picture from the binder on the gray area (or double-click the area to choose a file; right-click to remove the picture). Below it are the **document notes** (or, switched with the small menu, the **project notes**, shared by the whole project).
- **Bookmarks**: the bookmarks of the document and those of the project. A click on a bookmark of a document of the project shows its text below the lists. You can write and format there as in an editor (the format bar, the Format menu and their shortcuts act on it while the cursor is in it): everything goes into the document and its file at once, and an editor that shows the same document shows the change at once. The text there has a zoom of its own.
- **Mind Map**: a board of notes for the document or folder (see "Mind maps" below).
- **Objects**: the characters, locations, environments, points of interest, weather, and other things of your story, each with a sheet of its own and links between them (see "Objects" below).
- **Metadata**: label, status, section type, include in compile, created and modified dates, **custom metadata**, and **keywords**.
- **Snapshots** (section 11).
- **Comments and footnotes**.

**The inspector's own zoom.** **Ctrl+mouse wheel** over a part of the inspector makes that part larger or smaller, without changing the editors. The tabs with two parts have a zoom for each: **Synopsis and notes** (the card, the notes), **Bookmarks** (the two lists, the bookmarked text below them), **Snapshots** (the list, the text of the snapshot), **Objects** (the category buttons with the list, the sheet). The **Zoom** list at the bottom shows and sets the zoom of the part with the keyboard, or of the part you zoomed last; its title names the part ("Zoom: Notes"), and on the Mind Map tab it is the zoom of the board. The other tabs (Metadata, Comments) and the rows of buttons at the top and the bottom share one zoom; with **View > Zoom Inspector Tabs Separately** (also in the right-click menu of the Zoom list) Metadata and Comments keep one each.

**The inspector remembers** with the project which tab was open, where you put the dividers inside the tabs, the bookmark shown, and the object shown on the Objects tab.

**Comments of a folder.** With a folder in the editor (as a corkboard or an outliner), the comments and footnotes tab lists the comments and footnotes of every document inside the folder and its subfolders, grouped by document. A click on the name of a document opens it; a click on **Comment ▸** (or **Footnote ▸**) opens the document and shows that place; the text of a comment or footnote can be changed right there. In the combined text (several documents as one text), the tab lists the notes of all documents shown.

### Mind maps

Every document and folder can have a **mind map**: a board on which you put notes anywhere, connect them with lines and arrows, color them, and group them on background shapes. Boards can be saved as Scapple files, and Scapple files can be read (see *Files and Scapple* below). Click the **Mind Map** button of the inspector (between Bookmarks and Metadata). The button is underlined when the document has a mind map.

**In the inspector** the board fills the tab. The small bar above it has **New Note**, **New Background Shape**, **Zoom to Fit**, **Menu** (all commands: File, Edit, View, Notes, Format), and **Open in Window**. The bar below it counts the notes and has the zoom slider. **Open in Window** shows the same board in a large window with the same menus in its menu bar and an inspector on the right (**Note Format**, **Note Styles**, **Document Settings**); changes in one place show at once in the other. A right-click on the board opens the commands for what is under the mouse.

The commands of a mind map are in these menus of its own, not in the menus of the main window. Below, **Menu > Notes > Connect** means: the **Menu** button of the inspector's board, then **Notes**, then **Connect** (in the large window: **Notes > Connect** of its menu bar). While the board has the keyboard, its keys come first: **Ctrl+Z** undoes a change of the board, **Ctrl+F** finds in its notes, **Ctrl+P** prints it.

**Help for mind maps.** **Help > Mind Map Help** (also the **?** button at the right end of the board's bar, and **Menu > Help** of the board) shows a short guide with pictures: how to make notes, how to draw a line (drag a note onto another), how to draw an arrow (hold **Alt** while you let go; **Ctrl+Alt** for an arrow back, **Ctrl+Alt+Shift** for both ends), words on a line, and background shapes. Its button **Open Example Board** (also **Menu > Help > Open Example Board**) opens a practice board in a window of its own, with notes to drag onto each other and finished examples of lines and arrows. Nothing on the example board belongs to your project, and it is not saved.

**Notes.**

- **Double-click** an empty place: a new note, ready for typing. **Ctrl+Enter** makes a new note below the selected one (stacked). With **Ctrl** at the double-click, the new note is connected to the selected notes; with **Alt**, by an arrow towards it; with **Ctrl+Alt**, by an arrow back.
- **Double-click** a note to change its text; **Esc** or a click outside ends the editing. A short note becomes as narrow as its text; a long one wraps at the width of new notes (both in **Menu > File > Mind Map Options…**).
- **Drag** notes to move them; with **Ctrl** at the drop they are copied. **Arrow keys** move the selected notes when **Menu > Edit > Arrow Keys Move Notes** (**M**) is on (with **Shift** by 10 px); otherwise they select the next note in that direction. **Enter** or **F2** edits the selected note, **Delete** removes the selected notes.
- **Drag a note onto another** to connect the two with a line; again to remove the line. With **Alt** the line gets an arrow towards the note dropped on; with **Ctrl+Alt** an arrow back; with **Ctrl+Alt+Shift** arrows at both ends.
- **Double-click a line** to put a new note into it, or a line's label to change it (**Menu > Notes > Connection Label…**).
- **Menu > Notes > Connect** (**Ctrl+.**), **Connect With Arrow** (**Ctrl+Alt+.**), **Remove Arrows** (**Ctrl+,**), and **Disconnect** work on the selected notes: the first one you selected is the center.
- **Menu > Notes > New Connected Note** makes a note left of, right of, above, or below the selected one, connected to it (with or without an arrow).
- **Menu > Notes > Split** (**Ctrl+Shift+K**) divides a note at the cursor; **Merge** joins the selected notes.
- **Menu > Notes > Stack** (**Ctrl+'**), **Align**, **Distribute**, **Make Same Width / Height**, **Auto-Fit**, **Bring to Front**, **Send to Back**.
- **Menu > Edit > Add Link…** (**Ctrl+K**) links text of a note to a web page or a file; documents dragged from the binder onto the board become notes with links to them. A click on a link opens it: a document opens as document links in the text do (**File > Options > Behaviors**).
- **Menu > Notes > Add Notes to the Binder** turns the selected notes into documents inside the document or folder the board belongs to, in the reading order of the board.

**Background shapes** (**Menu > Notes > New Background Shape**, **Ctrl+Shift+G**) are colored areas behind notes. Made with notes selected, the shape surrounds them. A **magnetic** shape (**Menu > Notes > Magnetic**, **Ctrl+Shift+M**) takes the notes on it along when it is moved.

**The right-click menu of a note** (or of a background shape) changes the selected notes: **Fill Color**, **Text Color**, and **Border Color** offer the colors of your project's labels, the six colors of the format bar (red, blue, yellow, green, white, black), and **Other...** for any color, the same choice everywhere in {{APP}}; **Shape** is **Rectangle**, **Rounded Rectangle**, **Cloud**, or **Jagged** (a note without a border gets a thin one, so that the shape shows); **Border** sets the thickness; **Icons** puts up to eight icons on a note, one at each corner and one in the middle of each side, chosen from the icons of the binder (**Documents > Change Icon**), your own icons included. Right-click near a place of an icon to remove just that one; **Remove All Icons** clears them.

**The right-click menu of a line** belongs to that line only, so that other lines of the same notes are not changed: its arrows (**No Arrows**, an arrow to either note, or **Arrows at Both Ends**), **Line Color**, **Icon in the Middle** (an icon drawn in the middle of the line), the label, **Insert a Note on the Line**, and **Disconnect**. Each change can be undone on its own.

The icons, line colors, and line icons are {{APP}}'s own additions; they are kept in the board file in elements Scapple does not read (a board exported as a Scapple file has the notes, lines, and arrows without them). **File > Options > Extras > Mind Map** sets the size of the icons and the shape of new notes.

**Look.** **Menu > Format** has bold, italic, underline, strikethrough, bigger and smaller (**Ctrl+Shift+>** and **Ctrl+Shift+<**), text, fill, and border colors, border thickness (none, 1 to 5 px) and style (rounded, square, jagged, cloud), shadow, and **Fade** (**Ctrl+Shift+F**, the note is shown pale). **Note Styles** store a look under a name; **Menu > Format > Note Style** applies one (**Ctrl+Alt+1** to **9**, **Ctrl+Alt+0** for Default), makes one from the selected note, or copies and pastes the look of a note (**Alt+Shift+C** / **Alt+Shift+V**). **Document Settings** in the window's inspector set the background, the text color, the font, and the direction of the writing of the board (and **Set As Defaults** for new boards).

**Background.** A right-click on an empty place of the board (in the inspector or in the window), **Menu > Format > Background Color**, or the background button of **Document Settings** offers the colors of your project's labels, the six colors of the format bar, the default (**Default for Light Themes** or **Default for Dark Themes** in the menus, **Use Default** in Document Settings), and **Other...**. A board has a background of its own for light themes and one for dark themes: the color you choose is kept for the kind of theme in use, and the other kind keeps its own. A board without a color of its own shows the default of **File > Options > Extras > Mind Map** ("Background of boards", one for light and one for dark themes) and follows it when you change it. **Set As Defaults** in Document Settings makes the board's background the default of the kind of theme in use. A board keeps its background color also when there is nothing on it yet.

**Zoom and place.** Every board remembers, with the project, its zoom and the part of it on screen, one for the inspector and one for the large window. A board you have never zoomed opens at the default zoom of mind maps (**File > Options > Editing**, "Default text zoom").

**Selecting.** Click, **Shift+click** or **Ctrl+click** to add, drag a frame around notes, **Ctrl+A**. **Menu > Edit > Select** has notes stacked below, notes on the selected shapes (**Alt+A**), connected notes (**Ctrl+Shift+A**), and whole connected clusters; below it are **Invert Selection** (**Alt+Shift+I**) and **Deselect** (**Ctrl+D**).

**Looking around.** Zoom with **Ctrl+mouse wheel**, **Ctrl++** and **Ctrl+-**, **Actual Size** (**Ctrl+0**), **Zoom to Fit** (**Ctrl+9**), **Zoom to Selection** (**Ctrl+Shift+8**), and **Zoom to Random** (**Ctrl+Shift+R**). Hold **Space** and drag to move the board; hold **Z** to see the whole board for a moment. **Menu > Edit > Find** (**Ctrl+F**) finds and replaces text in the notes. **Menu > View > Page Guides** shows where the pages end when printing.

**Text, pictures, and files.** Paste or drop text to make a note (text with empty lines can become several notes), drop a picture for a picture note, and drop a text file to make notes of its paragraphs.

**Files and Scapple.** **Menu > File > Export** writes the board as PDF (**Ctrl+Shift+E**), PNG picture, plain text, text list, RTF, OPML outline, its pictures, or a Scapple file (*.scap*); **Menu > File > Import Mind Map or Scapple File…** reads a *.scap* file into the board. **Print** and **Print Preview** print it. **Menu > File > Prevent Editing** locks the board.

**Mind Map Options** (**Menu > File > Mind Map Options…**, and the same in **File > Options > Extras > Mind Map** of the main window): whether **Enter** ends the editing of a note (then **Shift+Enter** makes a new line), whether **Enter** on a selected note makes a new one, whether new notes take the look of the selected note, dashed lines without arrows, narrow notes for short texts, how pale faded notes are, and the size, width, color, and border of new notes and shapes.

**Where the board is kept.** A board is saved by itself, as a file in the layout of Scapple files (*content/<ID>/mindmap.scap* in the project folder; *Files/Data/<UUID>/quillbinder-mindmap.scap* in a Scrivener project, beside the document's own files). A document gets the file only when something is put on its board. Undo (**Ctrl+Z**) works for each board on its own.

{{APP}} reads Scapple files as Scapple 1.x writes them, as far as their layout is known; it was not possible to test them with Scapple itself (see the notes of version 0.17.0 in the status file).

### Objects: characters, locations, and more

**Objects** are the people, places, and things of your story, each with a sheet of fields: a character with race, height, eye color, traits, and background story; a place with its kind, its environment, and its weather; and so on. They belong to the whole project, not to one document, and are kept apart from the binder. Links connect them, so you can click your way through a background: the character sheet, the village where she lives, the landscape around it, its weather.

Click the **Objects** button of the inspector (right of the **Mind Map** button), or choose **Navigate > Inspect > Objects**. **Window > Objects…** (or the window button in the tab's bar) shows the same objects in a large window of their own, with the list on the left and the sheet on the right. Changes in one place show at once in the other.

**Categories.** The colored buttons at the top are the categories: **Characters**, **Locations**, **Environments**, **Points of Interest**, and **Weather** to start with. Click one to see its objects. You can rename them, give them other colors, change their order, remove them, and add categories of your own (factions, spells, ships ...): see *Templates and colors* below.

**Making objects.** **+** in the tab's bar makes a new object in the chosen category (in the folder of the selected object); type its name at the top of the sheet. The button with the folder makes a **folder** for sorting, for example "Villages" inside Locations; folders can hold folders. Drag objects and folders in the list to move them. **F2** or a right-click renames, and the right-click menu also has **Duplicate** and **Delete…** (also in the **≡** menu of the bar). The **Filter** field shows only the objects whose name or text contains what you type.

**The sheet.** Below the colored stripe with the category and the name are the fields of the category's template. Everything you type is saved by itself, like your documents. The kinds of fields are:

- **Short text** (one line) and **Date** (in any form you like, such as "spring 1203" or "the third winter").
- **Number** with a unit (cm, years, °C ...).
- **Long text**, which can hold links (see below), and **bold**, *italic*, underlined, struck-through, colored, and highlighted words: with the cursor in the field, the format bar, the Format menu and their shortcuts (**Ctrl+B**, **Ctrl+I**, **Ctrl+U**; also in the Objects window) act on it. Versions 0.20 and 0.21 of {{APP}} show these as well.
- **Choice**: a list to choose from; you can also type something else.
- **Yes / no**.
- **Picture**: click the area (or drop a picture on it) to choose one; it is copied into the project, made smaller if it is larger than 1600 pixels. Right-click removes it.
- **Link to objects**: the linked objects are shown as colored buttons. **+** adds one: type to search, then choose an object, or choose **New in Locations: "Ashvale"** to make a new object with that name and link it at once. A click on a linked object shows its sheet; a right-click removes the link.
- **Link to documents**: documents of the binder that belong to the object, such as "Appears in" of a character or "Scenes set here" of a place. A click opens the document in the editor.

**Color button.** The **Color** button of the stripe gives an object a color of its own (or back the category's color), for example to mark the main characters.

**Links inside long texts.** In a long text field, select a word and choose **Link to Object…** (or **Link to Document…**) in its right-click menu; without a selection the name of the object is put in as a link. A click on the link follows it; **Remove Link** in the right-click menu takes it away again. In the text of a document, **Edit > Link to Object…** puts a link to an object into the text; a click on it shows the object in the inspector. When the manuscript is compiled, these links become ordinary words.

**Going back and forth.** **‹** and **›** in the tab's bar go back and forward through the objects you looked at, like a web browser.

**Linked from.** At the bottom of every sheet, **Linked from** lists the objects that link to this one (in a link field or in a text), so the village shows who lives in it. A click goes there.

**In this document.** When the document in the editor is linked to objects (through a document link field, for example "Appears in"), the tab shows **In this document:** with those objects, and the **Objects** button is underlined like other tabs with content.

**Deleting.** Deleting an object asks first. Links to it are removed from the other objects (in texts the linked word stays, as plain text). A link to it in a document says that the object no longer exists when you click it. Deleting a folder keeps its objects; they move up one level.

**Templates and colors.** **Project > Object Templates and Colors…** (also in the **≡** menu of the tab, and in the right-click menu of a category button) edits the categories and their fields:

- On the left, the categories: **Add**, **Remove**, and the arrows for the order. Removing a category that has objects asks whether they are to be deleted as well; **No** changes nothing.
- On the right, the name and the **Color** of the category, and its fields: **Add Field**, **Remove**, **Up**, **Down**. For each field, its name and its kind; a number can have a unit, a choice its list of choices (one per line), and a link field the categories it can link to (none ticked: all).
- **Use for New Projects** keeps these categories and templates for projects you make from now on. **Standard Templates** resets a category to the template it came with, or adds the standard categories you removed.
- Removing a field from a template hides what the objects have in it, but does not delete it from their files. For a standard field, **Standard Templates > Reset This Category to the Standard** brings it back with its contents; a field of your own does not come back, so rename a field rather than remove it if you may want its contents again.

**Where objects are kept.** In the project folder, in a folder *objects*: one small file per object (*objects/characters/<ID>.json* and so on), the templates, colors, and folders in *objects/schema.json*, and the pictures in *objects/images*. The project file itself is not changed, so older versions of {{APP}} open the project as before and leave the objects alone. A project in Scrivener format keeps its objects in its working copy (section 14); Scrivener does not know them. Backups include them.

---

## 8. Organizing your project

### Keywords

Keywords are tags, for example the characters or places in a scene. Add them in the inspector or manage all of them with **Project > Project Keywords** (**Ctrl+Alt+K**), where you can give them colors, assign them to the selected documents, and find all documents with a keyword. You can also **drag keywords** from that window onto documents in the binder, a collection, the corkboard, or the outliner (if the document is one of several selected ones, all of them get the keywords), or into the keyword list of the inspector or of a Quick Reference panel. Hold **Alt** while dropping to assign the group of a keyword as well. Inside the window, drag a keyword onto another one to put it into that group. Keyword colors appear on index cards.

### Custom metadata

Under **Project > Project Settings > Custom Metadata** you can add your own fields: text, checkbox, list (a fixed set of choices), or date, for example *POV*, *Location*, or *Story Day*. They appear in the inspector and as outliner columns.

### Section types

Section types tell Compile what each document is: *Part*, *Chapter*, *Scene*, *Section*, and so on. Set them per document in the inspector, or let {{APP}} decide by position (**Project Settings > Section Types**, "Default types by structure"), for example: folders at the first level are chapters, text documents inside them are scenes.

### Document templates

Put template documents (for example a character sheet) in a folder, then choose that folder under **Project Settings > Special Folders**. The templates appear under **Project > New From Template**. Under **Documents > Default Template for Subdocuments** you can make a folder use a template for every new document you create inside it.

### Project templates

**File > Save as Template…** saves the current project (structure, settings, and any example documents) as a template for new projects.

---

## 9. Searching

### Quick search

Type a few letters into the field in the toolbar. A list shows the documents whose **title**, **synopsis**, or **text** contains them, up to ten of each kind under its own heading. Choose one to open it: the places in the text that contain what you typed are **marked**, and the first one is selected and scrolled into view. The marks go away when you choose another document in the binder. This is the fastest way to a document whose name you know.

### Project search

**Ctrl+Shift+F**, or the magnifying glass in the toolbar, shows a search field above the binder. Type, and the binder is replaced by the purple list of results.

- Click a result to open it. **The matches are highlighted in the text**, the first one is selected and scrolled into view, and **F3** / **Shift+F3** jump from one to the next. **Ctrl+F** opens the find bar already filled with your search term.
- The **X** in the header of the results returns to the binder; the editor keeps the document it shows. Click the magnifying glass in the toolbar again to hide the search field.
- With the collections shown, the tab **Search Results** runs your last search again. The last search is kept with the project.

The magnifying glass at the left of the search field opens the options:

| Group | Entries |
|-------|---------|
| **Search In** | **All**, or one of: Title, Text, Synopsis, Notes, Keywords, Label, Status, Custom Metadata. A click chooses that one place. Hold **Alt** while you click (or while you open the menu) to choose several places. |
| **Operator** | **All Words** (every word must occur, in any order), **Any Word**, **Exact Phrase**, **Regular Expression** |
| **Options** | Whole Words, Case Sensitive, Invert Results (the documents that do *not* match), Search Draft Only, Search Binder Selection Only (the documents selected in the binder and everything inside them), Search "Included" Documents / Search "Excluded" Documents (by **Include in Compile**), Include Trash |

Example: to see everything that still has the status "To Do", search for `To Do` and choose **Status** under Search In.

**Save Search as Collection…** at the bottom of the menu keeps the search, with its options, as a collection that runs again each time you open it.

### Project Replace

**Edit > Find > Project Replace** replaces a text in the whole project at once.

1. Enter the text to **Replace** and the text to put in its place (**With**). Leave "With" empty to remove the text.
2. Choose **Match case**, **Whole words only**, or **Regular expression** (then "With" may use `\1` to `\9` for the groups of the expression).
3. Tick where to replace: **Titles**, **Text**, **Notes**, **Synopses**, **Custom metadata** (text fields), **Comments and footnotes** (the texts shown in the inspector).
4. Choose **Entire project** or **Selected documents only** (the documents selected in the binder, with what is inside them). The Trash is left alone unless you tick it.
5. **Count** tells you how many occurrences would change and where. **Replace All** asks once more and then does it.

Project Replace cannot be undone as a whole. Leave **Take a snapshot of every text before it is changed** ticked: every changed text then has a snapshot named "Before replacing …" to roll back to (section 11). Titles and synopses have no snapshots; for a large change, make a backup first with **File > Back Up > Back Up Now**. A text that is open in an editor can also be taken back there with **Edit > Undo**.

Formatting stays as it is: the new text takes over the formatting of the text it replaces.

### Find by Formatting

**Edit > Find > Find by Formatting** (**Ctrl+F3**) opens a small window that stays open while you work. Choose what to look for:

| Find | Finds | Extra choice |
|------|-------|--------------|
| Highlighted Text | text marked with the highlighter | limit to one color |
| Comments and Footnotes | text with a comment or footnote attached (inspector) | limit to one comment color |
| Inline Annotations, Inline Footnotes | notes typed into the text | annotation color |
| Revision Color | text typed in revision mode | which revision |
| Colored Text | text whose color you set by hand | limit to one color |
| Links | web links and links to documents | |
| Character Format | bold, italic, underlined, struck-out text (set by hand, not by a style) | which of them |
| Style | text with a paragraph or character style | which style |
| Images | pictures in the text | |

**Containing text** narrows the search to places that contain a word (for comments and footnotes, the note's own text counts too). **Next** and **Previous** select the next place. With **Search in: All Documents**, the search continues in the following documents of the binder and opens them; with **The Current Editor Only** it stays where you are. **Ctrl+G** and **Ctrl+Shift+G** repeat the search without the window.

---

## 10. Targets, statistics, and writing tools

- **Project > Project Targets** (**Ctrl+Shift+T**): set a word or character target for the Draft and for each writing session, and optionally a deadline and your writing days. {{APP}} can then calculate how much you need to write each session. The two thin bars under the quick-search box show your progress (blue: Draft; orange: session).
- **Document targets**: click the target in the editor footer.
- **Project > Project Statistics**: words and characters in the Draft, in the compiled text, and in the whole project, plus an estimate of the page count.
- **Project > Writing History**: how many words you wrote on each day.

The three writing tools below are add-ons (section 17): **Name Generator**, **Scratch Pad** and **Text Statistics**. They come with update 0.11 as one package; each can be switched off or removed in **Help > Add-on Manager**. Without them, **Edit > Writing Tools** says so and leads to the Add-on Manager.

### Text Statistics

**Project > Text Statistics…** counts a part of your project. Choose at the top which text:

- **Selected text** (chosen when you open the window with text selected),
- **Text in the editor** (in the combined text: all documents shown),
- **Documents selected in the binder**, with the documents inside them,
- **Draft (included in compile)**, or the **Whole project** (without the Trash).

The window has four tabs:

- **Overview**: words (counted exactly like the footer bar and the targets), characters with and without spaces, paragraphs, sentences, different words, pages at 250 words, reading time at 230 words a minute, and averages (letters per word, words per sentence and per paragraph, the longest sentence; point at it to read it).
- **Readability (estimates)**, also on the first tab. For English: the *Flesch Reading Ease* (0 to 100, higher is easier; 60 to 70 is plain English) and the *Flesch-Kincaid Grade Level* (the US school grade needed to follow the text). For German: *Amstad's Flesch index* (the same scale) and the *Wiener Sachtextformel* (about the school grade, 4 easy to 15 very hard). For every language: *LIX* (words per sentence plus the share of words with more than six letters; below 30 very easy, above 60 very hard). The language is found by itself or chosen at the top. These scores are guides, not verdicts: syllables and sentences are found by rules of thumb, and the formulas were made for factual prose, not for dialogue or poetry.
- **Word Frequency**: every word with its count and share, most frequent first. **Hide common words** leaves out articles, pronouns, prepositions and the like (English and German). Set a minimum length, look for a word, or count capitals separately. Double-click a word to search the project for it.
- **Repeated Phrases**: groups of two to six words that you used more than once inside sentences, such as a description you gave twice. Phrases of common words only are left out. Double-click to search the project.
- **Documents**: a table of the documents with words, characters, sentences, words per sentence, and their share of the whole. Click a column heading to sort; double-click a document to open it.

**Export Table…** saves the table of the current tab as a CSV file for a spreadsheet.

### Name Generator

**Edit > Writing Tools > Name Generator…** makes names for characters.

1. Tick the lists for the **forenames** (62 origins from Europe, the Americas, Asia and Africa, for example English, American, Irish and Irish Gaelic, German, Austrian, Swiss, Dutch, French, French Canadian, Italian, Spanish, Mexican, Portuguese, Brazilian, Danish, Swedish, Norwegian, Finnish, Icelandic, Polish, Czech, Hungarian, Russian, Greek, Turkish, Hebrew, Arabic, Persian, Indian, Japanese, Chinese, Korean, Yoruba, Swahili and Zulu; there are no Scottish or Welsh lists yet). The **surnames** come from the same list as the forename, or untick **The list of the forename** and choose other lists (an Italian forename with an Irish surname).
2. Choose **Gender**, **Number of names**, and what to **Show** (full name, forename only, surname only, initials and surname). **With a middle name** adds a second forename.
3. **Fine-tuning**: **common ↔ rare** picks from the most common names (left) or the rare ones (right); this works with the lists marked *by frequency* (for example American, Swedish, Hungarian, Hebrew), which carry how often each name occurs. **Alliteration** gives that share of the names a surname with the forename's first letter (Peter Parker); **Double surnames** joins two surnames with a hyphen. **Forename starts with** / **Surname starts with** limit the names to those beginning with these letters.
4. Click **Generate Names**. Select names and click **Copy** (all names when none is selected) or **Insert in Text** (also by double-click) to put them at the cursor of the editor.

Women get the women's form of a surname where a language has one (Polish *Kowalska*, Czech *Nováková*, Russian *Иванова*). Japanese, Chinese, Korean, Hungarian and Vietnamese names show the family name first. Some lists are in their own script (Russian, Greek, Hebrew, Arabic and others); their names in the list say which.

**Add Own List…** turns a text file with one name per line into a list of your own (forenames, female or male forenames, or surnames). Choose the list and click **Remove Own List** to delete it. The name lists come from the open-source project *Faker* (MIT license); they are lists of names in use, not checked historical name lists.

### Scratch Pad

The scratch pad holds notes outside every project: ideas while another project is open, quotes copied from a web page, a picture of a part of the screen. Open it with **Window > Scratch Pad** (**Alt+Shift+Return**).

- **+** starts a note (the arrow beside it: formatted or plain text). The title above the text is the note's name. **−** deletes the selected note.
- Formatted notes take **bold**, *italics* and underlining (buttons or **Ctrl+B / I / U**) and pictures: paste a picture, or click the **camera** to take a picture of a part of the screen. The camera is there for formatted notes only.
- Everything is saved by itself as you type.
- **Send to Project** lists every open project, also those open in other {{APP}} windows. For each: **New Document in the Research Folder**, **New Document in** a folder of your choice, or **Add to the End of** a text document. Formatting and pictures come along. The note stays in the scratch pad until you delete it.

The notes are ordinary files in a folder, normally *Documents/{{APP}} Scratch Pad* (plain text as .txt, formatted notes as .html with their pictures in the hidden sub-folder *.images*). Choose another folder, the kind of new notes, and whether the scratch pad stays above other windows under **File > Options > General > Scratch Pad**. Notes changed elsewhere (another {{APP}} window, a synced folder) show up at once.

**From every program.** The shortcut in the program works while {{APP}} has the keyboard. To open the scratch pad from any program, let the desktop start {{APP}} with `--scratch-pad`: in KDE, **System Settings > Keyboard > Shortcuts > Add New > Command or Script**, enter the command shown under **File > Options > Keyboard** (button **Copy Command**), and press the keys you want. The scratch pad then opens on its own, without a project window, and sends notes to whichever projects are open. If {{APP}} was added to the application menu (**Help > Add to Application Menu**), the menu entry also has the action **Scratch Pad**.

**The camera** takes the picture with KDE's screenshot program *Spectacle* when it is installed (needed under Wayland, where programs may not read the screen themselves): drag over the part you want. Without Spectacle, on X11, {{APP}} shows the screen and you drag over the part; **Esc** cancels, **Enter** takes the whole screen.

---

## 11. Snapshots

A snapshot is a saved copy of a document at a certain moment. Take one before a big rewrite:

- **Ctrl+5** takes a snapshot; **Ctrl+Shift+5** asks for a title.
- With **File > Options > General > Saving**, **File > Save** (**Ctrl+S**) takes a snapshot of every text you changed since the last File > Save (titled "Saved"); **File > Options > Behaviors > Snapshots** shows a short message in the status bar when snapshots are taken.
- In the inspector's snapshot tab you can **compare** a snapshot with the current text (additions are shown in color, deletions crossed out; choose whether to compare words, sentences, paragraphs, or characters), and **roll back** to it. Before rolling back, {{APP}} can take a snapshot of the current text, so nothing is lost.

---

## 12. Composition mode

**F11** fills the screen with nothing but your text on a sheet of paper. Move the mouse to the bottom edge to show the control bar: zoom, paper width, background fade, typewriter scrolling, word count, and a small inspector with synopsis and notes. **Alt+Shift+Up / Down** moves to the previous or next document, **Ctrl+[** and **Ctrl+]** go back and forward through the documents you have looked at. The shortcuts of the Format and Insert menus (bold, styles, comments ...) work here as well. **Esc** or **F11** returns to the main window.

**File > Options > Behaviors > Composition Mode** chooses the screen it opens on (the current one, the primary one, or one by name), whether the main window gets out of the way (minimized while composition mode is open), and whether **Esc** closes it (**F11** always does).

The colors of text, paper, and background come from the theme. To change them, for example to black on white or green on black, go to **File > Options > Appearance > Composition Mode**; the change shows at once, even while composition mode is open. A background picture can be set per project under **Project Settings > Special Folders**.

---

## 13. Compiling: making the manuscript

**File > Compile…** (**Ctrl+Shift+E**) puts everything in the Draft together into one file.

### The compile window

The compile window has **Compile For** at the top, the formats on the left, the section layouts in the middle, and the contents, metadata, options, and replacements on the right (four tabs with icons; hold the mouse over a tab for its name).

1. **Compile For**: the output format. Built in, with no other program needed: **Word Document (.docx)**, **PDF (.pdf)**, **E-book (.epub)**, and for scripts **Fountain** and **Final Draft**. PDF and OpenDocument through LibreOffice, EPUB through Pandoc, and Markdown appear as well if the matching plugin and program are installed (see below). Beside it: how many documents and about how many words will be compiled.
2. **Formats** (left), under **My Formats** and **Built-in Formats**: the compile format decides how the manuscript looks: fonts, line spacing, page size, margins, headers and footers, how chapters and scenes look, how styles, notes, and quotes come out. The built-in formats (*Manuscript (Times)*, *Manuscript (Courier)*, *Modern*, *Paperback (6 x 9 in)*, *E-book*, *Default (Editor Formatting)*, and *Outline (Synopses)*) cannot be changed. The buttons below the list: **+** (duplicate the selected format to make your own version and edit it), the pencil (edit your own format), **-** (delete it), **Import...** and **Export...** (a *.qbformat* file to use on another computer or to give away; an imported format whose name is taken gets a number).
3. **Section Layouts** (middle): **Font** sets one font for the whole manuscript, or leaves it to the layouts ("Determined by Section Layout"). The table lists every section type of your project with the layout it gets in this format; choose another one in the list. Below the table, a small picture shows how a section with the selected layout begins. **Assign Section Layouts...** opens a larger window with the section types on the left and pictures of all layouts of the format on the right. The built-in formats know the usual names of section types: in *Manuscript (Courier)*, for example, *Part* gets **Part Title**, *Chapter* gets **Chapter Heading**, and *Scene* or *Section* gets **Section Text**.
4. **Contents** (first tab): what is compiled, with the section type of each document and its layout. Choose a folder to compile only part of the Draft (for example one chapter), add front matter (title page) and back matter (acknowledgments). Untick a document to leave it out; this switches off its **Include in Compile** setting. **Add object sheets** puts a sheet for each object of the Objects tab (section 7) after the back matter: a heading per category (Characters, Locations ...), then each object with its fields; the button beside it chooses the categories. It is off at first.
5. **Metadata** (second tab): title, subtitle, author, language (for example *en-US*), subject, keywords, publisher, date, description, the cover of an e-book (a picture of the binder), and the book identifier. Empty fields take the project title, your name, and today's date. The details go into the PDF's document properties, the e-book, and the Word file; placeholders such as `<$projecttitle>` work here too.
6. **Options** (third tab): whether the text of folders is compiled.
7. **Replacements** (fourth tab): replacements of this project, made after those of the format, for example a name spelled one way in your drafts and another in the book. Each row can be switched on and off, and can be a regular expression, match case, or whole words only.
8. **Preview** shows the pages of the manuscript as a PDF in a window of its own, with the format and contents you chose, before anything is saved.

The font, the replacements, and the object sheets are kept with the project like the other choices (older versions of {{APP}} ignore them).

Click **Compile…**, choose where to save, and the finished file opens in your word processor or reader if you like. **Save Settings** keeps the choices without compiling.

**Fonts and paragraph formatting set by hand.** With the format *Default (Editor Formatting)*, the compiled file keeps them all: fonts, sizes, colors, capitals, letter spacing, indents, spacing, line spacing, and tab stops. Every other format has a look of its own: it replaces font, size, indents, and spacing, and keeps text color, capitals, letter spacing, alignment, and tab stops.

### Editing a compile format

**Duplicate & Edit…** and **Edit…** open the format editor. The list on the left holds its pages:

- **Text**: font, size, line spacing, first line indent, space after paragraphs, justified text, the font and size of headings, bold headings. **Use the project's editor formatting** keeps the look of the editor instead.
- **Section Layouts**: the layouts of the format (add, duplicate, remove) with heading, prefix and suffix (`Chapter <$W:chapter>`), heading style and alignment, upper case, blank lines before the heading, text, synopsis, and notes, a new page, the separator before the section, no indent on the first paragraph. The preview shows how a section with this layout begins.
- **Assignments**: which layout each section type gets in this format.
- **Styles**: how a style of your project comes out in this format. **Add a style…**, then choose: **Keep its look**, **Change its look** (font, size, bold, italic, underline, small or all capitals, color, alignment, indents, space before and after, line spacing, text before and after it, upper case, start on a new page; a box that is half ticked, or "unchanged", leaves the style as it is), **Make it body text**, or **Leave the text out** (for example notes to yourself in a style of their own).
- **Page**: paper size and margins, mirror margins for books (inside and outside), headers and footers (left, center, right; placeholders such as `<$p>` for the page number and `<$title>`), facing pages (left texts on left-hand pages, right texts on right-hand pages), header and footer on the first page and on pages where a chapter starts, chapters on a right-hand page, the first page number and its style (1, 2, 3 / i, ii, iii / I, II, III), the size of headers and footers.
- **Footnotes and Comments**: footnotes at the bottom of the page (in e-books: notes that pop up), at the end of each chapter, or at the end of the text (PDF and e-books; a Word file always has its footnotes at the bottom of the page), with the title of the notes and their size; inline annotations as comments or removed; linked comments as comments of the Word file; highlights removed.
- **Replacements**: text replaced in the compiled file only (your project stays as it is): on or off, case, whole words, regular expressions (`\1` to `\9` in "With").
- **Transformations**: straight or typographic quotes, dashes as hyphens, italics as underlining (the manuscript convention), several spaces as one, no spaces at the end of paragraphs, plain text without bold, italics, fonts, or colors.
- **Contents, PDF and E-book**: a table of contents (its title and how many heading levels), PDF bookmarks for the headings, clickable web links in the PDF, a file per section that starts on a new page in an e-book, and the table of contents as a page of the e-book as well.

Your formats are kept in *~/.local/share/{{APP}}/CompileFormats* and are available in every project.

### PDF

The built-in PDF output lays out the pages itself: footnotes at the bottom of the page where they are referenced (a long one goes on to the next page), headers and footers, chapters on right-hand pages if the format says so, headings kept with the text that follows, no single lines at the top or bottom of a page, a table of contents with page numbers that can be clicked, bookmarks for the headings in the reader's side panel, and clickable links. Fonts are embedded. Pictures keep the size they have in the text; a picture larger than the text area is made smaller to fit.

### E-books

The built-in e-book output writes EPUB 3 (with a table of contents for older readers as well): a file per chapter, the headings in the reader's table of contents, footnotes as notes that pop up (or at the end of each chapter or of the book), pictures, and the cover from **Metadata…**. The book gets an identifier the first time it is compiled; it stays with the project, so a reader takes a new version for the same book. Readers choose their own fonts and page size, so page settings, headers, and footers do not apply.

### Word

The Word file uses named styles (Title, Subtitle, Heading 1 to 3, Quote, Centered, Caption, and your own paragraph and character styles), so its headings appear in Word's navigation pane. With **Include a table of contents**, a table of contents field is put at the start, and the file asks the word processor to update its fields when it is opened which fills in the page numbers; if a word processor does not, update the field there yourself. Facing headers, the first page, and the first page number come from the **Page** settings of the format.

### Headings and placeholders

In a section layout, the heading can contain placeholders that Compile replaces:

| Placeholder | Result |
|-------------|--------|
| `<$n>` | 1, 2, 3 … |
| `<$w>` / `<$W>` / `<$WW>` | one, two … / One, Two … / ONE, TWO … |
| `<$R>` / `<$r>` | I, II, III … / i, ii, iii … |
| `<$L>` / `<$l>` | A, B, C … / a, b, c … |
| `<$n:name>` | a separate counter called *name* (works with every number style above) |
| `<$title>` | the document title |
| `<$projecttitle>` / `<$shorttitle>` | the project title / its first three words in capitals |
| `<$author>`, `<$forename>`, `<$surname>` | your name from **File > Options** or the project settings |
| `<$wc>` / `<$wc100>` / `<$wc1000>` | the word count, exact or rounded |
| `<$date>` / `<$year>` | today's date (yyyy-mm-dd) / the year |
| `<$p>` | the page number (headers and footers only) |
| `<$pages>` | the number of pages (headers and footers of PDF files and printouts) |

Example: the heading `Chapter <$W:chapter>` becomes *Chapter One*, *Chapter Two*, and so on.

Placeholders can also be typed into the text itself (**Insert > Auto Number** and **Insert > Placeholders**). **Help > List of All Placeholders…** lists them all; select rows and press **Ctrl+C** to copy them.

### Styles when compiling

The headings, Title, Subtitle, Block Quote, Centered, and Caption get their look from the compile format. Every other style brings its own formatting along (indents, spacing, alignment, font, color), in every compile format. With **Default (Editor Formatting)**, changes you made to the built-in styles are carried over as well. The **Styles** page of a format changes this for each style (see *Editing a compile format*).

### Notes when compiling

On the **Footnotes and Comments** page of a compile format you choose where footnotes go in a PDF or an e-book (bottom of the page, end of each chapter, end of the text), whether inline annotations become comments in the Word file or are removed, whether linked comments are kept, and whether highlights are removed. In a Word file, footnotes are always real footnotes at the bottom of the page.

### More output formats (plugins)

{{APP}} also comes with these formats. Each needs a program that is installed separately; PDF and EPUB through LibreOffice and Pandoc are an alternative to the built-in ones (they start from the Word file and look like it):

| Format | Needs |
|--------|-------|
| PDF (via LibreOffice) | LibreOffice (installed on most Ubuntu and Kubuntu systems) |
| OpenDocument Text (.odt, via LibreOffice) | LibreOffice |
| E-book (.epub, via Pandoc) | Pandoc (install "pandoc" in Discover) |
| Markdown (.md) | nothing |

**File > Options > Plugins** shows which formats are ready and which are missing a program. More formats can be installed as add-ons (section 17).

### Compiling scripts

Two outputs are made for scripts and need no other program:

- **Fountain Screenplay (.fountain)**: a plain text file that Highland, Fade In, Beat, Trelby, and other screenwriting programs read.
- **Final Draft (.fdx)**: the file format of Final Draft, which most screenwriting programs can open as well.

In both, every paragraph keeps what it is (scene heading, action, character, parenthetical, dialogue, transition, shot); the other program lays the script out in its own way. Titles of folders and documents that the compile format adds become sections (Fountain) or act headings (Final Draft); text that is no script element becomes action. The project title and your name (File > Options) make the title page. Inline annotations become notes in a Fountain file and are left out of a Final Draft file.

For Word or PDF, choose the compile format **Default (Editor Formatting)**: it keeps the indents, capitals, and font of the elements as you see them in the editor. The other compile formats replace font and size but keep the indents.

---

## 14. Import and export

### Importing files

**File > Import > Files…** (**Ctrl+Shift+I**) adds text files (.txt, .md, .rtf, .docx, .odt; old Word files .doc with the add-on below), scripts (.fountain, .fdx), pictures, PDFs, audio and video, and other files to the binder. You can also drag files from the file manager into the binder. Text files become editable documents; pictures and PDFs are shown in the editor; audio and video play there with the add-on Media Playback (section 6).

How files are read is set under **File > Options > Sharing > Import**: whether comments of Word, LibreOffice, and RTF files become inline annotations (otherwise they stay comments beside the text), whether footnotes become inline footnotes (the standard) or linked footnotes, whether the paragraph and character styles of the files are left out (the text gets the look of your project; bold, italic, notes, and links stay), the encoding of plain text files (automatic: UTF-8, else Windows Western), and whether *.html* files become text documents instead of web page documents.

Where the files go: select a folder in the binder to import into it (the files come last), or a document to put them right below it. The Draft takes text only; other files dropped there go to Research.

**Import and Split…** reads one long file and cuts it into many documents, for example at every line that contains only `#`, or at every double empty line.

### Old Word files (.doc)

Word 97 to 2003 saved documents as *.doc* (templates *.dot*). They become text documents with the add-on **Word 97–2003 Import** (section 17); without it, a .doc file is added to the binder as it is, and you are told which add-on reads it. The add-on needs no other program. It keeps:

- the text, including Greek, Cyrillic, Asian scripts and the like;
- headings (Heading 1 to 6), title, subtitle, quotations and captions, which get the styles of your project; a paragraph style of the document that your project also has (same name) gets that style;
- bold, italic, underline, strikethrough, small and all capitals, superscript and subscript, highlighting; centered, right-aligned and justified paragraphs;
- bullet and numbered lists with their levels (the numbers themselves are made by {{APP}}, so "Chapter 1" numbering of headings is not taken over);
- tables, as a line per row with the cells separated by tabs;
- footnotes and endnotes (as footnotes), comments with the commented words (the author's name comes first in the comment);
- links, the result of fields (dates, page numbers as they were last shown), page and section breaks, line breaks;
- pictures in the text (JPEG, PNG, BMP, TIFF);
- in RTF projects also fonts, sizes, colors and indents set by hand (Markdown projects keep the look of the project, as with every import).

Left out: headers and footers, floating pictures and drawings, pictures in the old metafile formats (WMF, EMF), embedded objects, hidden text, and text deleted with tracked changes (inserted text stays). The text of text boxes is added at the end of the document. After the import a message names what was left out.

**File > Options > Sharing > Conversion** can make LibreOffice the first choice for *.doc* files (it also reads headers, drawings, and Word 6/95 files); when the chosen way fails, the other one is tried. Word 6.0 and Word 95 files, and files the add-on cannot read, are converted with LibreOffice when it is installed (on Ubuntu: `sudo apt install libreoffice-writer`); otherwise the message says so. Password-protected files must be opened and saved without the password first. A *.doc* file that is really an RTF file, a Word 2007 file (.docx) or a web page, as some programs saved them, is read as what it is, also without the add-on.

### Importing web pages

**File > Import > Web Page…** brings a page from the web into your project. The add-on **Web Page Import** (section 17) does the work; without it, the command tells you so.

1. Select the folder that should receive the page (otherwise it goes to **Research**).
2. Choose **File > Import > Web Page…**. If the clipboard holds a web address, it is already filled in; otherwise type or paste one.
3. Click **Load** (or press Enter). The page and its pictures are fetched, and the window shows what will be imported, with the number of words and pictures, the author, the site and the date.
4. Correct the **Title** if you like, check the choices below, and click **Import**.

**Only the article** keeps the text you came for and leaves out menus, adverts, cookie notices, share buttons, comment sections, and lists of other articles. {{APP}} finds the article the way reading tools do (the text with the most sentences, commas and the fewest links wins; names like "article" or "sidebar" in the page count for or against a part). If something is missing, untick it to take the whole page. **Pictures** fetches the pictures of the article (very large ones are made 1600 pixels wide).

**Import as**:

- **Text document** (the standard): headings, paragraphs, quotations, lists, tables (a line per row), program code, captions, bold, italics and links become an ordinary document you can edit, annotate, split and compile.
- **Web page document**: a read-only copy of the article with its pictures, for reference. It is not counted or compiled.

Either way, the page's address becomes a **bookmark** of the document (inspector, Bookmarks), the page's short description becomes its **synopsis**, and the **notes** record the address, site, author, date of publication, and when you fetched it.

**Dragging a link** from a web browser onto a folder in the binder, onto the corkboard or into the outliner imports the page directly, with the choices you made last time in the window.

Good to know: the page is fetched as a browser would, but without running its scripts. Pages that build their text with a script often still carry it in their data, which is then used (without formatting and pictures; a message says so). Pages behind a login or a paywall give what they show to a visitor who is not logged in. Secure pages (https) need OpenSSL 3 on your system (part of every Ubuntu). A web address that leads to a PDF file is not imported this way: download the file and use **File > Import > Files…**.

**Scripts.** Fountain files (*.fountain*) and Final Draft files (*.fdx*) are imported as scripts: the paragraphs get the elements of your project's script format, the document is in script mode, and the title page of the file goes into the document's notes. Bold, italic, and underlined text, notes, and forced page breaks are kept; scene numbers, revision marks, and the two-column look of dual dialogue are not (the two speeches follow each other). **Import and Split…** with **Scene headings** cuts a script into one document per scene, named after its heading.

### Importing a Scrivener project

**File > Import > Scrivener Project…** reads a Scrivener 3 or 2 project (the *.scriv* folder; choose the *.scrivx* file inside it) and creates a new {{APP}} project from it. Scrivener's project is only read, never changed.

The import brings over: the binder structure, texts, synopses, notes, labels, statuses, keywords, custom metadata, section types, include-in-compile flags, snapshots, comments and footnotes, inline annotations, research files (pictures, PDFs, and others), collections, project bookmarks, and targets. Compile settings are not imported; set them up once in the compile window. Choose whether the new project stores its text as Markdown or RTF.

Headings and paragraph styles: Scrivener marks styled text inside its files with codes such as `<$Scr_Ps::0><$Scr_H::2>`. They are not text; {{APP}} takes them out. A heading gets **Heading 1** to **Heading 6**, and a paragraph with one of Scrivener's paragraph styles gets the style of your project with the same name (**Title**, **Block Quote**; Scrivener's **Centered Text** becomes **Centered**). Projects imported by versions before 0.14 kept the codes as text; {{APP}} takes them out when such a project opens and says so in the status bar. Project bookmarks were not imported before 0.14; to get them, import the Scrivener project again.

If something did not come across correctly, the original project is untouched and you can copy text from Scrivener directly.

### Working in Scrivener format

Instead of making a copy, {{APP}} can also work on a Scrivener 3 project itself, so that you can go on in Scrivener later (for example on another computer).

- **Opening**: **File > Open Project…** (**Open Other…** in the start window) and choose the *.scrivx* file inside the *.scriv* folder. The first time {{APP}} asks: **Work in Scrivener Format** (your changes are saved into the *.scriv*), **Convert…** (a {{APP}} project of its own is made from it, as with the import above), or **Cancel**. After that, the project opens without the question, also from **File > Open Recent**.
- **New projects**: choose **Format: Scrivener 3 project (.scriv)** in the New Project window.
- **Saving**: every save (also the automatic one) writes into the *.scriv*. Only what you changed is written; documents you did not touch keep the files Scrivener wrote. The window title says *(Scrivener format)*, and **File > Show Project in File Manager** and the backups take the *.scriv* folder.
- **One program at a time**: if Scrivener has the project open (or was not closed properly), {{APP}} says so and asks before opening it. Close the project in one program before you open it in the other. When the project was changed in Scrivener (or by a sync service) since you last had it open here, {{APP}} reads it again and tells you how many documents changed in the status bar; a document changed in Scrivener in the meantime is never overwritten with an older text.

What goes into the *.scriv*: the binder, texts (as RTF), synopses, notes, labels, statuses, section types, **Include in Compile**, project bookmarks, project targets, snapshots, comments and footnotes, pictures and PDFs, icons (by Scrivener's names; your own icons in its *Icons* folder), and mind maps (as an extra file beside the document's files, which Scrivener leaves alone). What stays on the {{APP}} side only (in a working copy in *~/.local/share/{{APP}}/ScrivenerProjects*): keywords, custom metadata, collections, styles, script formats, compile settings, objects (section 7), and the window layout; whatever the *.scriv* already has of these is left in it as it is. Audio, video, web page documents, and other files go in as text documents that name the file, with the file beside them; {{APP}} reads them back as they were, Scrivener shows them as text.

If a save into the *.scriv* was cut off (a crash, a full disk) after new documents were written into the working copy, the next opening keeps those documents and says so; the next save puts them into the *.scriv*. Documents that were deleted in Scrivener stay deleted.

**File > Project Format** switches the open project between the two formats:

- **Scrivener 3 Project (.scriv)**: choose the name and place of the *.scriv*; the project is saved there from now on. The {{APP}} project folder stays as it was at this moment (keep it as a copy or delete it).
- **{{APP}} Project**: choose a folder; the project gets a {{APP}} project folder there and is saved there from now on. The *.scriv* stays as it was at this moment.
- **Save a Copy as Scrivener Project…** and **Save a Copy as {{APP}} Project…** write a copy in the other format; you go on working where you are. **File > Export > Scrivener Project…** is the same as the first.

### Exporting documents

**File > Export > Files…** saves the selected documents for other programs. Use Compile for a finished manuscript; use Export to hand individual documents to someone, or to keep a copy outside {{APP}}.

1. In the binder, select the documents and folders to export.
2. Choose **File > Export > Files…**.
3. **Export text files as**: Rich Text (.rtf), Word (.docx), Markdown (.md), plain text (.txt), Fountain or Final Draft (for scripts, Fountain is preselected). Pictures, PDF files, web pages, audio and video are exported as they are; the format then only counts for their notes.
4. **Metadata**: **Export metadata** writes a text file beside each document, "*name* (metadata).txt", with its title, kind, label, status, keywords, include in compile, section type, word count, target, dates of creation and change, custom metadata, and bookmarks. **Export synopses** adds the synopsis to it (or writes "*name* (synopsis).txt" when the metadata is not exported).
5. **Options**: **Include subdocuments** (folders become folders), **Export notes** ("*name* (notes)" in the format of step 3; plain text for scripts), **Export comments and inline annotations** (untick to hand over the clean text; the commented words stay, footnotes always stay), **Export snapshots** (a folder "*name* (snapshots)" with one file per snapshot, named by date and title).
6. **Location**: the folder to save in. When several documents are selected, they go into a new folder whose name you give (the project's title is proposed; if the folder exists, a number is added).
7. Click **Export**. The folder opens in the file manager.

The choices are remembered for the next export.

The quick way: **drag documents out of the binder** into the file manager or onto the desktop. Text documents are saved as RTF files (or Word, Markdown, or plain text: **File > Options > Behaviors**), pictures and other files as they are, and a folder becomes a folder with its documents inside. If the file manager asks whether to move or copy, either is fine: the documents stay in your project.

### Compare and transfer: a Word or LibreOffice file and the project

When a manuscript comes back from an editor, or you went on working on it in Word or LibreOffice, **File > Compare and Transfer > Compare with File…** finds what differs between that file and your project and takes over the side you choose.

1. Choose the file: a Word file (*.docx*) or a LibreOffice file (*.odt*). Old Word files (*.doc*), RTF, plain text, and Markdown files can be compared too, but only read: their text can go into the project; the file itself cannot be changed.
2. {{APP}} compares the file with **what Compile makes of the project**, not with the whole project: the documents of the compile group (the Draft, unless another group is chosen in the compile window) that have **Include in Compile**, in the order of the binder. Front and back matter, the Research folder, and documents left out of Compile do not count. If less than 65 % of the file's text is found in it, {{APP}} says so and asks whether to compare anyway (**Compare Anyway**); the file may belong to another project, or Compile leaves out what the file has. A word counts as found when it lies in a run of four words that the compiled text has too, so a few changed words do not make a file unrelated.
3. The window **Compare and Transfer** shows the differences. Choose the side that is updated:
   - **Update the project from the file**: the changes are made in your documents. Before that, every changed document gets a snapshot, "Before Compare and Transfer (*file name*)" (**Documents > Snapshots**), so you can always go back. Text that is only in the file becomes a new document (named after its heading in the file), placed after the document before it.
   - **Update the file from the project**: the file you chose stays as it is. {{APP}} writes a copy beside it, "*name* (Quillbinder).docx" (with a number when that name is taken). In a Word file every change is a **tracked change** by the author "Quillbinder": open the copy in Word or LibreOffice and accept or reject each change there (in Word on the Review tab; in LibreOffice with Edit, Track Changes, Manage). A LibreOffice copy (*.odt*) gets the changes directly, without marks.
4. In the view, **green** text comes in, **red** struck-through text goes, and **blue** text gets the other side's bold, italic, or underline. Gray paragraphs are unchanged text around a change, to show where it is. Each place has a link **take** or **skip**; a click switches it.
5. Click **Transfer ... into the Project** or **Write ... into a Copy of the File**. A message tells what was done; for the file, **Open the File** opens the copy.

**By Chapter** (**File > Compare and Transfer > By Chapter**, also a tick box in the window) lists the documents on the left, each with a tick box and what differs ("2 place(s)", "only in the file", "not in the file"). Untick a document to leave it as it is; a click shows its changes on the right. Without By Chapter, all changes are shown one below the other.

**Step by Step** (**File > Compare and Transfer > Step by Step**, also in the window) shows one change at a time: **Take** or **Skip** it, or go **Back**. At the end the window says how many changes will be taken; then click Transfer. Both choices are remembered for the next time.

What is compared: the words, and bold, italic, and underline. Each paragraph of the file is matched with the document that has its words, so a scene is found also where it was moved in the file (it is then marked "in another place in the file"; the order of the binder is not changed). Headings of the file that are not text of a document, such as the chapter headings made by Compile, are not compared.

What is ticked at first: every change inside a document. Text that is only on one side is ticked when it has at least 100 words (a new chapter of the file becomes a new document; a scene missing from the file is added to the copy). A whole document is never moved to the Trash, and a whole part is never taken out of the file, unless you tick it.

Limits: new paragraphs take the look of the paragraph next to them. Text in tables is compared like other paragraphs (a table itself is never taken out), and fields count with the text they show. Headers and footers, text boxes, and the text of the file's footnotes and comments are not compared and stay as they are; comments and footnotes of the project stay in the project and are not written into the file. If the file was changed after the comparison, the copy is not written; compare again.

### Printing

**File > Print Current Document…** (**Ctrl+P**) prints what the current editor shows:

- **A text** (one document, or several in the combined text): the text with its formatting and pictures, the title of each document above its text, and footnotes at the bottom of the page. With the keyboard in a copyholder, the copyholder's document is printed.
- **The corkboard**: the index cards in the order shown, as many on a sheet as fit (with lines to cut along), or one card on each sheet for index card stock.
- **The outliner**: the rows and columns as the outliner shows them, with the column titles at the top of every page.
- **A picture** (as large as the page allows), **a PDF file** (its pages), or **a web page** (its text).

The print window chooses the printer, the pages, and the number of copies. **Selection** prints only the selected text, or only the selected index cards or outliner rows. To save the printout as a PDF file instead, choose **Print to File (PDF)** as the printer.

**File > Print Preview…** shows the pages first; the printer button of the preview prints them.

**File > Print Settings…** decides how things go on paper. The window opens at the tab for what the editor shows; **Preview…** shows the result before you click **OK**. The settings belong to the project; tick **Use these settings for new projects as well** to make them the standard for new projects.

- **Page**: the paper size and orientation (the button **Paper of the Page View** takes over the paper and margins of **View > Text Editing > Page Size and Margins…**), the margins, and the header and footer, each with a left, a center, and a right part. Placeholders: `<$title>` (what is printed), `<$projecttitle>`, `<$author>`, `<$date>`, `<$p>` (the page number), and `<$pages>` (the number of pages). Untick **Header and footer on the first page as well** for a clean first page.
- **Text**: what is printed with the text (the title of each document, the synopsis below it, the document notes after the text, comments and inline annotations as notes at the bottom of the page, footnotes at the end of the text instead of the bottom of each page, highlights); the fonts (as in the editor, or **One font** of your choice); and the line spacing (as in the editor, single, 1.5 lines, or double, with room for corrections). Script documents keep their script layout.
- **Index Cards**: the card size (3 x 5 in, 4 x 6 in, 5 x 8 in, A7, A6, or a size of your own), as many cards on a sheet as fit or one card on each sheet, what each card shows (title, synopsis, the first lines of the text when there is no synopsis, pictures, the label color as a stripe at the top, status, keywords, the lines of an index card, a line around each card), and the font.
- **Outliner**: the synopses (as the outliner shows them, always, or never), also the rows inside closed groups, the column titles at the top of every page, a line between rows, a dot of the label color, and the font. With many columns, **Landscape** on the **Page** tab gives them more room.

Print Current Document prints a document as you work on it. For a finished manuscript with the look of a compile format, compile it (section 13) and print the PDF or Word file.

---

## 15. Backups

{{APP}} makes a backup (a ZIP file of the whole project) every time you close a project, and keeps the last five per project. Change this under **File > Options > Backup**, where you can also choose the backup folder (a cloud-synced folder is a good choice).

- With **Back up with each manual save** (**File > Options > Backup**), every **File > Save** (**Ctrl+S**) makes a backup as well. Two backups within the same second get a number ("... (2)").
- **File > Back Up > Back Up Now** makes a backup immediately.
- **Back Up To…** saves a backup wherever you choose, for example on a USB stick.
- **Show Backup Folder** opens the folder.

A backup is written under a temporary name (*… .zip.part*), checked, and only then gets its real name, so a backup that was cut off never looks like a finished one. Large projects (more than 4 GB, for example with audio and video) are backed up correctly as well (since version 0.20). A file that cannot be read (for example because of its permissions) is left out; the backup is still made, and **Back Up Now** names the files that are missing. The backup of a project in Scrivener format holds the *.scriv* and, beside it, the working copy of {{APP}} (*… - working copy of {{APP}}*), with the keywords, styles, objects, and other things that only {{APP}} knows.

**To restore a backup**: open the backup folder, unpack the ZIP file (right-click > Extract), and open the project in it with **File > Open Project**.

---

## 16. Options

**File > Options** (**Ctrl+,**) has a row of icons at the top for the sections, a list of pages (or tabs) on the left, the options on the right. Nothing changes until you click **OK** or **Apply**. At the bottom left, **Manage** saves all options into a file and loads them again (**Save Options to File...**, **Load Options from File...**, for example to take your settings to another computer; recent projects and window places are not in the file), saves the current look as a theme file, and lists the themes; **Defaults** sets every option back to the standard (your recent projects, your own themes, layouts, toolbars, and shortcuts stay).

- **General**: **Startup** (reopen the last project, the desktop's file dialogs); **Saving** (autosave interval, the number of recent projects, snapshots with each File > Save); **Author Information** (your name, address, phone, and email for title pages, used by the placeholders `<$author>`, `<$street>`, `<$city>`, `<$state>`, `<$zip>`, `<$country>`, `<$phone>`, `<$email>`, `<$address>`); the options of the writing tools (**Scratch Pad**); **Shared Templates** (a further folder of project templates, for example one synchronized between your computers; its templates appear in File > New Project); **Citations** (the program **Format > Bibliography/Citations...** starts, for example Zotero or JabRef); **Separators** (what goes between merged documents and between scratch pad notes added to a document: a single return, an empty line, a page break, nothing, or your own text); **Automatic Quit** (save and close after a number of minutes without key or mouse input); **Warnings** (**Show Internal Log Console** shows the program's messages of this session, **Reset All Warnings** asks again the questions answered with "Don't ask again").
- **Editing**: **Options** (reload the editors' last zoom, the default text zoom of each area, centimeters or inches and the step of the ruler, the Insert key for overtype, a date format of your own for Insert > Current Date, what the live count shows, inline annotations without color, comments and footnotes that stop before punctuation, a block insertion point); **Formatting** (the formatting and styles of new projects); **Revisions** (the five colors of Format > Revision Mode, from the shared palette).
- **Behaviors**: **Composition Mode** (screen, main window out of the way, Esc); **Document Links** (where document links open, back-links); **Double-Clicking** (a double-click on the corkboard makes a card); **Dragging & Dropping** (documents dropped onto text documents, the format of text dragged to other programs); **Folders & Files** (folders open as a corkboard; a text with subdocuments opens like a folder); **Playback** of audio and video (section 6); **Quick Reference** (panels reopen with their project); **Return Key** (Return ends editing a synopsis; Return makes a new card or row); **Snapshots** (a message when snapshots are taken).
- **Appearance**: the theme, and twelve parts of the window (General Interface, Binder, Composition Mode, Corkboard, Index Cards, Inspector & Notes, Main Editor, Outliner, Page View, Combined Text, Target Progress Bars, Textual Marks), each with its options, fonts, and colors. See "Themes, colors, and fonts" below. **General Interface** also has the full path of the project in the title bar and the size of the toolbar icons.
- **Corrections**: corrections while typing (section 4) and **Spelling** (the dictionary, words with numbers, the underline, the personal word list).
- **Keyboard**: the shortcut of every command (section 19).
- **Sharing**: **Import** (comments, footnotes, styles, the encoding of plain text, HTML files; section 14), **Export** (the file format and notes of File > Export > Files), **Conversion** (how *.doc* files are read).
- **Backup**: when to back up (on opening, on closing, with each File > Save), how many backups to keep, and where.
- **Extras**, for {{APP}}'s own additions: **Mind Map** (the options of the boards, the shape and look of new notes, the background of boards without a color of their own in light and in dark themes, the size of icons on notes and lines), **Objects** (whether new projects get your own object templates or the standard ones), **Add-ons and Plugins** (the compile formats and writing tools installed, the plugin folders), **Crash Reports** (after how many seconds without an answer a freeze report is written, how many reports are kept).

Other windows of the program can open the options at a page, for example the Playback page from the media player.

Updates and add-ons have their own windows: **File > Install Update or Add-on…** and **Help > Add-on Manager…** (section 17).

Project-specific settings (labels, status, section types, the look of the text, custom metadata, special folders, title and author for compiling) are under **Project > Project Settings**.

### Themes, colors, and fonts

A theme sets the colors, and if it wants the fonts, of every area of the window. Choose one under **Window > Themes**; the change takes effect at once, without a restart. Eight themes are built in:

| Theme | Look |
|-------|------|
| Default (Follow the Desktop) | the colors of your desktop (the KDE color scheme) |
| Light | light gray window, white paper |
| Dark Mode | dark gray, with dark index cards |
| Sepia | warm cream and brown |
| Slate | gray window, white paper on a gray desk |
| Midnight | dark blue with an amber accent |
| Ocean | light blue-green |
| High Contrast | white and yellow on black |

Add-ons can bring more themes (the add-on "Extra themes" has Northern Night, Forest, Graphite, Warm Paper, and Rose Dawn).

**Changing colors and fonts.** **File > Options > Appearance** (or **Window > Themes > Colors and Fonts…**) lists the parts of the window on the left. Each shows its options, its fonts, and its colors (a click on a color shows the shared palette: the label colors, the six standard colors, and **Other...**):

| Part | Fonts | Colors |
|------|-------|--------|
| General Interface | menus, buttons, and panels | window, lists, alternate rows, text, buttons, selection, links, tool tips, borders, grayed-out text |
| Binder | binder | background, titles |
| Composition Mode | (none) | background, paper, text |
| Corkboard | (none) | corkboard, status stamp |
| Index Cards | card titles, card text | index card, title, text, line under the title, ruled lines |
| Inspector & Notes | synopsis | background, labels, synopsis card and text, notes and their text |
| Main Editor | header bar title | paper, text, the space beside the text column, the header bar (normal, active editor, locked in place) |
| Outliner | outliner | background, alternate rows, text |
| Page View | (none) | the desk around the pages |
| Combined Text | titles in the combined text | (none) |
| Target Progress Bars | (none) | session, draft, target reached |
| Textual Marks | (none) | links in the text, search hits, spelling underline |

Click a color to change it, a font button to choose a font. The small picture at the top shows the result before you press **Apply** or **OK**. Your changes sit on top of the theme: a changed entry is shown in bold, the **↺** button next to it gives it back to the theme, and **Reset Colors and Fonts to the Theme** does so for all of them. A color you have not changed and the theme does not set follows the others; for example, the binder uses the color of lists until you give it one of its own.

The font and size of the text you write are not part of the theme: they belong to the project (**Project > Project Settings > Formatting**, section 4).

**Themes of your own.** **Save as Theme…** in the options (or **Window > Themes > Save Current Look as Theme…**) stores the theme together with your changes under a new name. It then appears in **Window > Themes** like the others. **Window > Themes > Manage Themes…** lists all themes: **Use This Theme**, **Duplicate…** (also for built-in themes, to get a starting point of your own), **Rename…** and **Delete** (your own themes only), **Export…** (writes a *.qbtheme* file you can pass on), and **Import…** (adds such a file).

When you choose another theme while you have changes of your own, {{APP}} asks whether to keep them with the new theme or to use the theme as it is.

Links in your text are stored with one blue; on dark paper they are shown in a lighter blue (**Main Editor > Links in the text**). Text you colored yourself keeps its color in every theme.

---

## 17. Updates and add-ons

**Help > Developer Guide: Updates and Add-ons** explains, for people who build {{APP}} or write add-ons, how updates and add-ons work and which tools make them.

{{APP}} is one program file (the AppImage). Updates and extensions come as small package files ending in **.qbpkg**. There are three kinds, and all are installed the same way:

- **Program update**: a newer version of {{APP}} itself.
- **Add-on**: an extension you can switch on and off, such as color themes, project templates, compile formats, dictionaries, writing tools (section 10), the playback of audio and video and its extra formats (section 6), importers for old Word files and web pages (section 14).

Program updates and add-ons are on the **Releases** page of the project page (section 1), next to the AppImage.
- **Update bundle**: one file that contains a program update and several add-ons, installed together.

### Installing a package

1. Choose **File > Install Update or Add-on…** and select the .qbpkg file, or double-click the file in your file manager.
2. {{APP}} shows what the package contains and what is new. Click **Install**.
3. If the package contains a program update, {{APP}} asks to restart. Your open project is saved and reopened.

{{APP}} checks every package before installing it: damaged files, and packages made for a different version, are refused. A program update is started once invisibly as a test; if it does not run on your computer, it is not installed.

### The Add-on Manager

**Help > Add-on Manager…** shows what is installed.

- **Program version**: the version built into your AppImage and every installed update. The one marked *running now* is in use. To go back to an earlier version, select it and click **Use This Version**, then restart. Installed updates you no longer need can be removed.
- **Add-ons**: untick an add-on to switch it off without removing it; **Remove Add-on** deletes it. Themes, templates, and compile formats of an add-on appear in the usual places: **Window > Themes**, the **New Project** window, and the **Compile** window.

### If an update does not start

If a program update fails to start twice in a row, {{APP}} switches it off by itself and starts the version built into the AppImage, with a message telling you so. Nothing in your projects is affected by this.

### When a new AppImage is needed

Program updates run on the libraries inside your AppImage. From time to time a larger update needs a new AppImage; the install window tells you when a package does not fit yours. Replace the old AppImage file with the new one; your settings, add-ons, and projects stay as they are.

A new AppImage also brings a new program version of its own (for example the AppImage 0.20.0). It starts that version, even if you installed updates into the old AppImage before: older updates are switched off and stay listed under **Help > Add-on Manager…**, where you can remove them. If the new file has another name or lies in another folder than the old one, choose **Help > Add to Application Menu** once, so that the menu entry and double-clicked projects use the new file.

---

## 18. The Markdown dialect

You do not need to know this to use {{APP}}; the editor shows formatted text. It matters only if you open the text files with another program. {{APP}} writes Markdown with these rules:

- **Every line is one paragraph**, as in a word processor. An empty line is an empty paragraph.
- Headings: `# ` to `###### ` at the start of the line.
- Block quote: `> `. Centered paragraph: `->text<-`. Any other paragraph style: `::Style Name:: text`.
- Character styles (from version 0.4): `<span class="Style Name">text</span>`. Only the names of styles are written into the text. What a style looks like is stored once, in the project file, so a changed style needs no change in the documents.
- Lists: `- ` and `1. `; two spaces per level of nesting.
- `**bold**`, `*italic*`, `~~strikethrough~~`, `==highlight==`, `<u>underline</u>`, `<sup>superscript</sup>`, `<sub>subscript</sub>`.
- Revision colors: `<rev1>…</rev1>` to `<rev5>…</rev5>`.
- Links: `[text](https://example.com)`; links to documents in the project: `[text](qb://ID)`.
- Images: `![](images/picture.png){width=300}` (the pictures are stored in the document's folder).
- A line break inside a paragraph: `<br>`.
- Fonts set by hand (from version 0.3): `<span style="font-family:'DejaVu Sans'; font-size:14pt; color:#aa0000">text</span>`. The declarations are ordinary CSS, so other Markdown programs that allow HTML show them too. Possible entries: `font-family`, `font-size` (pt), `font-weight` (100 to 900), `color`, `letter-spacing` (em), `font-variant:small-caps`, `text-transform:uppercase`.
- Paragraph formatting set by hand (from version 0.3): a list in braces at the start of the line, before any other prefix: `{: align=right left=10mm right=5mm first=0mm before=6pt after=12pt line=1.5 tabs=20mm,50mmR} text`. `line` is a multiple (`1.5`), an exact height (`14pt`), or a minimum (`14pt+`). Tab stops end with `R` (right), `C` (centered), or `D` (decimal point); no letter means left. Only the entries you set are written.
- Script elements (from version 0.10) are paragraph styles named after the element: `::Script > Scene Heading:: INT. LIGHTHOUSE - NIGHT`. What an element looks like is stored once, in the project file, with the script format.
- A page break before a paragraph (from imported scripts and RTF files, from version 0.10): `{: page=before} text`.
- Version 0.2 shows both markers as plain text. If you go back to version 0.2 with the Add-on Manager, do not edit documents that contain them.
- Versions before 0.4 do not know your own styles: they show `<span class=…>` as plain text and treat paragraphs of your own styles as body text. If you go back to an older version, do not edit documents that use your own styles.
- Versions before 0.10 treat script elements as body text, and they remove the names of the elements from a script as soon as it has been shown in the editor and the project is saved. If you go back to an older version, do not open your scripts there (see section 21).
- A backslash makes the next character ordinary text: `\*` is a star, not italic.
- Notes follow the CriticMarkup convention:
  - `{>>inline annotation<<}`
  - `^[inline footnote]`
  - `{==text==}{>>linked comment<<}`
  - `{==text==}^[linked footnote]`
  - A comment may start with a color: `{>>@#ff8800 text<<}`.

**File > Convert Markdown ⇄ RTF File…** reads both this dialect and standard Markdown (where paragraphs are separated by empty lines), and when writing Markdown it asks which of the two you want.

---

## 19. Keyboard shortcuts

### Changing shortcuts

**File > Options > Keyboard** (or **Help > Keyboard Shortcuts**) lists every command, grouped by menu, with its shortcut and the standard shortcut the program came with. The styles of the open project and the layouts are listed as well, so a style or a layout can have a shortcut of its own.

1. Find the command: scroll, or type a part of its name or a shortcut such as "Ctrl+K" into the search field.
2. Click the command, click into the shortcut field below the list, and press the new keys.
3. If another command has that shortcut, you are asked whether to take it away from there. The other command then has no shortcut until you give it a new one.

**No Shortcut** removes the shortcut of the selected command, **Standard** gives it back the one it came with, and **All to Standard** undoes all your changes. Your changes are shown in bold. A shortcut needs Ctrl, Alt, or Meta, or must be a function key, because plain letters are needed for writing. **Export** and **Import** save your set of shortcuts as a file (*.qbkeys*) and load one, for example to carry it to another computer.

Shortcuts of styles belong to the name of the style: "Block Quote" has its shortcut in every project that has a style of that name.

### The standard shortcuts

| Action | Shortcut |
|--------|----------|
| New project / open project | Ctrl+Shift+N / Ctrl+O |
| Save / close project / exit | Ctrl+S / Ctrl+W / Ctrl+Q |
| New document / new folder | Ctrl+N / Alt+Shift+N |
| Rename / duplicate | F2 / Ctrl+D |
| Move item up / down / out / in | Ctrl+Alt+Up / Down / Left / Right |
| Split at selection / with selection as title | Ctrl+K / Ctrl+Shift+K |
| Merge documents | Ctrl+Shift+M |
| Combined Text / corkboard / outliner | Ctrl+1 / Ctrl+2 / Ctrl+3 |
| Show or hide binder / inspector | Ctrl+Alt+B / Ctrl+Alt+I |
| Composition mode / full screen | F11 / Ctrl+Shift+F11 |
| Focus binder / focus editor / other editor | Alt+1 / Alt+2 / F6 |
| Back / forward | Ctrl+[ / Ctrl+] |
| Previous / next document | Alt+Shift+Up / Alt+Shift+Down |
| Reveal in binder | Ctrl+Shift+R |
| Find / replace / next / previous | Ctrl+F / Ctrl+H / F3 / Shift+F3 |
| Project search | Ctrl+Shift+F |
| Use selection for find | Ctrl+E |
| Find by formatting / next / previous | Ctrl+F3 / Ctrl+G / Ctrl+Shift+G |
| Scratch pad (add-on) | Alt+Shift+Return; from every program: see section 10 |
| Audio and video (add-on): play / pause | Ctrl+Return |
| Audio and video: skip back / skip forward | Ctrl+Alt+, / Ctrl+Alt+. |
| Audio and video: insert timestamp | Ctrl+Shift+Return |
| Filter corkboard or outliner | Ctrl+F (while one of them has the keyboard) |
| Link to document | Ctrl+L |
| Comment / footnote | Shift+F2 / Ctrl+Shift+F2 |
| Inline annotation / inline footnote | Ctrl+Shift+A / Ctrl+Alt+F |
| Take snapshot / with title | Ctrl+5 / Ctrl+Shift+5 |
| Project targets / project keywords | Ctrl+Shift+T / Ctrl+Alt+K |
| Import files / compile | Ctrl+Shift+I / Ctrl+Shift+E |
| Print current document | Ctrl+P |
| Fonts window / paragraph window | Ctrl+T / Ctrl+Alt+P |
| Font size bigger / smaller | Ctrl+> / Ctrl+< |
| Copy formatting / paste formatting | Ctrl+Alt+C / Ctrl+Alt+V |
| No Style / Heading 1 / Heading 2 / Heading 3 | Ctrl+Alt+0 / Ctrl+Alt+1 / Ctrl+Alt+2 / Ctrl+Alt+3 |
| Script mode / list of script elements | Ctrl+8 / Ctrl+\\ |
| In a script: next element / change element | Enter / Tab (see section 4, Scriptwriting) |
| Zoom in / zoom out / reset zoom | Ctrl++ / Ctrl+- / Ctrl+0 |
| Zoom the area under the mouse | Ctrl+mouse wheel |
| Show or hide the ruler | Ctrl+Alt+R |
| Typewriter scrolling | Meta+Ctrl+T |
| Options | Ctrl+, |
| This manual | F1 |

---

## 20. For helpers: plugins, packages, and the project folder

This section is for a technically minded friend who helps you set things up.

### Media engines

Audio and video are played by a plugin of the add-on *Media Playback* that implements `src/media/MediaApi.h` of the source (Qt Multimedia 6.4.2 with a decoding-only FFmpeg 6.1.1, see `addons/media-playback` and `tools/build-ffmpeg.sh`). The add-on keeps its libraries in its own folder (`media/lib`); FFmpeg is loaded only for the built-in decoding, so the system's GStreamer keeps its own FFmpeg. Its FFmpeg decodes free formats only; the add-on *Extra Video and Audio Formats* brings a full FFmpeg of the same version in `media-extra/lib`, which *Media Playback* loads instead while that add-on is installed and switched on. **File > Options > Plugins** lists the engine.

### Adding compile formats

**File > Options > Plugins > Open My Plugin Folder** opens *~/.local/share/{{APP}}/plugins*. Two kinds of plugins go there (or into the `plugins` folder of an add-on package):

- **Command plugins**: a small `.json` file that tells {{APP}} to run a converter program (such as LibreOffice or Pandoc) on the Word file it has just written. Copy one of the bundled examples and change the program and arguments. The keys are `id`, `name`, `extension`, `input` (`docx` or `md`), `program` or `programs`, `arguments` (with `{input}`, `{inputbase}`, `{outdir}`, `{output}`, `{tmpdir}`, `{title}`, `{author}`), `output`, and `timeout`.
- **Qt plugins**: a shared library (`.so`) built against Qt 6 that implements the `ExporterInterface` from the header *ExporterApi.h* in the source code. It receives the compiled manuscript as a simple model of paragraphs, formatted runs, footnotes, and comments. The Markdown exporter in the source code is a complete example. Version 0.3 extended the model with fonts and paragraph formatting (interface 1.1); a plugin built for an earlier version is not loaded until it is built again.

Restart {{APP}} after adding a plugin.

### Package files (.qbpkg)

A package is a ZIP file with a `manifest.json` (id, name, version, kind, description, what's new, required versions, and a list of files with SHA-256 checksums) and a `payload` folder. In an add-on, the payload uses these folders: `themes` (color themes as JSON), `layouts` (window layouts as JSON), `scripts` (script formats as JSON), `templates` (project templates as ZIP), `compile-formats` (JSON), `plugins` (command plugins and Qt plugins), and `dictionaries` (Hunspell files). The source code contains the tool *tools/make-package.py*, which writes the manifest and builds the file, and example add-ons in the *addons* folder.

Installed add-ons are stored in *~/.local/share/{{APP}}/addons*, program updates in *~/.local/share/{{APP}}/core*.

### Theme files

A theme is a small JSON file: in an add-on as *themes/name.json*, your own themes in *~/.local/share/{{APP}}/themes*, exported themes with the ending *.qbtheme*.

```
{ "name": "Northern Night",
  "colors": { "window": "#2b303b", "text": "#e3e7ee", "highlight": "#5e81ac", "cardPaper": "#434c5e" },
  "fonts":  { "binder": { "family": "Noto Sans", "size": 10.5, "bold": false, "italic": false } } }
```

Only `window` and `text` are required; every other color is derived when it is missing. The color names, in the order of the options: `window`, `windowText`, `base`, `alternateBase`, `text`, `button`, `buttonText`, `highlight`, `highlightedText`, `link`, `toolTipBase`, `toolTipText`, `mid`, `dark`, `light`, `disabledText`, `targetSession`, `targetDraft`, `targetReached`; `binderBackground`, `binderText`; `editorPaper`, `editorText`, `editorMargin`, `editorLink`, `searchHit`, `spellingError`, `headerBar`, `headerBarText`, `headerBarActive`, `headerBarLocked`; `corkboardBackground`, `cardPaper`, `cardTitle`, `cardText`, `cardTitleLine`, `cardLines`, `statusStamp`; `outlinerBackground`, `outlinerAlternate`, `outlinerText`; `inspectorBackground`, `inspectorText`, `synopsisPaper`, `synopsisText`, `notesPaper`, `notesText`; `compositionBackground`, `compositionPaper`, `compositionText`. Font names: `interface`, `binder`, `headerTitle`, `combinedTitle` (written `scriveningsTitle` before 0.40; still read), `cardTitle`, `cardText`, `outliner`, `synopsis`; in a font, leave out what should stay as it is. `"followDesktop": true` takes the window colors from the desktop. Theme files written for versions before 0.5 work unchanged, and versions before 0.5 read the window colors of newer files and ignore the rest.

### Layout and shortcut files

A layout is a JSON file: in an add-on as *layouts/name.json*, your own layouts in *~/.local/share/{{APP}}/layouts*, exported layouts with the ending *.qblayout*.

```
{ "format": "qblayout", "version": 1, "name": "Research Desk",
  "state": { "binder": true, "inspector": false, "toolbar": true, "formatBar": true,
             "binderShare": 0.17, "split": "vertical", "splitShare": 0.42,
             "panes": [ { "view": "outliner", "affectsOther": true }, { "view": "text" } ],
             "binderAffects": "first", "active": 1 } }
```

`split` is `none`, `horizontal` (one editor above the other), or `vertical` (side by side). `view` is `text`, `corkboard`, `outliner`, or `keep`; a corkboard can add `"cork": "grid"`, `"freeform"`, or `"labels"`. `binderShare`, `inspectorShare`, and `splitShare` are shares of the width (or height), so a layout fits small and large screens. `binderAffects` is `current`, `first`, `second`, or `other`. Optional: `collections`, `ruler`, `inspectorTab` (0 to 4), and `"window": { "width": 1400, "height": 900, "maximized": false }`.

A shortcut file (*.qbkeys*) lists only the changes: `{ "format": "qbkeys", "version": 1, "shortcuts": { "merge": "Ctrl+Alt+M", "split": "" } }`. The names are those of the commands in the program (an empty text means "no shortcut"); styles are written as `style:Name`, layouts as `layout:id`. The contents of the toolbars and your shortcuts are stored with the other options in *~/.config/{{APP}}/{{APP}}.conf*.

### Script format files

A script format is a JSON file: in an add-on as *scripts/name.json*, your own formats in *~/.local/share/{{APP}}/scripts*, saved formats with the ending *.qbscript*. The quickest way to make one is **Format > Scriptwriting > Script Settings… > Manage > Save to File…**.

```
{ "name": "Radio Play", "description": "...", "font": "", "size": 12, "unit": "in", "first": "Scene Heading",
  "elements": [
    { "name": "Character", "role": "character", "key": "C", "caps": true, "align": "left",
      "left": 55.9, "right": 0, "firstLine": 0, "linesBefore": 1, "lineSpacing": 1, "keepWithNext": true,
      "enter": "Dialogue", "tabEmpty": "Transition", "tabEnd": "Parenthetical", "completeFromScript": true },
    { "name": "Parenthetical", "role": "parenthetical", "key": "P", "left": 40.6, "right": 50.8,
      "linesBefore": 0, "prefix": "(", "suffix": ")", "enter": "Dialogue", "tabEmpty": "Dialogue" } ] }
```

Lengths are millimeters from the text margins (`unit` only says what the settings window shows). `role` is `scene`, `action`, `character`, `parenthetical`, `dialogue`, `transition`, `shot`, `lyrics`, `centered`, or `section`. `enter`, `tabEmpty`, and `tabEnd` name the element that follows; `tabInserts` is a text that Tab types instead. `completions` are offered at the start of the paragraph, `tabCompletions` after the text typed by Tab; with `completeFromScript` the texts of the same element elsewhere in the document are offered too. Also possible: `bold`, `italic`, `underline`, `color`. The name of an element may not contain `:` or `>`.

### Inside a project folder

```
MyNovel{{SUFFIX}}/
  MyNovel{{FILE}}          the binder and all settings (XML)
  content/<ID>/text.md     the text of a document (or text.rtf)
  content/<ID>/notes.md    its notes
  content/<ID>/images/     pictures used in the text
  content/<ID>/file.pdf    an imported file (pictures, PDFs …)
  content/<ID>/mindmap.scap  the mind map of a document or folder
  icons/                   the project's own icons
  objects/schema.json      categories, templates, colors, and folders of the objects
  objects/<category>/<ID>.json  one object (a character, a location ...)
  objects/images/          pictures of objects
  snapshots/<ID>/          snapshots of a document
  settings/                window layout and other preferences
  settings/spare-copy{{FILE}}  the project file as it was when the project was last opened
```

A project in Scrivener format has a working copy of this kind in a folder of its own under *~/.local/share/{{APP}}/ScrivenerProjects/*; its *settings/scrivener-sync.json* records what was written into the *.scriv* the last time. Deleting the working copy is safe: the next opening asks the first-time question again and reads the *.scriv* anew (what only {{APP}} knew, such as styles and compile settings, is then lost).

---

## 21. Troubleshooting

### Crash reports and system information

If {{APP}} ends because of an error, it writes a **crash report** on your computer before it closes. If its window does not respond for 30 seconds (it hangs; the time is set under **File > Options > Extras > Crash Reports**), it writes a **freeze report** that shows where it is stuck; if it recovers, the report says after how long. Nothing is sent anywhere. The next time you start {{APP}} after a crash, or after you had to end a frozen {{APP}}, a message tells you what happened. If it ended within a minute of starting, the message offers **Start Without the Last Project**, in case the project is the cause.

- **Help > Crash Reports…** lists the crash and freeze reports with their date and kind of error ("freeze (recovered)" when {{APP}} went on working). **Copy to Clipboard** copies the selected one, so you can paste it into a message to whoever looks after {{APP}} for you; **Open Folder** shows the files; **Delete** and **Delete All…** remove them. The newest 20 reports are kept (**File > Options > Extras > Crash Reports** changes the number).
- **File > Options > General > Warnings > Show Internal Log Console** shows the messages of the program and of Qt since it was started (the last 1000), with new ones as they come; useful when something does not work and no message says why.
- **Help > System Information…** shows the same snapshot of the program and the computer that a report carries. It starts with **Things to look at**: what may cause trouble soon, such as a nearly full or read-only drive, a project on a network folder, a backup drive with less free space than the project needs, little free memory, a long automatic-save time, automatic backups switched off, or recent crash and freeze reports. Then come the program version and installed updates and add-ons, the operating system, desktop, screens, processor and memory, the folders with their free space, the settings for saving and backups, and the open project. Use **Copy to Clipboard** or **Save As…** when you report a problem that is not a crash.

A report names files, folders and documents, and the commands and keys used shortly before the error (typed letters are only counted, never recorded). It holds no text of your documents. Your home folder is written as **~**.

If {{APP}} was stopped from outside or the computer was switched off without closing it, there is no report, but the next start tells you as well. In both cases your work is saved up to the last automatic save, a few seconds before the end; look at the document's snapshots or the newest backup if something is missing (sections 11 and 15).

**"The project file cannot be read."** If the project file (*YourTitle*{{FILE}}) is damaged, for example by a crash of the computer while it was saved, {{APP}} offers the copy it keeps of it (from the last time the project opened): **Use the Copy**. The damaged file is kept beside it (*… .damaged-<date>*). Changes to the binder since then (titles, order, labels) are missing; the texts are all there, and documents made since then are put into the folder **Recovered Documents** in Research, named by their first line.

**"The project file contained characters that are not allowed in it."** Versions before 0.20 could save invisible control characters (pasted from other programs into a title or a synopsis), after which the project no longer opened. {{APP}} removes them when it opens the project and tells you; nothing else is changed. Since 0.20 they are never saved.

**A document is shown empty, with the note "The file of this document cannot be read".** Its text file is there but cannot be read (its permissions were changed, or the drive it is on went away). {{APP}} shows it empty and does not let you edit it, so that the file is never overwritten. Check the file's permissions or the drive, then close and open the project again.

**Pressing Page Down or Page Up closed the program.** This was an error in versions before 0.19; update to 0.19 or later. Until then, scroll with the mouse or the scroll bar.

**The program does not start when I double-click it.** Right-click the file > Properties > Permissions and tick "Is executable" (in Dolphin: "Allow executing file as program"). Then double-click it again.

**The file dialogs look different from other KDE programs.** Switch **Use the desktop's file dialogs** on under **File > Options > General** and restart {{APP}}.

**Toolbar icons are hard to see in a dark or light theme.** {{APP}} picks the light or dark set of your desktop's icons to fit the theme. If the desktop has no fitting set, it uses its own icons. If single icons still do not fit, choose **Default (Follow the Desktop)** under **Window > Themes** and set the colors in the desktop's own settings.

**I changed colors and want the theme's back.** **File > Options > Appearance > Reset Colors and Fonts to the Theme**, or the **↺** button next to a single color or font.

**A program update did not start.** {{APP}} switches a failing update off by itself (section 17). You can try installing it again, or keep using the built-in version.

**A compile format is missing.** Check **File > Options > Plugins**: it lists which program the format needs. Install it in Discover and restart {{APP}}.

**The spelling check does not know my language.** Install the matching *hunspell* dictionary package in Discover and choose it under **File > Options > Editing**.

**The project "seems to be open in another window or on another computer."** This happens when the project is in a synced folder that is open elsewhere, or after a crash on another computer. If you are sure it is not open anywhere else, choose **Yes**.

**Alt+drag does not open a copyholder.** Some desktops use Alt+drag for something of their own, such as moving windows. Use **Navigate > Open > in Copyholder** or the right-click menu of the document instead, or change the key in the desktop's window settings.

**After going back to an older program version, a script is plain text without its layout.** Versions before 0.10 do not know script elements and save a script they have shown as ordinary text. Switch to the current version again (**Help > Add-on Manager**). The text is all there; to get the layout back, restore the document from a snapshot or a backup made before (sections 11 and 15), or give the paragraphs their elements again with the pop-up menu in the footer.

**The scratch pad shortcut does nothing in other programs.** Shortcuts set in **File > Options > Keyboard** work only inside {{APP}}. For other programs, the desktop must start the command (section 10, *From every program*). If you move the AppImage, enter the command again (it names the AppImage's place).

**The scratch pad's camera says it cannot read the screen.** Under Wayland only the desktop's own screenshot program may do that. Install *Spectacle* (`sudo apt install kde-spectacle`) and try again.

**Edit > Writing Tools says "No writing tools installed".** The tools are add-ons: install the package of update 0.11 or switch them on in **Help > Add-on Manager**.

**An audio or video file only offers "Open with Default Application".** Playing inside {{APP}} needs the add-on *Media Playback* (in the package of update 0.12); install it, or switch it on in **Help > Add-on Manager**. The line below the button says what is missing.

**A video plays but there is no sound, or an audio file says "No sound output was found".** {{APP}} plays sound through the desktop's sound server (PipeWire or PulseAudio). Check that sound works in other programs and that the right output is chosen in the desktop's audio settings; {{APP}} follows a change of the output at once.

**"This file cannot be played here: it uses H.264 and AAC ..."** The file is in a format licensed under patents, which *Media Playback* leaves out. Install the add-on **Extra Video and Audio Formats** (from the Releases page of the project page, section 1) and restart {{APP}}; or choose **GStreamer** as below.

**A file does not play, or plays without picture.** Some kinds are not part of the built-in decoding (AV1 video, WMA Pro, WMV 9). Choose **GStreamer** under **File > Options > Behaviors > Playback > Decoding**, install the codec packages of the system (for example *gstreamer1.0-libav* and *gstreamer1.0-plugins-good*) and restart {{APP}}, or open the file with another program (right-click the player: **Open with Default Application**).

**A .doc file is added as a file, not as text.** The add-on **Word 97–2003 Import** is not installed or is switched off (**Help > Add-on Manager**); after installing it, restart {{APP}}. A message after the import names what the add-on left out. Word 6.0 and 95 files need LibreOffice (`sudo apt install libreoffice-writer`).

**A web page comes in empty or with the wrong part.** Untick **Only the article** in the Web Page window to take the whole page and delete what you do not need. Pages that need a login or are built entirely by scripts cannot be read without a browser: open the page in your browser, select the text, and drag it into the binder (it becomes a new document). "Secure web pages cannot be fetched" means OpenSSL 3 is missing (`sudo apt install libssl3`, part of every Ubuntu) or the add-on is damaged (install it again).

**The first second of a file is silent or the time stands still at first.** The sound output needs a moment to start; with some sound servers more than a second. Rewind a little (Ctrl+Alt+,) if you missed the start.

**The pages in page view do not match my printed manuscript.** They are not meant to: page view shows your text on sheets while you write. The compile format decides the pages of the finished file (section 13).

**"Scrivener has this project open."** Scrivener leaves a lock file in the *.scriv* while it has the project open, and after a crash. Close the project in Scrivener (on every computer) and open it again here; if Scrivener is not running anywhere, choose **Yes**.

**A project in Scrivener format does not show my latest changes from Scrivener.** {{APP}} reads the *.scriv* again only when the project is opened. Close it here (**File > Close Project**), let Scrivener or the sync service finish, and open it again.

**The menus Insert, Navigate, Project, Documents, and Format are gray.** No project is open. Open or create one (File menu).

**The table of contents of a Word file has no page numbers.** The page numbers are filled in by the word processor when it updates the field; update it there.

**Something went wrong with a document.** Look in the inspector's snapshot tab, or restore a backup (section 15).

## Credits and license

- Main Developer: Claude.AI w. Opus 5.5 Extra

{{APP}} is free software under the Zero-Clause BSD license (0BSD): anyone may use, copy, change, and pass it on, with or without fee, without conditions and without warranty. The About window (in the Help menu) shows the version and the license.

{{APP}} uses parts of other projects (Qt, Hunspell, ICU, the Breeze icons, and the other libraries inside the AppImage), each under its own license. **Help > Third-Party Licenses…** (also a button of the About window) lists them with their full license texts; the same texts are inside the AppImage, in *usr/share/doc/quillbinder/licenses*. The source code of each, exactly in the version used, is in the release **Third-party source code** of the project page (https://github.com/Shelin-boop/Quillbinder_Public/releases/tag/third-party-sources); the window names the file for each library, and the license notes of the add-ons do the same for theirs.

Scrivener and Scapple are names of products of Literature & Latte. {{APP}} is an independent program, not made by or connected with Literature & Latte; it reads and writes their file formats so that your projects can move between the programs.
