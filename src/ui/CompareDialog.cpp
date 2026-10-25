#include "CompareDialog.h"

#include "core/Project.h"
#include "ui/AppSettings.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace qb {

using namespace compare;

static QString esc(const QString &s)
{
    QString h = s.toHtmlEscaped();
    h.replace(QLatin1Char('\n'), QLatin1String("<br>"));
    h.replace(QLatin1Char('\t'), QLatin1String("&nbsp;&nbsp;&nbsp;&nbsp;"));
    return h;
}

static QString fmtWrap(const QString &html, quint8 f)
{
    QString h = html;
    if (f & Bold)
        h = QStringLiteral("<b>") + h + QStringLiteral("</b>");
    if (f & Italic)
        h = QStringLiteral("<i>") + h + QStringLiteral("</i>");
    if (f & Underline)
        h = QStringLiteral("<u>") + h + QStringLiteral("</u>");
    return h;
}

// a piece of a paragraph with its own bold, italic, underline
static QString runs(const Para &p, int start, int end)
{
    QString out;
    int i = start;
    while (i < end) {
        int j = i + 1;
        while (j < end && p.fmt.value(j) == p.fmt.value(i))
            ++j;
        out += fmtWrap(esc(p.text.mid(i, j - i)), p.fmt.value(i));
        i = j;
    }
    return out;
}

static const char *kGone = "color:#b3261e; text-decoration:line-through;";
static const char *kNew = "color:#1b6e2a; background-color:#e2f3e5; text-decoration:underline;";
static const char *kLook = "background-color:#e3ecfa;";

static QString span(const char *style, const QString &html)
{
    return html.isEmpty() ? QString() : QStringLiteral("<span style=\"%1\">%2</span>").arg(QLatin1String(style), html);
}

static QString lookName(quint8 f)
{
    QStringList l;
    if (f & Bold)
        l << QObject::tr("bold");
    if (f & Italic)
        l << QObject::tr("italic");
    if (f & Underline)
        l << QObject::tr("underlined");
    return l.isEmpty() ? QObject::tr("plain") : l.join(QStringLiteral(", "));
}

CompareDialog::CompareDialog(Project *p, Result result, QWidget *parent) : QDialog(parent), m_project(p), m_result(std::move(result))
{
    setObjectName(QStringLiteral("compareDialog"));
    setWindowTitle(tr("Compare and Transfer: %1").arg(QFileInfo(m_result.filePath).fileName()));
    resize(1180, 760);
    auto *v = new QVBoxLayout(this);
    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("compareInfo"));
    m_info->setWordWrap(true);
    m_info->setText(tr("<b>%1</b>: %2 % of its text is in what Compile makes of the project (%3 words), %4 % of that is in the file (%5 words). "
                       "%6 place(s) differ.")
                        .arg(QFileInfo(m_result.filePath).fileName().toHtmlEscaped())
                        .arg(m_result.related)
                        .arg(m_result.compiledWords)
                        .arg(m_result.covered)
                        .arg(m_result.fileWords)
                        .arg(m_result.changeCount()));
    v->addWidget(m_info);

    auto *opts = new QHBoxLayout;
    m_intoProject = new QRadioButton(tr("Update the &project from the file"), this);
    m_intoProject->setObjectName(QStringLiteral("compareIntoProject"));
    m_intoFile = new QRadioButton(this);
    m_intoFile->setObjectName(QStringLiteral("compareIntoFile"));
    m_intoFile->setText(m_result.fileKind == QLatin1String("docx")
                            ? tr("Update the &file from the project (a copy with tracked changes)")
                            : tr("Update the &file from the project (a copy)"));
    if (!m_result.canWriteFile) {
        m_intoFile->setEnabled(false);
        m_intoFile->setToolTip(tr("Only Word (.docx) and LibreOffice (.odt) files can be changed; this file can only be read."));
    }
    auto *dir = new QButtonGroup(this);
    dir->addButton(m_intoProject);
    dir->addButton(m_intoFile);
    m_intoProject->setChecked(true);
    opts->addWidget(m_intoProject);
    opts->addWidget(m_intoFile);
    opts->addStretch(1);
    m_byChapter = new QCheckBox(tr("By &Chapter"), this);
    m_byChapter->setObjectName(QStringLiteral("compareByChapter"));
    m_byChapter->setToolTip(tr("Decide chapter by chapter (each with a tick box)"));
    m_stepBox = new QCheckBox(tr("&Step by Step"), this);
    m_stepBox->setObjectName(QStringLiteral("compareStepByStep"));
    m_stepBox->setToolTip(tr("Go through the changes one by one: take or skip each"));
    m_byChapter->setChecked(AppSettings::instance().compareByChapter);
    m_stepBox->setChecked(AppSettings::instance().compareStepByStep);
    opts->addWidget(m_byChapter);
    opts->addWidget(m_stepBox);
    v->addLayout(opts);

    auto *split = new QSplitter(this);
    m_list = new QTreeWidget(split);
    m_list->setObjectName(QStringLiteral("compareChapters"));
    m_list->setHeaderLabels({tr("Chapter"), tr("Differences")});
    m_list->setRootIsDecorated(false);
    m_list->header()->setStretchLastSection(true);
    m_list->setColumnWidth(0, 220);
    auto *right = new QWidget(split);
    auto *rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    m_view = new QTextBrowser(right);
    m_view->setObjectName(QStringLiteral("compareView"));
    m_view->setOpenLinks(false);
    rv->addWidget(m_view, 1);
    m_stepBar = new QWidget(right);
    auto *sb = new QHBoxLayout(m_stepBar);
    sb->setContentsMargins(0, 0, 0, 0);
    m_backBtn = new QPushButton(tr("&Back"), m_stepBar);
    m_stepLabel = new QLabel(m_stepBar);
    m_stepLabel->setObjectName(QStringLiteral("compareStepLabel"));
    m_skipBtn = new QPushButton(tr("S&kip"), m_stepBar);
    m_skipBtn->setObjectName(QStringLiteral("compareSkip"));
    m_takeBtn = new QPushButton(tr("&Take"), m_stepBar);
    m_takeBtn->setObjectName(QStringLiteral("compareTake"));
    sb->addWidget(m_backBtn);
    sb->addWidget(m_stepLabel, 1);
    sb->addWidget(m_skipBtn);
    sb->addWidget(m_takeBtn);
    rv->addWidget(m_stepBar);
    split->setStretchFactor(1, 1);
    split->setSizes({360, 820});
    v->addWidget(split, 1);

    auto *legend = new QLabel(this);
    legend->setWordWrap(true);
    v->addWidget(legend);
    auto *bb = new QDialogButtonBox(this);
    m_transferBtn = bb->addButton(tr("Transfer"), QDialogButtonBox::AcceptRole);
    m_transferBtn->setObjectName(QStringLiteral("compareTransfer"));
    bb->addButton(QDialogButtonBox::Cancel);
    v->addWidget(bb);

    auto refreshLegend = [this, legend] {
        legend->setText(direction() == Direction::IntoProject
                            ? tr("Shown: what happens to the <b>project</b>. %1 is taken from the file, %2 goes, %3 gets the file's bold, "
                                 "italic or underline. Every changed document gets a snapshot first.")
                                  .arg(span(kNew, tr("green")), span(kGone, tr("red")), span(kLook, tr("blue")))
                            : tr("Shown: what happens to the <b>file</b>. %1 is taken from the project, %2 goes, %3 gets the project's bold, "
                                 "italic or underline. The file itself stays as it is: the program writes a copy, %4.")
                                  .arg(span(kNew, tr("green")), span(kGone, tr("red")), span(kLook, tr("blue")),
                                       QFileInfo(outputPath()).fileName().toHtmlEscaped()));
    };
    connect(m_intoProject, &QRadioButton::toggled, this, [this, refreshLegend] {
        for (Pair &pr : m_result.pairs)
            if (!pr.userSet)
                pr.included = defaultIncluded(pr, direction());
        fillList();
        refreshLegend();
        updateMode();
    });
    connect(m_byChapter, &QCheckBox::toggled, this, [this](bool on) {
        AppSettings::instance().compareByChapter = on;
        AppSettings::instance().save();
        updateMode();
    });
    connect(m_stepBox, &QCheckBox::toggled, this, [this](bool on) {
        AppSettings::instance().compareStepByStep = on;
        AppSettings::instance().save();
        m_stepPair = -1;
        updateMode();
    });
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *cur) {
        if (!cur || m_filling)
            return;
        const int pair = cur->data(0, Qt::UserRole).toInt();
        if (m_stepBox->isChecked()) {
            // the first step of that chapter
            m_stepPair = pair;
            m_stepChange = m_result.pairs[pair].kind == Pair::Matched && !m_result.pairs[pair].changes.isEmpty() ? 0 : -1;
            showStep();
        } else {
            showPair(pair);
        }
    });
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it, int col) {
        if (m_filling || col != 0)
            return;
        const int pair = it->data(0, Qt::UserRole).toInt();
        Pair &pr = m_result.pairs[pair];
        pr.included = it->checkState(0) == Qt::Checked;
        pr.userSet = true;
        if (pr.included && pr.kind == Pair::Matched) {
            bool any = false;
            for (const Change &c : pr.changes)
                any = any || c.accepted;
            if (!any)
                for (Change &c : pr.changes)
                    c.accepted = true;
        }
        updateRow(pair);
        if (m_shownPair == pair && !m_stepBox->isChecked())
            showPair(pair);
        updateButtons();
    });
    connect(m_view, &QTextBrowser::anchorClicked, this, [this](const QUrl &u) {
        // "toggle:<pair>:<change>"
        const QStringList parts = u.toString().split(QLatin1Char(':'));
        if (parts.size() != 3 || parts[0] != QLatin1String("toggle"))
            return;
        const int pi = parts[1].toInt(), ci = parts[2].toInt();
        if (pi < 0 || pi >= m_result.pairs.size())
            return;
        Pair &pr = m_result.pairs[pi];
        if (ci < 0) {
            pr.included = !pr.included;
        } else if (ci < pr.changes.size()) {
            pr.changes[ci].accepted = !pr.changes[ci].accepted;
            bool any = false;
            for (const Change &c : pr.changes)
                any = any || c.accepted;
            pr.included = any;
        }
        pr.userSet = true;
        const int scroll = m_view->verticalScrollBar()->value();
        updateRow(pi);
        if (m_byChapter->isChecked() || m_stepBox->isChecked())
            showPair(pi);
        else
            m_view->setHtml(allHtml());
        m_view->verticalScrollBar()->setValue(scroll);
        updateButtons();
    });
    connect(m_takeBtn, &QPushButton::clicked, this, [this] { take(true); });
    connect(m_skipBtn, &QPushButton::clicked, this, [this] { take(false); });
    connect(m_backBtn, &QPushButton::clicked, this, [this] { nextStep(-1); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_transferBtn, &QPushButton::clicked, this, [this] {
        Report rep;
        QString err;
        if (transfer(&rep, &err)) {
            emit transferred(rep);
            accept();
        } else if (!err.isEmpty()) {
            m_info->setText(QStringLiteral("<span style=\"color:#b3261e\">%1</span>").arg(err.toHtmlEscaped()));
        }
    });
    fillList();
    refreshLegend();
    updateMode();
}

Direction CompareDialog::direction() const { return m_intoFile->isChecked() ? Direction::IntoFile : Direction::IntoProject; }

void CompareDialog::setDirection(Direction d)
{
    if (d == Direction::IntoFile && m_intoFile->isEnabled())
        m_intoFile->setChecked(true);
    else
        m_intoProject->setChecked(true);
}

void CompareDialog::setByChapter(bool on) { m_byChapter->setChecked(on); }
void CompareDialog::setStepByStep(bool on) { m_stepBox->setChecked(on); }
bool CompareDialog::stepByStep() const { return m_stepBox->isChecked(); }

QString CompareDialog::outputPath() const
{
    QString path = suggestedCopyPath(m_result.filePath);
    const QFileInfo fi(path);
    for (int n = 2; QFileInfo::exists(path) && n < 1000; ++n)
        path = fi.dir().filePath(QFileInfo(m_result.filePath).completeBaseName() + QStringLiteral(" (Quillbinder %1).").arg(n)
                                 + fi.suffix());
    return path;
}

QString CompareDialog::rowSummary(const Pair &pr) const
{
    const bool intoProject = direction() == Direction::IntoProject;
    QString s;
    switch (pr.kind) {
    case Pair::Matched:
        s = pr.changes.isEmpty() ? tr("the same") : tr("%n place(s)", nullptr, int(pr.changes.size()));
        break;
    case Pair::OnlyInFile:
        s = intoProject ? tr("only in the file: becomes a new document") : tr("only in the file: taken out of the copy");
        break;
    case Pair::OnlyInProject:
        s = intoProject ? tr("not in the file: the document goes to the Trash") : tr("not in the file: added to the copy");
        break;
    }
    if (pr.moved)
        s += tr(" · in another place in the file");
    return s;
}

void CompareDialog::fillList()
{
    m_filling = true;
    const int keep = m_list->currentItem() ? m_list->currentItem()->data(0, Qt::UserRole).toInt() : -1;
    m_list->clear();
    for (int i = 0; i < m_result.pairs.size(); ++i) {
        const Pair &pr = m_result.pairs[i];
        auto *it = new QTreeWidgetItem(m_list);
        it->setData(0, Qt::UserRole, i);
        updateRow(i);
        if (i == keep)
            m_list->setCurrentItem(it);
    }
    m_filling = false;
}

void CompareDialog::updateRow(int pair)
{
    QTreeWidgetItem *it = nullptr;
    for (int r = 0; r < m_list->topLevelItemCount(); ++r)
        if (m_list->topLevelItem(r)->data(0, Qt::UserRole).toInt() == pair)
            it = m_list->topLevelItem(r);
    if (!it)
        return;
    const bool was = m_filling;
    m_filling = true;
    const Pair &pr = m_result.pairs[pair];
    QString title = pr.title.isEmpty() ? tr("(untitled)") : pr.title;
    if (pr.kind == Pair::Matched && !pr.fileTitle.isEmpty() && pr.fileTitle != pr.title)
        it->setToolTip(0, tr("In the file: %1").arg(pr.fileTitle));
    it->setText(0, title);
    QString summary = rowSummary(pr);
    if (pr.kind == Pair::Matched && !pr.changes.isEmpty()) {
        int acc = 0;
        for (const Change &c : pr.changes)
            acc += c.accepted ? 1 : 0;
        if (acc != pr.changes.size())
            summary += tr(" (%1 taken)").arg(acc);
    }
    it->setText(1, summary);
    const bool actionable = !(pr.kind == Pair::Matched && pr.changes.isEmpty());
    if (actionable) {
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        it->setCheckState(0, pr.included ? Qt::Checked : Qt::Unchecked);
    } else {
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        it->setData(0, Qt::CheckStateRole, QVariant());
        it->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
        it->setForeground(1, palette().color(QPalette::Disabled, QPalette::Text));
    }
    m_filling = was;
}

QString CompareDialog::changeHtml(const Pair &pr, int ci, bool withContext) const
{
    const bool intoProject = direction() == Direction::IntoProject;
    const Change &c = pr.changes[ci];
    const int pi = int(&pr - &m_result.pairs[0]);
    QString h = QStringLiteral("<p style=\"margin-top:10px; margin-bottom:2px;\"><a href=\"toggle:%1:%2\" style=\"text-decoration:none;\">%3</a>"
                               " <span style=\"color:gray;\">%4</span></p>")
                    .arg(pi)
                    .arg(ci)
                    .arg(c.accepted && pr.included ? tr("☑ take") : tr("☐ skip"))
                    .arg(tr("Place %1 of %2").arg(ci + 1).arg(pr.changes.size()));
    const QList<Para> &target = intoProject ? pr.project : pr.file;
    const QList<Para> &source = intoProject ? pr.file : pr.project;
    auto context = [&](int idx) {
        if (!withContext || idx < 0 || idx >= target.size())
            return QString();
        return QStringLiteral("<p style=\"color:gray;\">%1</p>").arg(esc(target[idx].text));
    };
    const int tFirst = intoProject ? c.pFirst : c.fFirst;
    const int tCount = intoProject ? c.pCount : c.fCount;
    const int sFirst = intoProject ? c.fFirst : c.pFirst;
    const int sCount = intoProject ? c.fCount : c.pCount;
    h += context(tFirst - 1);
    if (c.kind == Change::Changed || c.kind == Change::Formatting) {
        const Para &T = target[tFirst];
        const Para &S = source[sFirst];
        QList<WordOp> ops = c.ops;
        std::sort(ops.begin(), ops.end(), [intoProject](const WordOp &a, const WordOp &b) {
            return intoProject ? a.pStart < b.pStart : a.fStart < b.fStart;
        });
        QString body;
        int at = 0;
        for (const WordOp &w : ops) {
            const int ts = intoProject ? w.pStart : w.fStart, te = intoProject ? w.pEnd : w.fEnd;
            const int ss = intoProject ? w.fStart : w.pStart, se = intoProject ? w.fEnd : w.pEnd;
            body += runs(T, at, ts);
            if (w.type == WordOp::Format) {
                body += QStringLiteral("<span style=\"%1\" title=\"%2\">%3</span>")
                            .arg(QLatin1String(kLook), tr("becomes %1").arg(lookName(S.fmt.value(ss))), runs(S, ss, se));
            } else {
                body += span(kGone, runs(T, ts, te));
                body += span(kNew, runs(S, ss, se));
            }
            at = te;
        }
        body += runs(T, at, int(T.text.size()));
        h += QStringLiteral("<p>%1</p>").arg(body);
    } else {
        const bool goes = (c.kind == Change::OnlyInProject) == intoProject;
        if (goes) {
            for (int k = tFirst; k < tFirst + tCount; ++k)
                h += QStringLiteral("<p>%1</p>").arg(span(kGone, runs(target[k], 0, int(target[k].text.size()))));
        } else {
            for (int k = sFirst; k < sFirst + sCount; ++k)
                h += QStringLiteral("<p>%1</p>").arg(span(kNew, runs(source[k], 0, int(source[k].text.size()))));
        }
    }
    h += context(c.kind == Change::OnlyInFile && intoProject ? tFirst : tFirst + qMax(1, tCount));
    return h;
}

QString CompareDialog::pairHtml(int pair) const
{
    const Pair &pr = m_result.pairs[pair];
    const bool intoProject = direction() == Direction::IntoProject;
    QString h = QStringLiteral("<h3>%1</h3>").arg(esc(pr.title.isEmpty() ? tr("(untitled)") : pr.title));
    if (!pr.fileTitle.isEmpty() && pr.fileTitle != pr.title)
        h += QStringLiteral("<p style=\"color:gray;\">%1</p>").arg(esc(tr("In the file: %1").arg(pr.fileTitle)));
    if (pr.moved)
        h += QStringLiteral("<p style=\"color:gray;\">%1</p>")
                 .arg(esc(tr("The file has this chapter in another place than Compile; its place is not changed.")));
    if (pr.kind == Pair::Matched) {
        if (pr.changes.isEmpty())
            h += QStringLiteral("<p>%1</p>").arg(esc(tr("The words and their bold, italic and underline are the same.")));
        for (int ci = 0; ci < pr.changes.size(); ++ci)
            h += changeHtml(pr, ci, true);
        return h;
    }
    const bool goes = (pr.kind == Pair::OnlyInProject) == intoProject;
    h += QStringLiteral("<p><a href=\"toggle:%1:-1\" style=\"text-decoration:none;\">%2</a> %3</p>")
             .arg(pair)
             .arg(pr.included ? tr("☑ take") : tr("☐ skip"))
             .arg(esc(rowSummary(pr)));
    const QList<Para> &paras = pr.kind == Pair::OnlyInProject ? pr.project : pr.file;
    for (const Para &para : paras)
        h += QStringLiteral("<p>%1</p>").arg(span(goes ? kGone : kNew, runs(para, 0, int(para.text.size()))));
    return h;
}

QString CompareDialog::allHtml() const
{
    QString h;
    int shown = 0;
    for (int i = 0; i < m_result.pairs.size(); ++i) {
        const Pair &pr = m_result.pairs[i];
        if (pr.kind == Pair::Matched && pr.changes.isEmpty())
            continue;
        h += pairHtml(i);
        ++shown;
    }
    if (!shown)
        h = QStringLiteral("<p>%1</p>").arg(esc(tr("Nothing differs.")));
    return h;
}

void CompareDialog::showPair(int pair)
{
    m_shownPair = pair;
    if (pair < 0 || pair >= m_result.pairs.size()) {
        m_view->clear();
        return;
    }
    m_view->setHtml(pairHtml(pair));
}

// the steps: every change of a document, every chapter only on one side
static QList<QPair<int, int>> stepsOf(const Result &r)
{
    QList<QPair<int, int>> s;
    for (int i = 0; i < r.pairs.size(); ++i) {
        const Pair &pr = r.pairs[i];
        if (pr.kind == Pair::Matched)
            for (int c = 0; c < pr.changes.size(); ++c)
                s.append({i, c});
        else
            s.append({i, -1});
    }
    return s;
}

void CompareDialog::showStep()
{
    const QList<QPair<int, int>> steps = stepsOf(m_result);
    if (steps.isEmpty()) {
        m_stepLabel->setText(tr("Nothing differs."));
        m_view->setHtml(allHtml());
        return;
    }
    int idx = int(steps.indexOf(qMakePair(m_stepPair, m_stepChange)));
    if (idx < 0) {
        idx = 0;
        m_stepPair = steps[0].first;
        m_stepChange = steps[0].second;
    }
    const Pair &pr = m_result.pairs[m_stepPair];
    m_stepLabel->setText(tr("Change %1 of %2, in \"%3\"").arg(idx + 1).arg(steps.size()).arg(pr.title));
    QString h = QStringLiteral("<h3>%1</h3>").arg(esc(pr.title));
    if (m_stepChange >= 0)
        h += changeHtml(pr, m_stepChange, true);
    else
        h = pairHtml(m_stepPair);
    m_view->setHtml(h);
    m_backBtn->setEnabled(idx > 0);
    m_shownPair = m_stepPair;
    for (int r = 0; r < m_list->topLevelItemCount(); ++r)
        if (m_list->topLevelItem(r)->data(0, Qt::UserRole).toInt() == m_stepPair) {
            m_filling = true;
            m_list->setCurrentItem(m_list->topLevelItem(r));
            m_filling = false;
        }
}

void CompareDialog::nextStep(int delta)
{
    const QList<QPair<int, int>> steps = stepsOf(m_result);
    int idx = int(steps.indexOf(qMakePair(m_stepPair, m_stepChange)));
    idx += delta;
    if (idx < 0)
        idx = 0;
    if (idx >= steps.size()) {
        m_stepLabel->setText(tr("All changes seen: %n to take. Press Transfer.", nullptr, acceptedCount()));
        m_view->setHtml(allHtml());
        m_takeBtn->setEnabled(false);
        m_skipBtn->setEnabled(false);
        m_stepPair = -2;
        updateButtons();
        return;
    }
    m_takeBtn->setEnabled(true);
    m_skipBtn->setEnabled(true);
    m_stepPair = steps[idx].first;
    m_stepChange = steps[idx].second;
    showStep();
}

void CompareDialog::take(bool yes)
{
    if (m_stepPair < 0 || m_stepPair >= m_result.pairs.size())
        return;
    Pair &pr = m_result.pairs[m_stepPair];
    if (m_stepChange >= 0) {
        pr.changes[m_stepChange].accepted = yes;
        bool any = false;
        for (const Change &c : pr.changes)
            any = any || c.accepted;
        pr.included = any;
    } else {
        pr.included = yes;
    }
    pr.userSet = true;
    updateRow(m_stepPair);
    updateButtons();
    nextStep(1);
}

void CompareDialog::updateMode()
{
    const bool step = m_stepBox->isChecked();
    const bool chapters = m_byChapter->isChecked() || step;
    m_list->setVisible(chapters);
    m_stepBar->setVisible(step);
    if (step) {
        // from the first change (what is ticked stays ticked until skipped)
        if (m_stepPair == -1) {
            const QList<QPair<int, int>> steps = stepsOf(m_result);
            if (!steps.isEmpty()) {
                m_stepPair = steps[0].first;
                m_stepChange = steps[0].second;
            }
            m_takeBtn->setEnabled(true);
            m_skipBtn->setEnabled(true);
        }
        if (m_stepPair >= 0)
            showStep();
    } else if (chapters) {
        int pair = m_shownPair;
        if (pair < 0)
            for (int i = 0; i < m_result.pairs.size() && pair < 0; ++i)
                if (!(m_result.pairs[i].kind == Pair::Matched && m_result.pairs[i].changes.isEmpty()))
                    pair = i;
        if (pair >= 0) {
            for (int r = 0; r < m_list->topLevelItemCount(); ++r)
                if (m_list->topLevelItem(r)->data(0, Qt::UserRole).toInt() == pair)
                    m_list->setCurrentItem(m_list->topLevelItem(r));
            showPair(pair);
        }
    } else {
        m_shownPair = -1;
        m_view->setHtml(allHtml());
    }
    updateButtons();
}

int CompareDialog::acceptedCount() const
{
    int n = 0;
    for (const Pair &pr : m_result.pairs) {
        if (!pr.included)
            continue;
        if (pr.kind == Pair::Matched) {
            for (const Change &c : pr.changes)
                n += c.accepted ? 1 : 0;
        } else {
            ++n;
        }
    }
    return n;
}

void CompareDialog::updateButtons()
{
    const int n = acceptedCount();
    m_transferBtn->setEnabled(n > 0);
    m_transferBtn->setText(direction() == Direction::IntoProject ? tr("Transfer %n Change(s) into the Project", nullptr, n)
                                                                 : tr("Write %n Change(s) into a Copy of the File", nullptr, n));
}

bool CompareDialog::transfer(Report *report, QString *error)
{
    return compare::apply(m_project, m_result, direction(), direction() == Direction::IntoFile ? outputPath() : QString(), report, error);
}

} // namespace qb
