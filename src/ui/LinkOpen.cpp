#include "ui/LinkOpen.h"
#include "core/TestSwitches.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMessageBox>
#include <QSet>
#include <QWidget>

namespace qb {
namespace links {

namespace {

std::function<void(const QUrl &)> &opener()
{
    static std::function<void(const QUrl &)> fn;
    return fn;
}

bool launch(const QUrl &url)
{
    if (opener()) {
        opener()(url);
        return true;
    }
    return QDesktopServices::openUrl(url);
}

QString tr(const char *s) { return QCoreApplication::translate("qb::links", s); }

bool ask(QWidget *parent, const QString &title, const QString &text)
{
    if (QB_TEST_ENV_SET("QB_NO_DIALOGS"))
        return false; // nobody to ask: not opened
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::Open | QMessageBox::Cancel, parent);
    box.setObjectName(QStringLiteral("linkQuestion"));
    box.setDefaultButton(QMessageBox::Cancel);
    return box.exec() == QMessageBox::Open;
}

void refuse(QWidget *parent, const QString &text)
{
    if (QB_TEST_ENV_SET("QB_NO_DIALOGS"))
        return;
    QMessageBox box(QMessageBox::Warning, tr("Link Not Opened"), text, QMessageBox::Ok, parent);
    box.setObjectName(QStringLiteral("linkRefused"));
    box.exec();
}

} // namespace

QString breakable(const QString &s)
{
    // a zero-width space after each separator, and after every 24 characters without one, so a long name wraps
    // wherever it must instead of running past the window's edge
    QString out;
    out.reserve(s.size() + s.size() / 8);
    int run = 0;
    for (const QChar c : s) {
        out += c;
        ++run;
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char('-') || c == QLatin1Char('_')
            || c == QLatin1Char('.') || c == QLatin1Char(' ')) {
            if (c != QLatin1Char(' '))
                out += QChar(0x200B);
            run = 0;
        } else if (run >= 24) {
            out += QChar(0x200B);
            run = 0;
        }
    }
    return out;
}

// 0.90.1 (the usability review of 0.90.0, UR-05; ISO 9241-110, self-descriptiveness): the whole target can be read
// before it opens, however long (the name wraps anywhere, its ending stays in view), and the question says what kind
// of file it is
QString fileQuestion(const QString &path)
{
    const QFileInfo fi(path);
    QString kind;
    if (fi.isDir()) {
        kind = tr("a folder");
    } else {
        const QMimeType mt = QMimeDatabase().mimeTypeForFile(fi, fi.exists() ? QMimeDatabase::MatchDefault : QMimeDatabase::MatchExtension);
        const QString suffix = fi.suffix().isEmpty() ? QString() : QStringLiteral(" (.") + fi.suffix() + QLatin1Char(')');
        kind = (mt.isValid() && !mt.isDefault() ? mt.comment() : tr("a file of unknown kind")) + suffix;
    }
    QString text = tr("This link points to %1 on this computer.").arg(fi.isDir() ? tr("a folder") : tr("a file"));
    text += QStringLiteral("\n\n") + tr("Kind: %1").arg(kind);
    text += QStringLiteral("\n") + tr("Name: %1").arg(breakable(fi.fileName()));
    text += QStringLiteral("\n") + tr("Folder: %1").arg(breakable(QDir::toNativeSeparators(fi.absolutePath())));
    if (!fi.exists())
        text += QStringLiteral("\n\n") + tr("It is not there now (moved, renamed, or on a drive that is not connected).");
    text += QStringLiteral("\n\n") + tr("Open it?");
    return text;
}

QUrl urlOf(const QString &href)
{
    const QString h = href.trimmed();
    if (h.isEmpty())
        return QUrl();
    // a link with a scheme as written (QUrl::fromUserInput would turn "a:b" into something else)
    const QUrl direct(h, QUrl::StrictMode);
    if (direct.isValid() && !direct.scheme().isEmpty() && direct.scheme().size() > 1)
        return direct;
    return QUrl::fromUserInput(h);
}

bool isProgramFile(const QString &path)
{
    static const QSet<QString> endings = {
        // Windows
        QStringLiteral("exe"), QStringLiteral("com"), QStringLiteral("bat"), QStringLiteral("cmd"), QStringLiteral("msi"),
        QStringLiteral("msp"), QStringLiteral("mst"), QStringLiteral("scr"), QStringLiteral("pif"), QStringLiteral("cpl"),
        QStringLiteral("ps1"), QStringLiteral("psm1"), QStringLiteral("psd1"), QStringLiteral("vbs"), QStringLiteral("vbe"),
        QStringLiteral("js"), QStringLiteral("jse"), QStringLiteral("wsf"), QStringLiteral("wsh"), QStringLiteral("hta"),
        QStringLiteral("lnk"), QStringLiteral("url"), QStringLiteral("reg"), QStringLiteral("inf"), QStringLiteral("msc"),
        QStringLiteral("chm"), QStringLiteral("gadget"), QStringLiteral("application"), QStringLiteral("appref-ms"),
        QStringLiteral("library-ms"), QStringLiteral("settingcontent-ms"), QStringLiteral("xll"), QStringLiteral("dll"),
        QStringLiteral("sys"), QStringLiteral("ocx"), QStringLiteral("appx"), QStringLiteral("msix"),
        QStringLiteral("appxbundle"), QStringLiteral("msixbundle"),
        // Linux and other Unix systems
        QStringLiteral("desktop"), QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("zsh"), QStringLiteral("csh"),
        QStringLiteral("ksh"), QStringLiteral("fish"), QStringLiteral("run"), QStringLiteral("bin"), QStringLiteral("elf"),
        QStringLiteral("so"), QStringLiteral("appimage"), QStringLiteral("deb"), QStringLiteral("rpm"), QStringLiteral("snap"),
        QStringLiteral("flatpak"), QStringLiteral("flatpakref"), QStringLiteral("qbpkg"),
        // macOS
        QStringLiteral("app"), QStringLiteral("command"), QStringLiteral("tool"), QStringLiteral("scpt"),
        QStringLiteral("pkg"), QStringLiteral("dmg"), QStringLiteral("webloc"),
        // scripts that run with an interpreter
        QStringLiteral("py"), QStringLiteral("pyw"), QStringLiteral("pyz"), QStringLiteral("pl"), QStringLiteral("rb"),
        QStringLiteral("php"), QStringLiteral("jar"), QStringLiteral("jnlp"), QStringLiteral("lua"), QStringLiteral("tcl")};
    const QFileInfo fi(path);
    // the last ending decides, as it does for the desktop ("letter.pdf.exe" is a program)
    if (endings.contains(fi.suffix().toLower()))
        return true;
    if (fi.isFile() && fi.isExecutable()) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray head = f.read(4);
            if (head.startsWith("#!") || head == QByteArray("\x7f" "ELF", 4) || head.startsWith("MZ"))
                return true;
        }
    }
    return false;
}

Decision judge(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty())
        return Decision::Nothing;
    const QString scheme = url.scheme().toLower();
    if ((scheme == QLatin1String("http") || scheme == QLatin1String("https")) && !url.host().isEmpty())
        return Decision::Open;
    if (scheme == QLatin1String("mailto") && !url.path().isEmpty())
        return Decision::Open;
    if (url.isLocalFile() && url.host().isEmpty() && isProgramFile(url.toLocalFile()))
        return Decision::Refuse;
    return Decision::Ask;
}

bool open(QWidget *parent, const QString &href)
{
    const QUrl url = urlOf(href);
    switch (judge(url)) {
    case Decision::Nothing:
        return false;
    case Decision::Open:
        return launch(url);
    case Decision::Refuse:
        refuse(parent, tr("This link points to a program or a file that starts one:\n\n%1\n\nSuch files are not opened "
                          "from a link. If you trust it, open it yourself from your file manager.")
                           .arg(url.toLocalFile()));
        return false;
    case Decision::Ask:
        break;
    }
    QString text;
    if (url.isLocalFile() && url.host().isEmpty())
        text = fileQuestion(url.toLocalFile());
    else if (url.isLocalFile())
        text = tr("This link points to a file on another computer of the network:\n\n%1\n\nOpening it connects to that "
                  "computer, which may then learn your user name. Open it?")
                   .arg(url.toString());
    else
        text = tr("This link is not a web or mail address; it is handed to whatever program is set up on this computer "
                  "for \"%1\":\n\n%2\n\nOpen it?")
                   .arg(url.scheme(), url.toString());
    if (!ask(parent, tr("Open Link"), text))
        return false;
    return launch(url);
}

bool openFile(QWidget *parent, const QString &path)
{
    if (path.isEmpty())
        return false;
    if (isProgramFile(path)) {
        refuse(parent, tr("This file is a program or starts one:\n\n%1\n\nIt is not opened from here. If you trust it, "
                          "open it yourself from your file manager.")
                           .arg(path));
        return false;
    }
    return launch(QUrl::fromLocalFile(path));
}

void setOpenerForTests(std::function<void(const QUrl &)> fn) { opener() = std::move(fn); }

} // namespace links
} // namespace qb
