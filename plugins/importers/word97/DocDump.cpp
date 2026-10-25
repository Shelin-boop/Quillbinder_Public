// qb_doc_dump <file.doc> [--plain]: what the Word 97–2003 reader makes of a file.
#include "WordReader.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2)
        return 2;
    QFile f(args.at(1));
    if (!f.open(QIODevice::ReadOnly))
        return 2;
    const bool plain = args.contains(QStringLiteral("--plain"));
    qb::importers::Content c;
    QString err;
    const qbdoc::Failure fail = qbdoc::readWord97(f.readAll(), &c, &err);
    QTextStream out(stdout);
    if (fail != qbdoc::Failure::None) {
        out << "FAILED " << int(fail) << ": " << err << "\n";
        return 1;
    }
    for (const auto &p : c.paragraphs) {
        QString t;
        for (const auto &r : p.runs) {
            if (r.image >= 0)
                t += plain ? QString() : QStringLiteral("[picture %1]").arg(r.image);
            else if (r.note >= 0)
                t += plain ? QString() : QStringLiteral("[note %1]").arg(r.note);
            else if (plain)
                t += r.text;
            else {
                QString s = r.text;
                if (r.bold) s = QStringLiteral("**") + s + QStringLiteral("**");
                if (r.italic) s = QStringLiteral("_") + s + QStringLiteral("_");
                if (!r.link.isEmpty()) s = QStringLiteral("[") + s + QStringLiteral("](") + r.link + QStringLiteral(")");
                if (r.comment >= 0) s = QStringLiteral("{c%1:").arg(r.comment) + s + QStringLiteral("}");
                t += s;
            }
        }
        t.replace(QChar::LineSeparator, plain ? QStringLiteral("\n") : QStringLiteral("↵"));
        if (!plain) {
            QString pre;
            if (!p.style.isEmpty()) pre += QStringLiteral("<") + p.style + QStringLiteral("> ");
            if (p.headingLevel) pre += QStringLiteral("<H%1> ").arg(p.headingLevel);
            if (p.listLevel >= 0) pre += QStringLiteral("<%1%2> ").arg(p.numbered ? QStringLiteral("#") : QStringLiteral("*")).arg(p.listLevel);
            if (p.tableRow) pre += QStringLiteral("<row> ");
            if (p.pageBreakBefore) pre += QStringLiteral("<pb> ");
            if (p.align) pre += QStringLiteral("<a%1> ").arg(p.align);
            t = pre + t;
        }
        out << t << "\n";
    }
    if (!plain) {
        for (int i = 0; i < c.notes.size(); ++i)
            out << "NOTE " << i << " kind " << int(c.notes[i].kind) << " (" << c.notes[i].author << "): " << c.notes[i].text << "\n";
        for (int i = 0; i < c.images.size(); ++i)
            out << "IMAGE " << i << " " << c.images[i].data.size() << " bytes " << c.images[i].width << "x" << c.images[i].height << "pt\n";
        for (const QString &w : c.warnings)
            out << "WARNING " << w << "\n";
    }
    return 0;
}
