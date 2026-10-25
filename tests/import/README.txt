Test files of tests/m12_tests.cpp (import and export)

features.doc, chapters.doc   made from features.fodt and chapters.fodt with LibreOffice:
                             soffice --headless --convert-to doc:"MS Word 97" features.fodt
docx-saved-as.doc            features.fodt converted to .docx and given the name .doc
word6-fake.doc               features.doc with the version number of the Word 6 format
                             (nFib 0x0065) written into its WordDocument stream, so the
                             reader hands it to LibreOffice
article.html                 an invented news page with menus, a cookie notice, comments,
                             share buttons and a lazy-loaded picture around the article
latin1.html                  a page in ISO-8859-1 with Windows-1252 dashes
script-only.html             a page whose text is only in its JSON-LD data
notes.txt, *.png, *.jpg, *.gif   plain text and pictures the test server hands out
