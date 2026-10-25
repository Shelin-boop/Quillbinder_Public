#include "ui/LicensesDialog.h"

#include "AppInfo.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSplitter>
#include <QVBoxLayout>

namespace qb {

static QString readResource(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

void showThirdPartyLicenses(QWidget *parent)
{
    static QPointer<QDialog> win;
    if (!win) {
        win = new QDialog(parent);
        win->setAttribute(Qt::WA_DeleteOnClose);
        win->setObjectName(QStringLiteral("licensesWindow"));
        win->setWindowTitle(QObject::tr("Third-Party Licenses"));
        win->resize(980, 700);
        auto *l = new QVBoxLayout(win);
        auto *intro = new QLabel(
            QObject::tr("<p>%1 itself is under the <b>Zero-Clause BSD license (0BSD)</b>: anyone may use, copy, change and "
                        "pass it on, without conditions. The AppImage also carries parts of other projects, each under its "
                        "own license; they are listed here with their full license texts (the same texts are in the AppImage "
                        "under <i>usr/share/doc/quillbinder/licenses</i>). The libraries are those of Ubuntu 24.04; the "
                        "source code of each is available from Ubuntu as named below its license. You may replace the "
                        "libraries inside the AppImage with your own builds. Add-ons carry their own license files.</p>")
                .arg(QLatin1String(QB_APP_NAME)),
            win);
        intro->setWordWrap(true);
        intro->setTextFormat(Qt::RichText);
        l->addWidget(intro);
        auto *split = new QSplitter(win);
        auto *list = new QListWidget(split);
        list->setObjectName(QStringLiteral("licensesList"));
        auto *text = new QPlainTextEdit(split);
        text->setObjectName(QStringLiteral("licensesText"));
        text->setReadOnly(true);
        text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        split->setStretchFactor(0, 0);
        split->setStretchFactor(1, 1);
        split->setSizes({300, 680});
        l->addWidget(split, 1);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, win);
        QObject::connect(bb, &QDialogButtonBox::rejected, win, &QDialog::close);
        l->addWidget(bb);

        const QJsonObject index = QJsonDocument::fromJson(readResource(QStringLiteral(":/licenses/index.json")).toUtf8()).object();
        for (const QJsonValue &v : index.value(QStringLiteral("components")).toArray()) {
            const QJsonObject c = v.toObject();
            const QString version = c.value(QStringLiteral("version")).toString();
            auto *it = new QListWidgetItem(c.value(QStringLiteral("name")).toString()
                                               + (version.isEmpty() ? QString() : QLatin1Char(' ') + version),
                                           list);
            QString head = QObject::tr("%1\nLicense: %2\nUsed for: %3")
                               .arg(it->text(), c.value(QStringLiteral("license")).toString(),
                                    c.value(QStringLiteral("where")).toString());
            const QString src = c.value(QStringLiteral("source")).toString();
            if (!src.isEmpty())
                head += QObject::tr("\nSource: %1").arg(src);
            it->setToolTip(c.value(QStringLiteral("license")).toString());
            it->setData(Qt::UserRole, QStringLiteral(":/licenses/texts/") + c.value(QStringLiteral("text")).toString());
            it->setData(Qt::UserRole + 1, head);
        }
        const QJsonArray common = index.value(QStringLiteral("commonLicenses")).toArray();
        if (!common.isEmpty()) {
            auto *sep = new QListWidgetItem(QObject::tr("Full texts of common licenses:"), list);
            sep->setFlags(Qt::NoItemFlags);
            QFont f = sep->font();
            f.setBold(true);
            sep->setFont(f);
            for (const QJsonValue &v : common) {
                const QJsonObject c = v.toObject();
                auto *it = new QListWidgetItem(QStringLiteral("  ") + c.value(QStringLiteral("name")).toString(), list);
                it->setData(Qt::UserRole, QStringLiteral(":/licenses/texts/") + c.value(QStringLiteral("text")).toString());
                it->setData(Qt::UserRole + 1, QObject::tr("The full text of %1, which licenses above refer to.")
                                                  .arg(c.value(QStringLiteral("name")).toString()));
            }
        }
        QObject::connect(list, &QListWidget::currentItemChanged, text, [text](QListWidgetItem *cur) {
            if (!cur || cur->data(Qt::UserRole).toString().isEmpty())
                return;
            text->setPlainText(cur->data(Qt::UserRole + 1).toString() + QStringLiteral("\n\n")
                               + QString(60, QChar(0x2500)) + QStringLiteral("\n\n")
                               + readResource(cur->data(Qt::UserRole).toString()));
        });
        if (list->count())
            list->setCurrentRow(0);
    }
    win->show();
    win->raise();
    win->activateWindow();
}

} // namespace qb
