#include "NameGenerator.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTextStream>
#include <QVBoxLayout>

using namespace qb::tools;

static const char *kTool = "name-generator";

// ------------------------------------------------------------------ plugin

QList<Command> NameGeneratorTool::commands() const
{
    Command c;
    c.key = QStringLiteral("name-generator.show");
    c.text = tr("&Name Generator…");
    c.place = QStringLiteral("edit/writing-tools");
    c.icon = QStringLiteral("im-user");
    c.toolTip = tr("Makes names for characters from lists of many origins");
    return {c};
}

void NameGeneratorTool::run(const QString &key)
{
    if (key != QLatin1String("name-generator.show") || !m_host)
        return;
    if (!m_window) {
        m_window = new NameGeneratorWindow(m_host, m_host->window());
        m_window->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

void NameGeneratorTool::shutdown()
{
    if (m_window)
        m_window->close();
}

// ------------------------------------------------------------------ window

static QSlider *percentSlider(QWidget *parent, const QString &name)
{
    auto *s = new QSlider(Qt::Horizontal, parent);
    s->setObjectName(name);
    s->setRange(0, 100);
    s->setPageStep(10);
    s->setTickInterval(25);
    s->setTickPosition(QSlider::TicksBelow);
    return s;
}

NameGeneratorWindow::NameGeneratorWindow(ToolHost *host, QWidget *parent) : QWidget(parent, Qt::Window), m_host(host)
{
    setObjectName(QStringLiteral("nameGenerator"));
    setWindowTitle(tr("Name Generator"));
    auto *split = new QSplitter(Qt::Horizontal, this);
    auto *outer = new QVBoxLayout(this);
    outer->addWidget(split, 1);

    // ---- options
    auto *left = new QWidget(split);
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    auto *form = new QFormLayout;
    m_gender = new QComboBox(left);
    m_gender->setObjectName(QStringLiteral("ngGender"));
    m_gender->addItem(tr("Male and female"), int(namegen::Gender::Both));
    m_gender->addItem(tr("Female"), int(namegen::Gender::Female));
    m_gender->addItem(tr("Male"), int(namegen::Gender::Male));
    form->addRow(tr("Gender:"), m_gender);
    m_count = new QSpinBox(left);
    m_count->setObjectName(QStringLiteral("ngCount"));
    m_count->setRange(1, 500);
    m_count->setValue(25);
    form->addRow(tr("Number of names:"), m_count);
    m_show = new QComboBox(left);
    m_show->setObjectName(QStringLiteral("ngShow"));
    m_show->addItem(tr("Forename and surname"), int(namegen::Show::Full));
    m_show->addItem(tr("Forename only"), int(namegen::Show::ForenameOnly));
    m_show->addItem(tr("Surname only"), int(namegen::Show::SurnameOnly));
    m_show->addItem(tr("Initials and surname"), int(namegen::Show::InitialAndSurname));
    form->addRow(tr("Show:"), m_show);
    m_middle = new QCheckBox(tr("With a middle name"), left);
    m_middle->setObjectName(QStringLiteral("ngMiddle"));
    form->addRow(QString(), m_middle);
    lv->addLayout(form);

    auto *lists = new QHBoxLayout;
    auto *foreBox = new QGroupBox(tr("Forenames from"), left);
    auto *fb = new QVBoxLayout(foreBox);
    m_fore = new QListWidget(foreBox);
    m_fore->setObjectName(QStringLiteral("ngForenames"));
    fb->addWidget(m_fore);
    auto *foreRow = new QHBoxLayout;
    auto *foreAll = new QPushButton(tr("All"), foreBox);
    auto *foreNone = new QPushButton(tr("None"), foreBox);
    foreRow->addWidget(foreAll);
    foreRow->addWidget(foreNone);
    fb->addLayout(foreRow);
    auto *surBox = new QGroupBox(tr("Surnames from"), left);
    auto *sb = new QVBoxLayout(surBox);
    m_same = new QCheckBox(tr("The list of the forename"), surBox);
    m_same->setObjectName(QStringLiteral("ngSameOrigin"));
    m_same->setChecked(true);
    sb->addWidget(m_same);
    m_sur = new QListWidget(surBox);
    m_sur->setObjectName(QStringLiteral("ngSurnames"));
    sb->addWidget(m_sur);
    auto *surRow = new QHBoxLayout;
    auto *surAll = new QPushButton(tr("All"), surBox);
    auto *surNone = new QPushButton(tr("None"), surBox);
    surRow->addWidget(surAll);
    surRow->addWidget(surNone);
    sb->addLayout(surRow);
    lists->addWidget(foreBox);
    lists->addWidget(surBox);
    lv->addLayout(lists, 1);

    auto *fine = new QGroupBox(tr("Fine-tuning"), left);
    auto *ff = new QFormLayout(fine);
    m_foreObsc = percentSlider(fine, QStringLiteral("ngForeObscurity"));
    m_surObsc = percentSlider(fine, QStringLiteral("ngSurObscurity"));
    const QString obscTip = tr("Left: names that are common, right: rare ones. Works with the lists marked “by frequency”; "
                               "the others have no frequencies and every name is equally likely.");
    m_foreObsc->setToolTip(obscTip);
    m_surObsc->setToolTip(obscTip);
    ff->addRow(tr("Forenames: common ↔ rare"), m_foreObsc);
    ff->addRow(tr("Surnames: common ↔ rare"), m_surObsc);
    m_allit = percentSlider(fine, QStringLiteral("ngAlliteration"));
    m_allit->setToolTip(tr("Share of names whose surname starts with the forename's letter (Peter Parker)"));
    ff->addRow(tr("Alliteration:"), m_allit);
    m_double = percentSlider(fine, QStringLiteral("ngDoubleBarrelled"));
    m_double->setToolTip(tr("Share of names with two surnames joined by a hyphen"));
    ff->addRow(tr("Double surnames:"), m_double);
    m_foreStarts = new QLineEdit(fine);
    m_foreStarts->setObjectName(QStringLiteral("ngForeStarts"));
    m_foreStarts->setPlaceholderText(tr("any letters"));
    ff->addRow(tr("Forename starts with:"), m_foreStarts);
    m_surStarts = new QLineEdit(fine);
    m_surStarts->setObjectName(QStringLiteral("ngSurStarts"));
    m_surStarts->setPlaceholderText(tr("any letters"));
    ff->addRow(tr("Surname starts with:"), m_surStarts);
    lv->addWidget(fine);

    auto *own = new QHBoxLayout;
    auto *addList = new QPushButton(tr("Add Own List…"), left);
    addList->setObjectName(QStringLiteral("ngAddList"));
    addList->setToolTip(tr("A text file with one name per line becomes a list of its own"));
    m_removeList = new QPushButton(tr("Remove Own List"), left);
    m_removeList->setObjectName(QStringLiteral("ngRemoveList"));
    own->addWidget(addList);
    own->addWidget(m_removeList);
    own->addStretch(1);
    lv->addLayout(own);

    // ---- results
    auto *right = new QWidget(split);
    auto *rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    auto *gen = new QPushButton(tr("Generate Names"), right);
    gen->setObjectName(QStringLiteral("ngGenerate"));
    gen->setDefault(true);
    rv->addWidget(gen);
    m_results = new QListWidget(right);
    m_results->setObjectName(QStringLiteral("ngResults"));
    m_results->setSelectionMode(QAbstractItemView::ExtendedSelection);
    rv->addWidget(m_results, 1);
    m_status = new QLabel(right);
    m_status->setObjectName(QStringLiteral("ngStatus"));
    m_status->setWordWrap(true);
    rv->addWidget(m_status);
    auto *row = new QHBoxLayout;
    m_copy = new QPushButton(tr("Copy"), right);
    m_copy->setObjectName(QStringLiteral("ngCopy"));
    m_copy->setToolTip(tr("Copies the selected names (or all of them), one per line"));
    m_insert = new QPushButton(tr("Insert in Text"), right);
    m_insert->setObjectName(QStringLiteral("ngInsert"));
    m_insert->setToolTip(tr("Puts the selected name at the cursor of the editor"));
    row->addWidget(m_copy);
    row->addWidget(m_insert);
    rv->addLayout(row);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({480, 260});
    resize(820, 640);

    connect(gen, &QPushButton::clicked, this, &NameGeneratorWindow::generate);
    connect(m_copy, &QPushButton::clicked, this, &NameGeneratorWindow::copyNames);
    connect(m_insert, &QPushButton::clicked, this, &NameGeneratorWindow::insertNames);
    connect(m_results, &QListWidget::itemDoubleClicked, this, &NameGeneratorWindow::insertNames);
    connect(m_results, &QListWidget::itemSelectionChanged, this, &NameGeneratorWindow::updateButtons);
    connect(m_same, &QCheckBox::toggled, this, [this](bool on) { m_sur->setEnabled(!on); });
    auto setAll = [](QListWidget *w, bool on) {
        for (int i = 0; i < w->count(); ++i)
            w->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    };
    connect(foreAll, &QPushButton::clicked, this, [=] { setAll(m_fore, true); });
    connect(foreNone, &QPushButton::clicked, this, [=] { setAll(m_fore, false); });
    connect(surAll, &QPushButton::clicked, this, [=] { setAll(m_sur, true); });
    connect(surNone, &QPushButton::clicked, this, [=] { setAll(m_sur, false); });
    connect(m_fore, &QListWidget::currentItemChanged, this, &NameGeneratorWindow::updateButtons);
    connect(addList, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Add Own List"), QDir::homePath(),
                                                          tr("Text files (*.txt *.csv);;All files (*)"));
        if (file.isEmpty())
            return;
        QDialog d(this);
        d.setWindowTitle(tr("Add Own List"));
        auto *f = new QFormLayout(&d);
        auto *name = new QLineEdit(QFileInfo(file).completeBaseName(), &d);
        f->addRow(tr("Name of the list:"), name);
        auto *kind = new QComboBox(&d);
        kind->addItem(tr("Forenames (female and male)"), QStringLiteral("both"));
        kind->addItem(tr("Female forenames"), QStringLiteral("female"));
        kind->addItem(tr("Male forenames"), QStringLiteral("male"));
        kind->addItem(tr("Surnames"), QStringLiteral("surnames"));
        f->addRow(tr("The file holds:"), kind);
        auto *hint = new QLabel(tr("One name per line; a second column after a comma or tab is ignored. "
                                   "Lines starting with # are comments."), &d);
        hint->setWordWrap(true);
        f->addRow(hint);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        f->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() == QDialog::Accepted)
            addListFromFile(file, name->text(), kind->currentData().toString());
    });
    connect(m_removeList, &QPushButton::clicked, this, &NameGeneratorWindow::removeOwnList);

    reloadLists();
    restoreOptions();
    updateButtons();
}

void NameGeneratorWindow::reloadLists()
{
    const QString ownDir = m_host->dataDir(QString::fromLatin1(kTool)) + QStringLiteral("/lists");
    m_lists = namegen::loadLists(m_host->contentDirs(QStringLiteral("names")), ownDir);
    const QStringList foreOn = ticked(m_fore), surOn = ticked(m_sur);
    fillListWidgets();
    if (!foreOn.isEmpty() || !surOn.isEmpty())
        chooseLists(foreOn, surOn);
    if (m_lists.isEmpty())
        m_status->setText(tr("No name lists found. They come with the add-on “Name Generator” (Help > Add-on Manager)."));
}

void NameGeneratorWindow::fillListWidgets()
{
    for (QListWidget *w : {m_fore, m_sur}) {
        w->clear();
        for (const namegen::NameList &l : m_lists) {
            if (w == m_fore && l.male.isEmpty() && l.female.isEmpty())
                continue;
            if (w == m_sur && l.surnames.isEmpty())
                continue;
            QString label = l.name;
            // "by frequency" where common <-> rare works for this kind of name
            const bool freq = w == m_fore ? (!l.male.weights.isEmpty() || !l.female.weights.isEmpty()) : !l.surnames.weights.isEmpty();
            if (freq)
                label += tr(" (by frequency)");
            if (l.own)
                label += tr(" (own list)");
            auto *it = new QListWidgetItem(label, w);
            it->setData(Qt::UserRole, l.id);
            it->setData(Qt::UserRole + 1, l.own);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(Qt::Unchecked);
            QString tip = l.source;
            if (l.unisex)
                tip += tr("\nThe forenames of this list are not divided into female and male.");
            it->setToolTip(tip);
        }
    }
}

QStringList NameGeneratorWindow::ticked(QListWidget *w) const
{
    QStringList ids;
    for (int i = 0; i < w->count(); ++i)
        if (w->item(i)->checkState() == Qt::Checked)
            ids << w->item(i)->data(Qt::UserRole).toString();
    return ids;
}

void NameGeneratorWindow::chooseLists(const QStringList &forenames, const QStringList &surnames)
{
    for (int i = 0; i < m_fore->count(); ++i) {
        QListWidgetItem *it = m_fore->item(i);
        it->setCheckState(forenames.isEmpty() || forenames.contains(it->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
    }
    for (int i = 0; i < m_sur->count(); ++i) {
        QListWidgetItem *it = m_sur->item(i);
        it->setCheckState(surnames.isEmpty() || surnames.contains(it->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
    }
}

namegen::Options NameGeneratorWindow::options() const
{
    namegen::Options o;
    o.gender = namegen::Gender(m_gender->currentData().toInt());
    o.show = namegen::Show(m_show->currentData().toInt());
    o.forenameLists = ticked(m_fore);
    o.surnameLists = ticked(m_sur);
    o.sameOrigin = m_same->isChecked();
    o.count = m_count->value();
    o.forenameObscurity = m_foreObsc->value();
    o.surnameObscurity = m_surObsc->value();
    o.alliteration = m_allit->value();
    o.doubleBarrelled = m_double->value();
    o.middleName = m_middle->isChecked();
    o.forenameStarts = m_foreStarts->text().trimmed();
    o.surnameStarts = m_surStarts->text().trimmed();
    return o;
}

void NameGeneratorWindow::generate()
{
    QString problem;
    const namegen::Options o = options();
    m_names = namegen::generate(m_lists, o, QRandomGenerator::global(), &problem);
    m_results->clear();
    for (const namegen::Name &n : m_names) {
        auto *it = new QListWidgetItem(n.text(o.show), m_results);
        it->setToolTip(n.listName + (n.female ? tr(" · female") : tr(" · male")));
    }
    m_status->setText(problem);
    saveOptions();
    updateButtons();
}

QStringList NameGeneratorWindow::results() const
{
    QStringList out;
    for (int i = 0; i < m_results->count(); ++i)
        out << m_results->item(i)->text();
    return out;
}

void NameGeneratorWindow::copyNames()
{
    QStringList names;
    for (QListWidgetItem *it : m_results->selectedItems())
        names << it->text();
    if (names.isEmpty())
        names = results();
    QApplication::clipboard()->setText(names.join(QLatin1Char('\n')));
    m_status->setText(tr("%n name(s) copied.", nullptr, int(names.size())));
}

void NameGeneratorWindow::insertNames()
{
    QStringList names;
    for (QListWidgetItem *it : m_results->selectedItems())
        names << it->text();
    if (names.isEmpty())
        return;
    if (!m_host->insertText(names.join(QStringLiteral(", "))))
        m_status->setText(tr("There is no text editor to insert into. Click into a text first."));
    else
        m_status->setText(tr("Inserted in “%1”.").arg(m_host->projectTitle()));
}

void NameGeneratorWindow::updateButtons()
{
    m_copy->setEnabled(m_results->count() > 0);
    m_insert->setEnabled(!m_results->selectedItems().isEmpty() && m_host->hasProject());
    QListWidgetItem *cur = m_fore->currentItem();
    m_removeList->setEnabled(cur && cur->data(Qt::UserRole + 1).toBool());
}

bool NameGeneratorWindow::addListFromFile(const QString &file, const QString &name, const QString &kind)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_status->setText(tr("The file could not be read: %1").arg(f.errorString()));
        return false;
    }
    QTextStream ts(&f);
    QStringList names;
    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        line = line.section(QRegularExpression(QStringLiteral("[,;\\t]")), 0, 0).trimmed();
        if (line.startsWith(QLatin1Char('"')) && line.endsWith(QLatin1Char('"')) && line.size() > 1)
            line = line.mid(1, line.size() - 2);
        if (!line.isEmpty() && !names.contains(line))
            names << line;
    }
    if (names.isEmpty()) {
        m_status->setText(tr("The file has no names."));
        return false;
    }
    namegen::NameList l;
    l.name = name.trimmed().isEmpty() ? QFileInfo(file).completeBaseName() : name.trimmed();
    QString id = QStringLiteral("own-") + l.name.toLower();
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    l.id = id;
    l.source = tr("Own list from %1").arg(QFileInfo(file).fileName());
    const QString ownDir = m_host->dataDir(QString::fromLatin1(kTool)) + QStringLiteral("/lists");
    QDir().mkpath(ownDir);
    const QString target = ownDir + QLatin1Char('/') + id + QStringLiteral(".json");
    // an own list with the same name gets the new names added
    namegen::NameList old;
    if (QFile::exists(target) && namegen::readList(target, &old, nullptr))
        l = old;
    auto merge = [&names](namegen::NameSet &set) {
        for (const QString &n : names)
            if (!set.names.contains(n))
                set.names << n;
        set.weights.clear();
    };
    if (kind == QLatin1String("female") || kind == QLatin1String("both"))
        merge(l.female);
    if (kind == QLatin1String("male") || kind == QLatin1String("both"))
        merge(l.male);
    if (kind == QLatin1String("surnames"))
        merge(l.surnames);
    if (kind == QLatin1String("both"))
        l.unisex = true;
    QString error;
    if (!namegen::writeList(target, l, &error)) {
        m_status->setText(error);
        return false;
    }
    QStringList foreOn = ticked(m_fore), surOn = ticked(m_sur);
    foreOn << l.id;
    surOn << l.id;
    reloadLists();
    chooseLists(foreOn, surOn);
    m_status->setText(tr("List “%1” with %n name(s).", nullptr, int(names.size())).arg(l.name));
    return true;
}

void NameGeneratorWindow::removeOwnList()
{
    QListWidgetItem *cur = m_fore->currentItem();
    if (!cur || !cur->data(Qt::UserRole + 1).toBool())
        return;
    const QString id = cur->data(Qt::UserRole).toString();
    for (const namegen::NameList &l : m_lists)
        if (l.id == id && l.own)
            QFile::remove(l.file);
    reloadLists();
    updateButtons();
}

void NameGeneratorWindow::saveOptions()
{
    const QString t = QString::fromLatin1(kTool);
    const namegen::Options o = options();
    m_host->setSetting(t, QStringLiteral("gender"), m_gender->currentIndex());
    m_host->setSetting(t, QStringLiteral("show"), m_show->currentIndex());
    m_host->setSetting(t, QStringLiteral("forenameLists"), o.forenameLists);
    m_host->setSetting(t, QStringLiteral("surnameLists"), o.surnameLists);
    m_host->setSetting(t, QStringLiteral("sameOrigin"), o.sameOrigin);
    m_host->setSetting(t, QStringLiteral("count"), o.count);
    m_host->setSetting(t, QStringLiteral("forenameObscurity"), o.forenameObscurity);
    m_host->setSetting(t, QStringLiteral("surnameObscurity"), o.surnameObscurity);
    m_host->setSetting(t, QStringLiteral("alliteration"), o.alliteration);
    m_host->setSetting(t, QStringLiteral("doubleBarrelled"), o.doubleBarrelled);
    m_host->setSetting(t, QStringLiteral("middleName"), o.middleName);
}

void NameGeneratorWindow::restoreOptions()
{
    const QString t = QString::fromLatin1(kTool);
    m_gender->setCurrentIndex(m_host->setting(t, QStringLiteral("gender"), 0).toInt());
    m_show->setCurrentIndex(m_host->setting(t, QStringLiteral("show"), 0).toInt());
    m_same->setChecked(m_host->setting(t, QStringLiteral("sameOrigin"), true).toBool());
    m_sur->setEnabled(!m_same->isChecked());
    m_count->setValue(m_host->setting(t, QStringLiteral("count"), 25).toInt());
    m_foreObsc->setValue(m_host->setting(t, QStringLiteral("forenameObscurity"), 30).toInt());
    m_surObsc->setValue(m_host->setting(t, QStringLiteral("surnameObscurity"), 30).toInt());
    m_allit->setValue(m_host->setting(t, QStringLiteral("alliteration"), 0).toInt());
    m_double->setValue(m_host->setting(t, QStringLiteral("doubleBarrelled"), 0).toInt());
    m_middle->setChecked(m_host->setting(t, QStringLiteral("middleName"), false).toBool());
    QStringList fore = m_host->setting(t, QStringLiteral("forenameLists")).toStringList();
    QStringList sur = m_host->setting(t, QStringLiteral("surnameLists")).toStringList();
    if (fore.isEmpty())
        fore = {QStringLiteral("english"), QStringLiteral("american")}; // first time
    if (sur.isEmpty())
        sur = fore;
    chooseLists(fore, sur);
}

void NameGeneratorWindow::closeEvent(QCloseEvent *e)
{
    saveOptions();
    QWidget::closeEvent(e);
}
