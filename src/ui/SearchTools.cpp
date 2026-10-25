#include "ui/SearchTools.h"
#include "core/BinderItem.h"
#include "core/ItemIcons.h"
#include "text/Styles.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace qb {

// ------------------------------------------------------------ project replace

namespace {
ReplaceOptions s_lastOptions;
bool s_haveLast = false;
bool s_lastSelectionOnly = false;
} // namespace

ProjectReplaceDialog::ProjectReplaceDialog(Project *project, QWidget *parent) : QDialog(parent), m_project(project)
{
    setObjectName(QStringLiteral("projectReplaceDialog"));
    setWindowTitle(tr("Project Replace"));
    auto *vl = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_find = new QLineEdit(this);
    m_with = new QLineEdit(this);
    m_find->setObjectName(QStringLiteral("replaceFind"));
    m_with->setObjectName(QStringLiteral("replaceWith"));
    m_find->setMinimumWidth(340);
    form->addRow(tr("&Replace:"), m_find);
    form->addRow(tr("&With:"), m_with);
    vl->addLayout(form);

    auto *optRow = new QHBoxLayout;
    m_case = new QCheckBox(tr("Match &case"), this);
    m_whole = new QCheckBox(tr("W&hole words only"), this);
    m_regex = new QCheckBox(tr("Re&gular expression"), this);
    m_regex->setToolTip(tr("\"Replace\" is a regular expression; \"With\" may use \\1 to \\9 for its groups."));
    optRow->addWidget(m_case);
    optRow->addWidget(m_whole);
    optRow->addWidget(m_regex);
    optRow->addStretch(1);
    vl->addLayout(optRow);

    auto *where = new QGroupBox(tr("Replace in"), this);
    auto *wg = new QGridLayout(where);
    m_titles = new QCheckBox(tr("&Titles"), where);
    m_text = new QCheckBox(tr("Te&xt"), where);
    m_notes = new QCheckBox(tr("&Notes"), where);
    m_synopses = new QCheckBox(tr("&Synopses"), where);
    m_custom = new QCheckBox(tr("Custom &metadata"), where);
    m_comments = new QCheckBox(tr("Comments and &footnotes"), where);
    m_comments->setToolTip(tr("The texts of the comments and footnotes shown in the inspector"));
    const QList<QCheckBox *> boxes = {m_titles, m_text, m_notes, m_synopses, m_custom, m_comments};
    for (int i = 0; i < boxes.size(); ++i) {
        boxes[i]->setChecked(true);
        wg->addWidget(boxes[i], i / 3, i % 3);
    }
    vl->addWidget(where);

    auto *scope = new QGroupBox(tr("Scope"), this);
    auto *sl = new QVBoxLayout(scope);
    m_all = new QRadioButton(tr("&Entire project"), scope);
    m_selected = new QRadioButton(tr("Selected &documents only"), scope);
    m_all->setChecked(true);
    m_selected->setEnabled(false);
    m_trash = new QCheckBox(tr("Also in the Trash"), scope);
    sl->addWidget(m_all);
    sl->addWidget(m_selected);
    sl->addWidget(m_trash);
    vl->addWidget(scope);

    m_snapshots = new QCheckBox(tr("Take a snapshot of every text before it is changed"), this);
    m_snapshots->setChecked(true);
    vl->addWidget(m_snapshots);

    m_result = new QLabel(this);
    m_result->setObjectName(QStringLiteral("replaceResult"));
    m_result->setWordWrap(true);
    m_result->setMinimumHeight(fontMetrics().height() * 2);
    m_result->setText(tr("Project Replace changes all documents at once and cannot be undone as a whole."));
    vl->addWidget(m_result);

    auto *buttons = new QDialogButtonBox(this);
    m_countButton = buttons->addButton(tr("C&ount"), QDialogButtonBox::ActionRole);
    m_replaceButton = buttons->addButton(tr("Replace &All"), QDialogButtonBox::ActionRole);
    m_countButton->setObjectName(QStringLiteral("replaceCount"));
    m_replaceButton->setObjectName(QStringLiteral("replaceAllButton"));
    buttons->addButton(QDialogButtonBox::Close);
    vl->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_countButton, &QPushButton::clicked, this, [this] { count(); });
    connect(m_replaceButton, &QPushButton::clicked, this, [this] { replaceAll(true); });
    connect(m_find, &QLineEdit::textChanged, this, &ProjectReplaceDialog::updateButtons);
    for (QCheckBox *b : boxes)
        connect(b, &QCheckBox::toggled, this, &ProjectReplaceDialog::updateButtons);
    if (s_haveLast)
        setOptions(s_lastOptions, false);
    updateButtons();
}

void ProjectReplaceDialog::setSelection(const QList<BinderItem *> &items)
{
    m_selection.clear();
    for (BinderItem *it : items)
        if (!it->isRootFolder() || it->type != ItemType::Trash)
            m_selection << it->id;
    m_selected->setEnabled(!m_selection.isEmpty());
    m_selected->setText(m_selection.isEmpty() ? tr("Selected &documents only")
                                              : tr("Selected &documents only (%1, with what is inside them)").arg(m_selection.size()));
    if (m_selection.isEmpty())
        m_all->setChecked(true);
    else if (s_haveLast && s_lastSelectionOnly)
        m_selected->setChecked(true);
}

ReplaceOptions ProjectReplaceDialog::options() const
{
    ReplaceOptions o;
    o.find = m_find->text();
    o.with = m_with->text();
    o.caseSensitive = m_case->isChecked();
    o.wholeWord = m_whole->isChecked();
    o.regex = m_regex->isChecked();
    o.inTitles = m_titles->isChecked();
    o.inText = m_text->isChecked();
    o.inNotes = m_notes->isChecked();
    o.inSynopses = m_synopses->isChecked();
    o.inCustom = m_custom->isChecked();
    o.inComments = m_comments->isChecked();
    if (m_selected->isChecked())
        o.scopeIds = m_selection;
    o.includeTrash = m_trash->isChecked();
    o.snapshotFirst = m_snapshots->isChecked();
    o.snapshotTitle = tr("Before replacing \"%1\"").arg(o.find);
    return o;
}

void ProjectReplaceDialog::setOptions(const ReplaceOptions &o, bool selectionOnly)
{
    m_find->setText(o.find);
    m_with->setText(o.with);
    m_case->setChecked(o.caseSensitive);
    m_whole->setChecked(o.wholeWord);
    m_regex->setChecked(o.regex);
    m_titles->setChecked(o.inTitles);
    m_text->setChecked(o.inText);
    m_notes->setChecked(o.inNotes);
    m_synopses->setChecked(o.inSynopses);
    m_custom->setChecked(o.inCustom);
    m_comments->setChecked(o.inComments);
    m_trash->setChecked(o.includeTrash);
    m_snapshots->setChecked(o.snapshotFirst);
    if (selectionOnly && m_selected->isEnabled())
        m_selected->setChecked(true);
    else
        m_all->setChecked(true);
    updateButtons();
}

void ProjectReplaceDialog::updateButtons()
{
    const bool any = m_titles->isChecked() || m_text->isChecked() || m_notes->isChecked() || m_synopses->isChecked()
                     || m_custom->isChecked() || m_comments->isChecked();
    const bool ok = m_project && !m_find->text().isEmpty() && any;
    m_countButton->setEnabled(ok);
    m_replaceButton->setEnabled(ok);
}

QString ProjectReplaceDialog::describe(const ReplaceResult &r, bool done)
{
    if (!r.error.isEmpty() && r.total() == 0)
        return r.error;
    if (r.total() == 0)
        return tr("Nothing found.");
    QStringList parts;
    auto add = [&parts](int n, const QString &where) {
        if (n > 0)
            parts << tr("%1 in %2").arg(n).arg(where);
    };
    add(r.texts, tr("text"));
    add(r.titles, tr("titles"));
    add(r.synopses, tr("synopses"));
    add(r.notes, tr("notes"));
    add(r.comments, tr("comments and footnotes"));
    add(r.custom, tr("custom metadata"));
    QString s = (done ? tr("%1 replaced in %2: %3.") : tr("%1 found in %2: %3."))
                    .arg(text::plural(r.total(), tr("occurrence"), tr("occurrences")),
                         text::plural(r.documents, tr("document"), tr("documents")), parts.join(QLatin1String(", ")));
    if (done && r.snapshots > 0)
        s += QLatin1Char(' ') + tr("Snapshots taken: %1.").arg(r.snapshots);
    if (!r.error.isEmpty())
        s += QLatin1Char('\n') + r.error;
    return s;
}

QString ProjectReplaceDialog::resultText() const { return m_result->text(); }

ReplaceResult ProjectReplaceDialog::count()
{
    ReplaceResult r;
    if (!m_project)
        return r;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    r = replace::run(m_project, options(), true);
    QApplication::restoreOverrideCursor();
    m_result->setText(describe(r, false));
    return r;
}

ReplaceResult ProjectReplaceDialog::replaceAll(bool ask)
{
    ReplaceResult r;
    if (!m_project)
        return r;
    const ReplaceOptions o = options();
    if (ask) {
        const ReplaceResult found = count();
        if (found.total() == 0)
            return found;
        const QString with = o.with.isEmpty() ? tr("nothing (the text is removed)") : QStringLiteral("\"%1\"").arg(o.with);
        if (QMessageBox::question(this, tr("Project Replace"),
                                  tr("Replace \"%1\" with %2?\n\n%3\n\nThis cannot be undone as a whole.%4")
                                      .arg(o.find, with, describe(found, false),
                                           o.snapshotFirst ? tr(" Each changed text gets a snapshot to go back to.")
                                                           : tr(" A backup (File > Back Up) beforehand is a good idea.")),
                                  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
            != QMessageBox::Yes)
            return r;
    }
    s_lastOptions = o;
    s_lastOptions.scopeIds.clear();
    s_lastSelectionOnly = m_selected->isChecked();
    s_haveLast = true;
    QProgressDialog *progress = nullptr;
    if (ask) {
        progress = new QProgressDialog(tr("Replacing…"), tr("Stop"), 0, 100, this);
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(600);
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    r = replace::run(m_project, o, false, [progress](int done, int total) {
        if (!progress)
            return true;
        progress->setMaximum(qMax(1, total));
        progress->setValue(done);
        return !progress->wasCanceled();
    });
    QApplication::restoreOverrideCursor();
    delete progress;
    m_result->setText(describe(r, true));
    emit replaced(r);
    return r;
}

// --------------------------------------------------------- find by formatting

static QIcon colorIcon(const QColor &c)
{
    QPixmap pm(28, 14);
    pm.fill(c);
    return QIcon(pm);
}

QString FormatFindWindow::kindName(fmtfind::Kind k)
{
    using fmtfind::Kind;
    switch (k) {
    case Kind::Highlight: return tr("Highlighted Text");
    case Kind::CommentOrFootnote: return tr("Comments and Footnotes");
    case Kind::InlineAnnotation: return tr("Inline Annotations");
    case Kind::InlineFootnote: return tr("Inline Footnotes");
    case Kind::Revision: return tr("Revision Color");
    case Kind::ColoredText: return tr("Colored Text");
    case Kind::Link: return tr("Links");
    case Kind::CharFormat: return tr("Character Format");
    case Kind::Style: return tr("Style");
    case Kind::Image: return tr("Images");
    }
    return QString();
}

FormatFindWindow::FormatFindWindow(Project *project, QWidget *parent) : QDialog(parent, Qt::Tool), m_project(project)
{
    setObjectName(QStringLiteral("formatFindWindow"));
    setWindowTitle(tr("Find by Formatting"));
    auto *vl = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_kind = new QComboBox(this);
    m_kind->setObjectName(QStringLiteral("formatKind"));
    for (int i = 0; i < fmtfind::kKindCount; ++i)
        m_kind->addItem(kindName(fmtfind::Kind(i)), i);
    m_containing = new QLineEdit(this);
    m_containing->setObjectName(QStringLiteral("formatContaining"));
    m_containing->setClearButtonEnabled(true);
    m_containing->setMinimumWidth(260);
    m_scope = new QComboBox(this);
    m_scope->setObjectName(QStringLiteral("formatScope"));
    m_scope->addItem(tr("All Documents"));
    m_scope->addItem(tr("The Current Editor Only"));
    form->addRow(tr("&Find:"), m_kind);
    form->addRow(tr("&Containing text:"), m_containing);
    form->addRow(tr("&Search in:"), m_scope);
    vl->addLayout(form);

    m_stack = new QStackedWidget(this);
    // 0: nothing
    m_stack->addWidget(new QWidget(m_stack));
    // 1: color
    auto *colorPage = new QWidget(m_stack);
    auto *cl = new QHBoxLayout(colorPage);
    cl->setContentsMargins(0, 0, 0, 0);
    m_limitColor = new QCheckBox(tr("&Limit search to color:"), colorPage);
    m_colorButton = new QToolButton(colorPage);
    m_colorButton->setIconSize(QSize(28, 14));
    m_colorButton->setIcon(colorIcon(m_color));
    cl->addWidget(m_limitColor);
    cl->addWidget(m_colorButton);
    cl->addStretch(1);
    m_stack->addWidget(colorPage);
    // 2: revision
    auto *revPage = new QWidget(m_stack);
    auto *rl = new QHBoxLayout(revPage);
    rl->setContentsMargins(0, 0, 0, 0);
    m_revision = new QComboBox(revPage);
    m_revision->setObjectName(QStringLiteral("formatRevision"));
    m_revision->addItem(tr("Any Revision"), 0);
    const QStringList levels = {tr("First Revision"), tr("Second Revision"), tr("Third Revision"), tr("Fourth Revision"),
                                tr("Fifth Revision")};
    for (int i = 0; i < levels.size(); ++i)
        m_revision->addItem(colorIcon(text::revisionColor(i + 1)), levels[i], i + 1);
    rl->addWidget(new QLabel(tr("Revision:"), revPage));
    rl->addWidget(m_revision);
    rl->addStretch(1);
    m_stack->addWidget(revPage);
    // 3: character format
    auto *chPage = new QWidget(m_stack);
    auto *chl = new QHBoxLayout(chPage);
    chl->setContentsMargins(0, 0, 0, 0);
    m_bold = new QCheckBox(tr("&Bold"), chPage);
    m_italic = new QCheckBox(tr("&Italic"), chPage);
    m_underline = new QCheckBox(tr("&Underline"), chPage);
    m_strike = new QCheckBox(tr("Stri&kethrough"), chPage);
    m_bold->setChecked(true);
    for (QCheckBox *b : {m_bold, m_italic, m_underline, m_strike})
        chl->addWidget(b);
    chl->addStretch(1);
    m_stack->addWidget(chPage);
    // 4: style
    auto *stPage = new QWidget(m_stack);
    auto *stl = new QHBoxLayout(stPage);
    stl->setContentsMargins(0, 0, 0, 0);
    m_style = new QComboBox(stPage);
    m_style->setObjectName(QStringLiteral("formatStyle"));
    stl->addWidget(new QLabel(tr("Style:"), stPage));
    stl->addWidget(m_style, 1);
    m_stack->addWidget(stPage);
    m_stack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    vl->addWidget(m_stack);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("formatStatus"));
    vl->addWidget(m_status);
    auto *buttons = new QHBoxLayout;
    auto *prev = new QPushButton(tr("&Previous"), this);
    auto *next = new QPushButton(tr("&Next"), this);
    prev->setObjectName(QStringLiteral("formatPrevious"));
    next->setObjectName(QStringLiteral("formatNext"));
    next->setDefault(true);
    buttons->addStretch(1);
    buttons->addWidget(prev);
    buttons->addWidget(next);
    vl->addLayout(buttons);

    connect(m_kind, &QComboBox::currentIndexChanged, this, &FormatFindWindow::kindChanged);
    connect(m_colorButton, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_color, this, tr("Color to Find"));
        if (c.isValid()) {
            setColor(c);
            m_limitColor->setChecked(true);
        }
    });
    connect(prev, &QPushButton::clicked, this, [this] { emit findRequested(true); });
    connect(next, &QPushButton::clicked, this, [this] { emit findRequested(false); });
    connect(m_containing, &QLineEdit::returnPressed, this, [this] { emit findRequested(false); });
    setProject(project);
    kindChanged();
}

void FormatFindWindow::setProject(Project *project)
{
    if (m_project)
        disconnect(m_project, nullptr, this, nullptr);
    m_project = project;
    if (project)
        connect(project, &Project::stylesChanged, this, &FormatFindWindow::refreshStyles);
    refreshStyles();
}

void FormatFindWindow::refreshStyles()
{
    const QString cur = m_style->currentData().toString();
    m_style->clear();
    m_style->addItem(tr("Any Style"), QString());
    if (m_project) {
        for (const Style &s : styles::sheet(m_project->formatting).styles)
            m_style->addItem(styles::marker(s) + s.name, s.name);
    }
    m_style->setCurrentIndex(qMax(0, m_style->findData(cur)));
}

void FormatFindWindow::setColor(const QColor &c)
{
    m_color = c;
    m_colorButton->setIcon(colorIcon(c));
}

void FormatFindWindow::kindChanged()
{
    using fmtfind::Kind;
    int page = 0;
    switch (Kind(m_kind->currentData().toInt())) {
    case Kind::Highlight:
    case Kind::CommentOrFootnote:
    case Kind::InlineAnnotation:
    case Kind::ColoredText:
        page = 1;
        break;
    case Kind::Revision:
        page = 2;
        break;
    case Kind::CharFormat:
        page = 3;
        break;
    case Kind::Style:
        page = 4;
        break;
    default:
        break;
    }
    m_stack->setCurrentIndex(page);
    m_containing->setEnabled(Kind(m_kind->currentData().toInt()) != Kind::Image);
    m_status->clear();
}

fmtfind::Query FormatFindWindow::query() const
{
    fmtfind::Query q;
    q.kind = fmtfind::Kind(m_kind->currentData().toInt());
    q.containing = m_containing->isEnabled() ? m_containing->text() : QString();
    if (m_stack->currentIndex() == 1 && m_limitColor->isChecked())
        q.color = m_color;
    q.revision = m_revision->currentData().toInt();
    q.charFormat = (m_bold->isChecked() ? fmtfind::Bold : 0) | (m_italic->isChecked() ? fmtfind::Italic : 0)
                   | (m_underline->isChecked() ? fmtfind::Underline : 0) | (m_strike->isChecked() ? fmtfind::Strikethrough : 0);
    q.style = m_style->currentData().toString();
    return q;
}

void FormatFindWindow::setQuery(const fmtfind::Query &q)
{
    m_kind->setCurrentIndex(qMax(0, m_kind->findData(int(q.kind))));
    m_containing->setText(q.containing);
    m_limitColor->setChecked(q.color.isValid());
    if (q.color.isValid())
        setColor(q.color);
    m_revision->setCurrentIndex(qMax(0, m_revision->findData(q.revision)));
    m_bold->setChecked(q.charFormat & fmtfind::Bold);
    m_italic->setChecked(q.charFormat & fmtfind::Italic);
    m_underline->setChecked(q.charFormat & fmtfind::Underline);
    m_strike->setChecked(q.charFormat & fmtfind::Strikethrough);
    m_style->setCurrentIndex(qMax(0, m_style->findData(q.style)));
    kindChanged();
}

bool FormatFindWindow::allDocuments() const { return m_scope->currentIndex() == 0; }
void FormatFindWindow::setAllDocuments(bool on) { m_scope->setCurrentIndex(on ? 0 : 1); }
void FormatFindWindow::setStatus(const QString &text) { m_status->setText(text); }
QString FormatFindWindow::status() const { return m_status->text(); }

} // namespace qb
