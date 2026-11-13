#pragma once
// Opening links that lead out of the program (0.64.1, the external audit's X-4): links in texts, mind
// maps, bookmarks and imported web pages may come from a project, a web page or a Word file someone else
// made. Web and mail links (http, https, mailto) open at once; any other target (a file, a folder, a
// network share, another program's scheme) is shown in full and opened only when the writer says so;
// program files (programs, scripts, shortcuts, installers) are never opened.
#include <QString>
#include <QUrl>

#include <functional>

class QWidget;

namespace qb {
namespace links {

enum class Decision { Open, Ask, Refuse, Nothing };

// The URL a link's text stands for (as typed: "www.example.com" is a web address).
QUrl urlOf(const QString &href);
// What open() does with url, without asking.
Decision judge(const QUrl &url);
// A local file that is a program or starts one when opened: by its name's last ending, or a file
// marked executable that begins as a program does ("#!", ELF, "MZ").
bool isProgramFile(const QString &path);

// Opens href as judged above; parent is the window of the questions. True when it was opened.
bool open(QWidget *parent, const QString &href);
// A file of the project (a picture, a PDF, a recording) opened with the desktop's program for it;
// a program file is refused. True when it was opened.
bool openFile(QWidget *parent, const QString &path);

// 0.90.1 (UR-05): the question before a file or folder on this computer opens: its kind, its whole name and its
// folder, which wrap anywhere (zero-width spaces), so a long name's ending can be read
QString fileQuestion(const QString &path);
// s with a zero-width space after each separator and after every 24 characters without one
QString breakable(const QString &s);

// Tests: called instead of the desktop's URL handler.
void setOpenerForTests(std::function<void(const QUrl &)> fn);

} // namespace links
} // namespace qb
