// The shared choices of the portable program (W4b); see PortableSetup.h.
#include "ui/PortableSetup.h"
#include "core/Portable.h"
#include "ui/HintLabel.h"
#include "ui/WinShell.h"

#include <QButtonGroup>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace qb {

namespace {

QString key(winshell::Item item)
{
    switch (item) {
    case winshell::Item::StartMenu:
        return QStringLiteral("startMenu");
    case winshell::Item::Desktop:
        return QStringLiteral("desktop");
    case winshell::Item::FileTypes:
        break;
    }
    return QStringLiteral("fileTypes");
}

} // namespace

QString PortableSetup::shownPath(const QString &path)
{
    QString out = QDir::toNativeSeparators(path);
    const QChar sep = QDir::separator();
    out.replace(sep, QString(sep) + QChar(0x200B)); // zero-width space: a place to break the line
    return out;
}

PortableSetup::PortableSetup(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("portableSetup"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);

    // ---- notes and transcriptions
    auto *notes = new QGroupBox(tr("Scratch Pad Notes and Transcriptions"), this);
    auto *nv = new QVBoxLayout(notes);
    const portable::Notes now = portable::notes();
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty())
        docs = QDir::homePath();
    m_documents = new QRadioButton(tr("In Documents"), notes);
    m_documents->setObjectName(QStringLiteral("portNotesDocuments"));
    m_program = new QRadioButton(tr("In the program's folder"), notes);
    m_program->setObjectName(QStringLiteral("portNotesProgram"));
    // the folders under the choices, as lines that may break (paths can be long)
    auto pathLine = [notes](const QString &path) {
        auto *l = new QLabel(shownPath(path), notes);
        l->setWordWrap(true);
        l->setContentsMargins(24, 0, 0, 0);
        hintlabel::style(l);
        return l;
    };
    m_own = new QRadioButton(tr("In a folder of your own:"), notes);
    m_own->setObjectName(QStringLiteral("portNotesOwn"));
    auto *group = new QButtonGroup(this);
    for (QRadioButton *b : {m_documents, m_program, m_own})
        group->addButton(b);
    nv->addWidget(m_documents);
    nv->addWidget(pathLine(docs + QStringLiteral("/Quillbinder Scratch Pad, ") + docs + QStringLiteral("/Quillbinder Transcriptions")));
    nv->addWidget(m_program);
    nv->addWidget(pathLine(portable::programFolder() + QStringLiteral("/Scratch Pad, ") + portable::programFolder()
                           + QStringLiteral("/Transcriptions")));
    auto *row = new QHBoxLayout;
    row->addWidget(m_own);
    m_ownFolder = new QLineEdit(QDir::toNativeSeparators(now.own), notes);
    m_ownFolder->setObjectName(QStringLiteral("portNotesOwnFolder"));
    auto *choose = new QPushButton(tr("Choose…"), notes);
    choose->setObjectName(QStringLiteral("portNotesChoose"));
    row->addWidget(m_ownFolder, 1);
    row->addWidget(choose);
    nv->addLayout(row);
    (now.place == portable::NotesPlace::ProgramFolder ? m_program
     : now.place == portable::NotesPlace::Own         ? m_own
                                                      : m_documents)
        ->setChecked(true);
    auto *nh = new QLabel(tr("Your notes of the Scratch Pad and the texts of Speech Transcription are your writing: in "
                             "Documents or a folder of your own they stay when the program's folder is deleted or replaced. "
                             "A change here gives both tools the new place; what they wrote so far stays where it is."),
                          notes);
    nh->setObjectName(QStringLiteral("portNotesHint"));
    nh->setWordWrap(true);
    hintlabel::style(nh);
    nv->addWidget(nh);
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Folder for Notes and Transcriptions"),
                                                            m_ownFolder->text().isEmpty() ? QDir::homePath() : m_ownFolder->text());
        if (!d.isEmpty()) {
            m_ownFolder->setText(QDir::toNativeSeparators(d));
            m_own->setChecked(true);
        }
    });
    auto ownOn = [this] { m_ownFolder->setEnabled(m_own->isChecked()); };
    connect(m_own, &QRadioButton::toggled, this, ownOn);
    ownOn();
    v->addWidget(notes);

    // ---- what goes outside the folder, only when asked for
    auto *shell = new QGroupBox(tr("Windows"), this);
    shell->setObjectName(QStringLiteral("portShellGroup"));
    auto *grid = new QGridLayout(shell);
    const struct
    {
        winshell::Item item;
        QString label;
    } rows[] = {{winshell::Item::StartMenu, tr("Entry in the Start menu")},
                {winshell::Item::Desktop, tr("Shortcut on the desktop")},
                {winshell::Item::FileTypes, tr("Project files: .qbx files open with Quillbinder; it is offered in \"Open with\" "
                                               "for Scrivener's .scrivx files")}};
    int r = 0;
    for (const auto &it : rows) {
        auto *label = new QLabel(it.label, shell);
        label->setWordWrap(true);
        auto *state = new QLabel(shell);
        state->setObjectName(QStringLiteral("portShellState_") + key(it.item));
        auto *button = new QPushButton(shell);
        button->setObjectName(QStringLiteral("portShell_") + key(it.item));
        grid->addWidget(label, r, 0);
        grid->addWidget(state, r, 1);
        grid->addWidget(button, r, 2);
        const int item = int(it.item);
        connect(button, &QPushButton::clicked, this, [this, item] {
            const auto i = winshell::Item(item);
            QString err;
            const bool remove = winshell::state(i) == winshell::State::Here;
            const bool ok = remove ? winshell::remove(i, &err) : winshell::add(i, &err);
            if (!ok)
                QMessageBox::warning(this, tr("Windows"),
                                     (remove ? tr("This could not be removed: %1.") : tr("This could not be added: %1.")).arg(err));
            refreshShell();
        });
        m_shell.append({item, state, button});
        ++r;
    }
    grid->setColumnStretch(0, 1);
    auto *sh = new QLabel(tr("These are the only things the program puts outside its folder, and only when you ask for them "
                             "here. Remove them before you delete or move the program's folder. To pin Quillbinder to the "
                             "taskbar: start it, then right-click its button in the taskbar and choose \"Pin to taskbar\"."),
                          shell);
    sh->setObjectName(QStringLiteral("portShellHint"));
    sh->setWordWrap(true);
    hintlabel::style(sh);
    grid->addWidget(sh, r, 0, 1, 3);
    v->addWidget(shell);
    refreshShell();
}

void PortableSetup::refreshShell()
{
    const bool can = winshell::available();
    for (const ShellRow &row : m_shell) {
        const winshell::State st = winshell::state(winshell::Item(row.item));
        if (!can)
            row.state->setText(tr("only on Windows"));
        else if (st == winshell::State::Here)
            row.state->setText(tr("added"));
        else if (st == winshell::State::Elsewhere)
            row.state->setText(tr("there, for another copy of the program"));
        else
            row.state->setText(tr("not added"));
        row.button->setText(st == winshell::State::Here ? tr("Remove") : st == winshell::State::Elsewhere ? tr("Point Here") : tr("Add"));
        row.button->setEnabled(can);
    }
}

void PortableSetup::apply()
{
    portable::Notes n;
    if (m_program->isChecked())
        n.place = portable::NotesPlace::ProgramFolder;
    else if (m_own->isChecked() && !m_ownFolder->text().trimmed().isEmpty()) {
        n.place = portable::NotesPlace::Own;
        n.own = QDir::fromNativeSeparators(m_ownFolder->text().trimmed());
    }
    const portable::Notes before = portable::notes();
    // unchanged: a folder chosen in one of the tools themselves stays
    if (before.place == n.place && (n.place != portable::NotesPlace::Own || before.own == n.own))
        return;
    portable::setNotes(n);
}

} // namespace qb
