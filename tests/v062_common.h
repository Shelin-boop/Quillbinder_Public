#pragma once
// Helpers shared by the parts of qb_v062_tests.
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/SettingsFile.h"
#include "core/SettingsLayer.h"
#include "compile/PluginManager.h"
#include "text/DisplayFormat.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/Commands.h"
#include "ui/EditorPane.h"
#include "ui/FirstStartDialog.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/ProjectLook.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextTable>
#include <QTimer>
#include <cstdio>
#include <functional>

using namespace qb;

inline int failures = 0;
inline QString outDir;

inline void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(3000)));
    }
    std::fflush(stdout);
}

inline void note(const QString &s)
{
    std::printf("      %s\n", qPrintable(s));
    std::fflush(stdout);
}

inline void settle(int ms = 150)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
}

inline void shot(QWidget *w, const QString &name)
{
    if (w)
        w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

template <typename T>
inline void withModal(int delayMs, std::function<void(T *)> fn)
{
    QTimer::singleShot(delayMs, qApp, [fn] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<T *>(tl))
                if (d->isVisible()) {
                    fn(d);
                    return;
                }
        std::printf("      (the modal window did not open)\n");
        if (QWidget *m = QApplication::activeModalWidget())
            m->close();
    });
}

inline BinderItem *textDoc(Project *p, BinderItem *parent, const QString &title, const QStringList &paragraphs)
{
    BinderItem *it = p->model()->createItem(parent, -1, ItemType::Text, title);
    QTextCursor c(p->docs()->text(it->id));
    for (int i = 0; i < paragraphs.size(); ++i) {
        if (i > 0)
            c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(paragraphs[i]);
    }
    return it;
}

inline std::unique_ptr<Project> blankProject(const QString &dir, const QString &name)
{
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;
    QString err;
    auto p = templates::createProject(blank, dir, name, &err);
    if (!p)
        note(QStringLiteral("project not made: %1").arg(err));
    return p;
}

inline QVariant programValue(const QString &key)
{
    QSettings s(settingslayer::programFile(), QSettings::IniFormat);
    return s.value(key);
}

inline EditorPane *visiblePane(MainWindow &w)
{
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible())
            return ep;
    return nullptr;
}

void settingsChecks();
void tableChecks();
