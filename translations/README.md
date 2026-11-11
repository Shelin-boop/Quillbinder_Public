# Translations

`quillbinder_de.ts` holds the German texts of the program (0.70.0), written for Quillbinder and released with it
under its license (Apache License 2.0). It also translates the few texts of Qt's own dialogs and buttons that the program shows;
those translations were written anew here (the source strings are Qt's, the German is this project's).

- After texts in the source changed: `lupdate -locations none -no-obsolete -source-language en -target-language de src plugins
  -ts translations/quillbinder_de.ts`, translate the new entries (marked unfinished), then
  `lrelease translations/quillbinder_de.ts -qm translations/quillbinder_de.qm` (qt6-l10n-tools).
- `qt_de.ts`: the German of the texts of Qt's own buttons and dialogs that the program shows (QPlatformTheme,
  QFileDialog, the context menu of text fields, key names in menus ...), kept apart because lupdate does not
  know them (it would drop them as obsolete). Edit by hand, then `lrelease translations/qt_de.ts -qm
  translations/qt_de.qm`.
- `quillbinder_de.qm` and `qt_de.qm` are the compiled files the program carries as resources (`translations.qrc`).
- The suite `qb_v0700_tests` checks that every entry is translated and that the .qm matches the .ts.
