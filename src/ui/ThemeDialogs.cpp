#include "ui/ThemeDialogs.h"

#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/ProjectLook.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace qb {

using theme::Role;

// ------------------------------------------------------------------ preview

ThemePreview::ThemePreview(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ThemePreview::setColors(const QMap<Role, QColor> &colors)
{
    m_colors = colors;
    update();
}

void ThemePreview::setFonts(const QMap<theme::FontRole, theme::FontSpec> &fonts)
{
    m_fonts = fonts;
    update();
}

void ThemePreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (m_colors.isEmpty())
        return;
    auto c = [this](Role r) { return m_colors.value(r); };
    auto fnt = [this](theme::FontRole r, double factor, bool bold) {
        QFont f = font();
        f.setBold(bold);
        f = m_fonts.value(theme::FontRole::Interface).applied(f);
        f = m_fonts.value(r).applied(f);
        f.setPointSizeF(qBound(5.0, f.pointSizeF() * factor, 9.0));
        return f;
    };
    const QRect r = rect().adjusted(0, 0, -1, -1);
    p.fillRect(r, c(Role::Window));
    const int barH = 16, binderW = r.width() * 22 / 100, inspW = r.width() * 20 / 100;
    const int corkH = (r.height() - barH) * 38 / 100;

    // toolbar
    p.setPen(c(Role::WindowText));
    p.setFont(fnt(theme::FontRole::Interface, 0.8, false));
    p.drawText(QRect(6, 0, r.width() - 12, barH), Qt::AlignVCenter | Qt::AlignLeft, tr("File  Edit  View"));
    p.fillRect(QRect(r.width() - 60, 4, 54, barH - 8), c(Role::Button));

    // binder
    const QRect binder(0, barH, binderW, r.height() - barH);
    p.fillRect(binder, c(Role::BinderBackground));
    p.setFont(fnt(theme::FontRole::Binder, 0.8, false));
    const int rowH = qMax(11, QFontMetrics(p.font()).height());
    const QStringList rows = {tr("Draft"), tr("  Chapter 1"), tr("  Chapter 2"), tr("Research")};
    for (int i = 0; i < rows.size(); ++i) {
        const QRect row(binder.left(), binder.top() + 3 + i * rowH, binder.width(), rowH);
        if (i == 1) {
            p.fillRect(row, c(Role::Highlight));
            p.setPen(c(Role::HighlightedText));
        } else {
            p.setPen(c(Role::BinderText));
        }
        p.drawText(row.adjusted(5, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, rows[i]);
    }

    // editor: header bar, margin, paper
    const QRect editor(binderW, barH, r.width() - binderW - inspW, r.height() - barH - corkH);
    const QRect header(editor.left(), editor.top(), editor.width(), 14);
    p.fillRect(header, c(Role::HeaderBarActive));
    p.setPen(c(Role::HeaderBarText));
    p.setFont(fnt(theme::FontRole::HeaderTitle, 0.8, true));
    p.drawText(header.adjusted(5, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, tr("Chapter 1"));
    const QRect body = editor.adjusted(0, 14, 0, 0);
    p.fillRect(body, c(Role::EditorMargin));
    const QRect paper = body.adjusted(body.width() / 9, 0, -body.width() / 9, 0);
    p.fillRect(paper, c(Role::EditorPaper));
    QFont serif(QStringLiteral("Serif"));
    serif.setPointSizeF(7.5);
    p.setFont(serif);
    p.setPen(c(Role::EditorText));
    const QRect textRect = paper.adjusted(8, 4, -8, -2);
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               tr("The morning train left without her, and for once she did not mind."));
    p.setPen(c(Role::EditorLink));
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignBottom, tr("a link"));

    // corkboard with two cards
    const QRect cork(binderW, editor.bottom() + 1, editor.width(), corkH);
    p.fillRect(cork, c(Role::CorkboardBackground));
    const int cardW = qMin(84, (cork.width() - 24) / 2), cardH = cork.height() - 14;
    for (int i = 0; i < 2; ++i) {
        const QRect card(cork.left() + 8 + i * (cardW + 8), cork.top() + 7, cardW, cardH);
        p.fillRect(card.translated(1, 2), QColor(0, 0, 0, 50));
        p.fillRect(card, c(Role::CardPaper));
        p.setPen(c(Role::CardTitle));
        p.setFont(fnt(theme::FontRole::CardTitle, 0.75, true));
        p.drawText(card.adjusted(4, 1, -2, 0), Qt::AlignLeft | Qt::AlignTop, i == 0 ? tr("Scene") : tr("Idea"));
        const int ly = card.top() + QFontMetrics(p.font()).height() + 2;
        p.setPen(c(Role::CardTitleLine));
        p.drawLine(card.left() + 3, ly, card.right() - 3, ly);
        QColor ruled = c(Role::CardLines);
        ruled.setAlpha(110);
        p.setPen(ruled);
        for (int y = ly + 8; y < card.bottom() - 2; y += 8)
            p.drawLine(card.left() + 3, y, card.right() - 3, y);
        p.setPen(c(Role::CardText));
        p.setFont(fnt(theme::FontRole::CardText, 0.7, false));
        p.drawText(card.adjusted(4, ly - card.top() + 1, -2, 0), Qt::AlignLeft | Qt::AlignTop, tr("Synopsis"));
    }

    // inspector
    const QRect insp(r.width() - inspW, barH, inspW + 1, r.height() - barH);
    p.fillRect(insp, c(Role::InspectorBackground));
    p.setPen(c(Role::InspectorText));
    p.setFont(fnt(theme::FontRole::Interface, 0.75, true));
    p.drawText(insp.adjusted(4, 2, 0, 0), Qt::AlignLeft | Qt::AlignTop, tr("Synopsis"));
    const QRect syn(insp.left() + 4, insp.top() + 16, insp.width() - 9, insp.height() * 30 / 100);
    p.fillRect(syn, c(Role::SynopsisPaper));
    p.setPen(c(Role::SynopsisText));
    p.setFont(fnt(theme::FontRole::Synopsis, 0.7, false));
    p.drawText(syn.adjusted(3, 1, -1, 0), Qt::AlignLeft | Qt::AlignTop, tr("Text"));
    const QRect notes(insp.left() + 4, syn.bottom() + 6, insp.width() - 9, insp.bottom() - syn.bottom() - 10);
    p.fillRect(notes, c(Role::NotesPaper));
    p.setPen(c(Role::NotesText));
    p.drawText(notes.adjusted(3, 1, -1, 0), Qt::AlignLeft | Qt::AlignTop, tr("Notes"));

    p.setPen(c(Role::Mid));
    p.drawLine(binder.topRight(), binder.bottomRight());
    p.drawLine(insp.topLeft(), insp.bottomLeft());
    p.drawRect(r);
}

// ------------------------------------------------------------------ questions

namespace themeui {

// A theme chosen in Window > Themes (or the Themes window) is for the program, new projects and
// the open project alike (0.40.2: every project keeps its own look; other projects keep theirs).
bool choose(QWidget *parent, const QString &id, bool ask)
{
    AppSettings &s = AppSettings::instance();
    look::Values pl = look::active();
    const bool hasProject = look::project() != nullptr;
    int own = int(s.ownColors.size() + s.ownFonts.size());
    if (hasProject) {
        // what the writer sees: the general colors and the interface font, and the project's areas
        own = int(pl.colors.size() + pl.fonts.size());
        for (auto it = s.ownColors.constBegin(); it != s.ownColors.constEnd(); ++it)
            own += theme::isAreaColor(it.key()) ? 0 : 1;
        for (auto it = s.ownFonts.constBegin(); it != s.ownFonts.constEnd(); ++it)
            own += theme::isAreaFont(it.key()) ? 0 : 1;
    }
    bool discard = false;
    if (own > 0 && ask && (id != s.theme || id != pl.theme)) {
        QMessageBox box(QMessageBox::Question, QObject::tr("Change Theme"),
                        QObject::tr("You have changed %1 of your own on top of the current theme.")
                            .arg(text::plural(own, QObject::tr("color or font"), QObject::tr("colors and fonts"))),
                        QMessageBox::NoButton, parent);
        box.setInformativeText(QObject::tr("Keep them with the new theme, or use the theme as it is? "
                                           "(Save Current Look as Theme keeps the present look for later.)"));
        QPushButton *asIs = box.addButton(QObject::tr("Use the Theme as It Is"), QMessageBox::AcceptRole);
        QPushButton *keep = box.addButton(QObject::tr("Keep My Changes"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(keep);
        box.exec();
        if (box.clickedButton() == asIs)
            discard = true;
        else if (box.clickedButton() != keep)
            return false;
    }
    if (discard) {
        s.ownColors.clear();
        s.ownFonts.clear();
        pl.colors.clear();
        pl.fonts.clear();
    }
    s.theme = id;
    s.notifyChanged();
    if (hasProject) {
        pl.theme = id;
        QString err;
        if (!look::change(pl, &err))
            QMessageBox::warning(parent, QObject::tr("Change Theme"), err);
    }
    return true;
}

QString askName(QWidget *parent, const QString &title, const QString &suggestion, const QString &exceptId)
{
    QString name = suggestion;
    while (true) {
        bool ok = false;
        name = QInputDialog::getText(parent, title, QObject::tr("Name of the theme:"), QLineEdit::Normal, name, &ok).trimmed();
        if (!ok)
            return QString();
        if (name.isEmpty())
            continue;
        if (!theme::nameInUse(name, exceptId))
            return name;
        QMessageBox::information(parent, title, QObject::tr("There is already a theme named “%1”. Choose another name.").arg(name));
    }
}

QString saveLookAs(const theme::Theme &look, const QString &name, QString *error)
{
    theme::Theme t = look;
    t.id.clear();
    t.name = name.trimmed();
    if (theme::nameInUse(t.name)) {
        if (error)
            *error = QObject::tr("There is already a theme named “%1”.").arg(t.name);
        return QString();
    }
    return theme::saveUserTheme(t, error);
}

QString saveCurrentLook(QWidget *parent)
{
    const QString name = askName(parent, QObject::tr("Save Current Look as Theme"),
                                 QObject::tr("%1 (My Version)").arg(theme::currentName()));
    if (name.isEmpty())
        return QString();
    QString error;
    const QString id = saveLookAs(theme::currentLook(), name, &error);
    if (id.isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("Save Current Look as Theme"), error);
        return QString();
    }
    // the own changes are part of the new theme now (also those of the open project's areas)
    AppSettings &s = AppSettings::instance();
    s.ownColors.clear();
    s.ownFonts.clear();
    s.theme = id;
    s.notifyChanged();
    if (look::project()) {
        look::Values pl = look::active();
        pl.colors.clear();
        pl.fonts.clear();
        pl.theme = id;
        look::change(pl);
    }
    return id;
}

QString importFromFile(QWidget *parent)
{
    const QString file = QFileDialog::getOpenFileName(parent, QObject::tr("Import Theme"), QDir::homePath(),
                                                      QObject::tr("Theme files (*.qbtheme *.json);;All files (*)"));
    if (file.isEmpty())
        return QString();
    QString error;
    const QString id = theme::importTheme(file, &error);
    if (id.isEmpty())
        QMessageBox::warning(parent, QObject::tr("Import Theme"), error);
    return id;
}

} // namespace themeui

// ------------------------------------------------------------------ theme list

ThemeManagerDialog::ThemeManagerDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Themes"));
    resize(680, 430);
    auto *lay = new QVBoxLayout(this);
    auto *main = new QHBoxLayout;
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("themeList"));
    m_list->setMinimumWidth(230);
    main->addWidget(m_list, 1);
    auto *right = new QVBoxLayout;
    m_preview = new ThemePreview(this);
    m_preview->setFixedHeight(170);
    right->addWidget(m_preview);
    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    right->addWidget(m_info);
    right->addStretch(1);
    auto button = [&](const QString &text, const char *name) {
        auto *b = new QPushButton(text, this);
        b->setObjectName(QLatin1String(name));
        right->addWidget(b);
        return b;
    };
    m_use = button(tr("Use This Theme"), "themeUse");
    m_duplicate = button(tr("Duplicate…"), "themeDuplicate");
    m_rename = button(tr("Rename…"), "themeRename");
    m_delete = button(tr("Delete"), "themeDelete");
    m_export = button(tr("Export…"), "themeExport");
    QPushButton *import = button(tr("Import…"), "themeImport");
    main->addLayout(right, 1);
    lay->addLayout(main, 1);
    auto *hint = new QLabel(tr("To change colors and fonts, use File ▸ Options ▸ Appearance and then "
                               "Save as Theme. Built-in themes and themes of add-ons cannot be changed; "
                               "duplicate one to get a theme of your own."),
                            this);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    lay->addWidget(hint);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_list, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { m_use->click(); });
    connect(m_use, &QPushButton::clicked, this, [this] {
        const QString id = selectedId();
        if (!id.isEmpty() && themeui::choose(this, id))
            reload(id);
    });
    connect(m_duplicate, &QPushButton::clicked, this, [this] {
        const QString id = selectedId();
        theme::Theme t;
        if (!theme::load(id, &t))
            return;
        const QString name = themeui::askName(this, tr("Duplicate Theme"), tr("%1 Copy").arg(t.name));
        if (name.isEmpty())
            return;
        QString error;
        const QString made = duplicate(id, name, &error);
        if (made.isEmpty())
            QMessageBox::warning(this, tr("Duplicate Theme"), error);
    });
    connect(m_rename, &QPushButton::clicked, this, [this] {
        const QString id = selectedId();
        theme::Theme t;
        if (!theme::load(id, &t))
            return;
        const QString name = themeui::askName(this, tr("Rename Theme"), t.name, id);
        if (name.isEmpty() || name == t.name)
            return;
        QString error, newId;
        if (!theme::renameUserTheme(id, name, &newId, &error))
            QMessageBox::warning(this, tr("Rename Theme"), error);
        reload(newId.isEmpty() ? id : newId);
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        const QString id = selectedId();
        theme::Theme t;
        if (!theme::load(id, &t))
            return;
        if (QMessageBox::question(this, tr("Delete Theme"), tr("Delete the theme “%1”? This cannot be undone.").arg(t.name),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
            return;
        QString error;
        if (!theme::removeUserTheme(id, &error)) {
            QMessageBox::warning(this, tr("Delete Theme"), error);
            return;
        }
        if (AppSettings::instance().theme == id) { // the theme in use is gone
            AppSettings::instance().theme = QStringLiteral("system");
            AppSettings::instance().notifyChanged();
        }
        reload();
    });
    connect(m_export, &QPushButton::clicked, this, [this] {
        const QString id = selectedId();
        theme::Theme t;
        if (!theme::load(id, &t))
            return;
        QString file = QFileDialog::getSaveFileName(this, tr("Export Theme"),
                                                    QDir::homePath() + QLatin1Char('/') + t.name + QLatin1String(".qbtheme"),
                                                    tr("Theme files (*.qbtheme)"));
        if (file.isEmpty())
            return;
        if (QFileInfo(file).suffix().isEmpty())
            file += QLatin1String(".qbtheme");
        QString error;
        if (!theme::exportTheme(id, file, &error))
            QMessageBox::warning(this, tr("Export Theme"), error);
    });
    connect(import, &QPushButton::clicked, this, [this] {
        const QString id = themeui::importFromFile(this);
        if (!id.isEmpty())
            reload(id);
    });
    reload(look::active().theme);
}

QString ThemeManagerDialog::selectedId() const
{
    return m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
}

void ThemeManagerDialog::select(const QString &id)
{
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toString() == id)
            m_list->setCurrentRow(i);
}

QString ThemeManagerDialog::duplicate(const QString &id, const QString &name, QString *error)
{
    theme::Theme t;
    if (!theme::load(id, &t)) {
        if (error)
            *error = tr("The theme could not be read.");
        return QString();
    }
    const QString made = themeui::saveLookAs(t, name, error);
    if (!made.isEmpty())
        reload(made);
    return made;
}

void ThemeManagerDialog::reload(const QString &selectId)
{
    const QString want = selectId.isEmpty() ? selectedId() : selectId;
    m_list->clear();
    const QString inUse = look::active().theme;
    for (const theme::Entry &e : theme::available()) {
        auto *it = new QListWidgetItem(e.name, m_list);
        it->setData(Qt::UserRole, e.id);
        it->setData(Qt::UserRole + 1, int(e.kind));
        if (e.id == inUse) {
            QFont f = it->font();
            f.setBold(true);
            it->setFont(f);
            it->setText(tr("%1  (in use)").arg(e.name));
        }
    }
    select(want);
    if (m_list->currentRow() < 0)
        select(inUse);
    if (m_list->currentRow() < 0 && m_list->count())
        m_list->setCurrentRow(0);
    updateButtons();
}

void ThemeManagerDialog::updateButtons()
{
    QListWidgetItem *it = m_list->currentItem();
    const bool any = it != nullptr;
    const auto kind = any ? theme::Kind(it->data(Qt::UserRole + 1).toInt()) : theme::Kind::BuiltIn;
    const bool own = any && kind == theme::Kind::User;
    m_use->setEnabled(any && selectedId() != look::active().theme);
    m_duplicate->setEnabled(any);
    m_export->setEnabled(any);
    m_rename->setEnabled(own);
    m_delete->setEnabled(own);
    theme::Theme t;
    if (any && theme::load(selectedId(), &t)) {
        m_preview->setColors(theme::resolvedColors(t, {}));
        m_preview->setFonts(t.fonts);
        const QString kindText = kind == theme::Kind::BuiltIn ? tr("Built-in theme")
                                 : kind == theme::Kind::Addon ? tr("Theme of an add-on")
                                                              : tr("Your own theme");
        QString what = t.followDesktop && t.colors.isEmpty()
                           ? tr("uses the colors of the desktop (KDE color scheme)")
                           : tr("sets %1").arg(text::plural(int(t.colors.size()), tr("color"), tr("colors")));
        if (!t.fonts.isEmpty())
            what += tr(" and %1").arg(text::plural(int(t.fonts.size()), tr("font"), tr("fonts")));
        m_info->setText(QStringLiteral("%1: %2.").arg(kindText, what));
    } else {
        m_preview->setColors({});
        m_info->clear();
    }
}

} // namespace qb
