#include "TextStatistics.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

using namespace qb::tools;

static const char *kTool = "text-statistics";

QList<Command> TextStatisticsTool::commands() const
{
    Command c;
    c.key = QStringLiteral("text-statistics.show");
    c.text = tr("Te&xt Statistics…");
    c.place = QStringLiteral("project");
    c.icon = QStringLiteral("office-chart-bar");
    c.toolTip = tr("Counts, readability, word frequency and repeated phrases of a selection, documents or the whole project");
    c.needsProject = true;
    return {c};
}

void TextStatisticsTool::run(const QString &key)
{
    if (key != QLatin1String("text-statistics.show") || !m_host || !m_host->hasProject())
        return;
    if (!m_window) {
        m_window = new TextStatisticsWindow(m_host, m_host->window());
        m_window->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(m_host->events(), SIGNAL(projectChanged()), m_window, SLOT(close()));
    } else {
        // new selection or document: start where the writer is now
        m_window->setScope(m_host->selectedText().trimmed().isEmpty() ? int(Scope::Document) : int(Scope::Selection));
    }
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

void TextStatisticsTool::shutdown()
{
    if (m_window)
        m_window->close();
}

// ------------------------------------------------------------------ share bar in the documents table

namespace {
class ShareDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        QStyledItemDelegate::paint(p, opt, QModelIndex()); // background, selection
        const double share = idx.data(Qt::UserRole).toDouble();
        QRect r = opt.rect.adjusted(4, 4, -4, -4);
        const int textW = opt.fontMetrics.horizontalAdvance(QStringLiteral("100.0 %")) + 6;
        QRect bar(r.left(), r.top(), qMax(0, r.width() - textW), r.height());
        p->save();
        p->setPen(Qt::NoPen);
        QColor c = opt.palette.color(QPalette::Highlight);
        c.setAlpha(opt.state & QStyle::State_Selected ? 255 : 170);
        p->setBrush(c);
        p->drawRect(QRect(bar.left(), bar.top(), int(bar.width() * qBound(0.0, share, 1.0)), bar.height()));
        p->setPen(opt.palette.color(opt.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
        p->drawText(QRect(bar.right() + 6, r.top(), textW, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                    idx.data(Qt::DisplayRole).toString());
        p->restore();
    }
};

class NumberItem : public QTableWidgetItem
{
public:
    NumberItem(double value, const QString &text) : QTableWidgetItem(text) { setData(Qt::UserRole + 1, value); }
    bool operator<(const QTableWidgetItem &o) const override
    {
        return data(Qt::UserRole + 1).toDouble() < o.data(Qt::UserRole + 1).toDouble();
    }
};

QTableWidget *makeTable(QWidget *parent, const QString &name, const QStringList &headers)
{
    auto *t = new QTableWidget(parent);
    t->setObjectName(name);
    t->setColumnCount(int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->verticalHeader()->setVisible(false);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setSortingEnabled(true);
    t->setAlternatingRowColors(true);
    return t;
}
} // namespace

// ------------------------------------------------------------------ window

TextStatisticsWindow::TextStatisticsWindow(ToolHost *host, QWidget *parent) : QWidget(parent, Qt::Window), m_host(host)
{
    setObjectName(QStringLiteral("textStatistics"));
    setWindowTitle(tr("Text Statistics"));
    auto *v = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    m_scope = new QComboBox(this);
    m_scope->setObjectName(QStringLiteral("tsScope"));
    m_scope->addItem(tr("Selected text"), int(Scope::Selection));
    m_scope->addItem(tr("Text in the editor"), int(Scope::Document));
    m_scope->addItem(tr("Documents selected in the binder"), int(Scope::BinderSelection));
    m_scope->addItem(tr("Draft (included in compile)"), int(Scope::Draft));
    m_scope->addItem(tr("Whole project"), int(Scope::Project));
    m_lang = new QComboBox(this);
    m_lang->setObjectName(QStringLiteral("tsLanguage"));
    m_lang->addItem(tr("Language: automatic"), int(textstats::Language::Auto));
    m_lang->addItem(tr("English"), int(textstats::Language::English));
    m_lang->addItem(tr("German"), int(textstats::Language::German));
    m_lang->addItem(tr("Other language"), int(textstats::Language::Other));
    auto *refreshBtn = new QPushButton(tr("Count Again"), this);
    refreshBtn->setObjectName(QStringLiteral("tsRefresh"));
    top->addWidget(new QLabel(tr("Text:"), this));
    top->addWidget(m_scope, 1);
    top->addWidget(m_lang);
    top->addWidget(refreshBtn);
    v->addLayout(top);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("tsSummary"));
    v->addWidget(m_summary);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("tsTabs"));
    // overview
    m_overview = new QTreeWidget(m_tabs);
    m_overview->setObjectName(QStringLiteral("tsOverview"));
    m_overview->setColumnCount(2);
    m_overview->setHeaderLabels({tr("Measure"), tr("Value")});
    m_overview->setRootIsDecorated(false);
    m_overview->setAlternatingRowColors(true);
    m_tabs->addTab(m_overview, tr("Overview"));
    // words
    auto *wordsPage = new QWidget(m_tabs);
    auto *wv = new QVBoxLayout(wordsPage);
    auto *wopt = new QHBoxLayout;
    m_hideCommon = new QCheckBox(tr("Hide common words"), wordsPage);
    m_hideCommon->setObjectName(QStringLiteral("tsHideCommon"));
    m_hideCommon->setChecked(m_host->setting(QString::fromLatin1(kTool), QStringLiteral("hideCommon"), true).toBool());
    m_hideCommon->setToolTip(tr("Leaves out articles, pronouns, prepositions and the like (English and German)"));
    m_case = new QCheckBox(tr("Case-sensitive"), wordsPage);
    m_case->setObjectName(QStringLiteral("tsCase"));
    m_minLength = new QSpinBox(wordsPage);
    m_minLength->setObjectName(QStringLiteral("tsMinLength"));
    m_minLength->setRange(1, 20);
    m_minLength->setValue(m_host->setting(QString::fromLatin1(kTool), QStringLiteral("minLength"), 1).toInt());
    m_minLength->setPrefix(tr("at least "));
    m_minLength->setSuffix(tr(" letters"));
    m_filter = new QLineEdit(wordsPage);
    m_filter->setObjectName(QStringLiteral("tsFilter"));
    m_filter->setPlaceholderText(tr("Find a word"));
    m_filter->setClearButtonEnabled(true);
    wopt->addWidget(m_hideCommon);
    wopt->addWidget(m_case);
    wopt->addWidget(m_minLength);
    wopt->addWidget(m_filter, 1);
    wv->addLayout(wopt);
    m_words = makeTable(wordsPage, QStringLiteral("tsWords"), {tr("Word"), tr("Count"), tr("Share")});
    m_words->setToolTip(tr("Double-click: search the project for the word"));
    wv->addWidget(m_words, 1);
    m_tabs->addTab(wordsPage, tr("Word Frequency"));
    // phrases
    auto *phrPage = new QWidget(m_tabs);
    auto *pv = new QVBoxLayout(phrPage);
    auto *popt = new QHBoxLayout;
    m_phraseMin = new QSpinBox(phrPage);
    m_phraseMin->setObjectName(QStringLiteral("tsPhraseMin"));
    m_phraseMin->setRange(2, 100);
    m_phraseMin->setValue(2);
    m_phraseMin->setPrefix(tr("used at least "));
    m_phraseMin->setSuffix(tr(" times"));
    m_phraseLen = new QSpinBox(phrPage);
    m_phraseLen->setObjectName(QStringLiteral("tsPhraseLength"));
    m_phraseLen->setRange(2, 6);
    m_phraseLen->setValue(4);
    m_phraseLen->setPrefix(tr("up to "));
    m_phraseLen->setSuffix(tr(" words"));
    popt->addWidget(new QLabel(tr("Phrases"), phrPage));
    popt->addWidget(m_phraseMin);
    popt->addWidget(m_phraseLen);
    popt->addStretch(1);
    pv->addLayout(popt);
    m_phrases = makeTable(phrPage, QStringLiteral("tsPhrases"), {tr("Phrase"), tr("Words"), tr("Count")});
    m_phrases->setToolTip(tr("Repeated word groups inside sentences; double-click to search the project"));
    pv->addWidget(m_phrases, 1);
    m_tabs->addTab(phrPage, tr("Repeated Phrases"));
    // documents
    m_docs = makeTable(m_tabs, QStringLiteral("tsDocs"),
                       {tr("Document"), tr("Folder"), tr("Words"), tr("Characters"), tr("Sentences"), tr("Words per sentence"), tr("Share")});
    m_docs->setItemDelegateForColumn(6, new ShareDelegate(m_docs));
    m_docs->setToolTip(tr("Double-click: open the document"));
    m_tabs->addTab(m_docs, tr("Documents"));
    v->addWidget(m_tabs, 1);

    auto *bottom = new QHBoxLayout;
    auto *note = new QLabel(tr("Readability scores are estimates: syllables and sentences are found by rules of thumb."), this);
    note->setEnabled(false);
    note->setWordWrap(true);
    auto *exportBtn = new QPushButton(tr("Export Table…"), this);
    exportBtn->setObjectName(QStringLiteral("tsExport"));
    exportBtn->setToolTip(tr("Saves the table of the current tab as a CSV file (for a spreadsheet)"));
    bottom->addWidget(note, 1);
    bottom->addWidget(exportBtn);
    v->addLayout(bottom);
    resize(720, 560);

    connect(refreshBtn, &QPushButton::clicked, this, &TextStatisticsWindow::refresh);
    connect(m_scope, qOverload<int>(&QComboBox::activated), this, [this] { refresh(); });
    connect(m_lang, qOverload<int>(&QComboBox::activated), this, [this] { refresh(); });
    for (QCheckBox *cb : {m_hideCommon, m_case})
        connect(cb, &QCheckBox::toggled, this, &TextStatisticsWindow::fillWords);
    connect(m_minLength, qOverload<int>(&QSpinBox::valueChanged), this, &TextStatisticsWindow::fillWords);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &t) {
        for (int r = 0; r < m_words->rowCount(); ++r)
            m_words->setRowHidden(r, !t.isEmpty() && !m_words->item(r, 0)->text().contains(t, Qt::CaseInsensitive));
    });
    connect(m_phraseMin, qOverload<int>(&QSpinBox::valueChanged), this, &TextStatisticsWindow::fillPhrases);
    connect(m_phraseLen, qOverload<int>(&QSpinBox::valueChanged), this, &TextStatisticsWindow::fillPhrases);
    connect(m_words, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { m_host->searchProject(m_words->item(row, 0)->text()); });
    connect(m_phrases, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { m_host->searchProject(m_phrases->item(row, 0)->text()); });
    connect(m_docs, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QString id = m_docs->item(row, 0)->data(Qt::UserRole).toString();
        if (!id.isEmpty())
            m_host->showDocument(id);
    });
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Export Table"),
                                                       QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                                           + QLatin1Char('/') + m_tabs->tabText(m_tabs->currentIndex()) + QStringLiteral(".csv"),
                                                       tr("CSV files (*.csv)"));
        if (!f.isEmpty())
            exportCsv(f, m_tabs->currentIndex());
    });

    m_lang->setCurrentIndex(m_host->setting(QString::fromLatin1(kTool), QStringLiteral("language"), 0).toInt());
    m_scope->setCurrentIndex(m_host->selectedText().trimmed().isEmpty() ? 1 : 0);
    refresh();
}

void TextStatisticsWindow::setScope(int scope)
{
    const int i = m_scope->findData(scope);
    if (i >= 0)
        m_scope->setCurrentIndex(i);
    refresh();
}

void TextStatisticsWindow::setLanguage(int language)
{
    const int i = m_lang->findData(language);
    if (i >= 0)
        m_lang->setCurrentIndex(i);
    refresh();
}

void TextStatisticsWindow::refresh()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const Scope scope = Scope(m_scope->currentData().toInt());
    m_inputs.clear();
    for (const TextDocument &d : m_host->documents(scope)) {
        textstats::Input in;
        in.id = d.id;
        in.title = d.title;
        in.path = d.path;
        in.text = d.text;
        m_inputs << in;
    }
    ToolHost *host = m_host;
    m_result = textstats::analyze(m_inputs, textstats::Language(m_lang->currentData().toInt()),
                                  [host](const QString &t) { return host->countWords(t); });
    m_host->setSetting(QString::fromLatin1(kTool), QStringLiteral("language"), m_lang->currentIndex());
    QLocale loc;
    if (m_inputs.isEmpty())
        m_summary->setText(scope == Scope::Selection ? tr("No text is selected in the editor.") : tr("No text documents here."));
    else
        m_summary->setText(tr("%1: %2 words in %n document(s)", nullptr, int(m_inputs.size()))
                               .arg(m_host->projectTitle(), loc.toString(m_result.words)));
    fillOverview();
    fillWords();
    fillPhrases();
    fillDocuments();
    QApplication::restoreOverrideCursor();
}

void TextStatisticsWindow::fillOverview()
{
    m_overview->clear();
    QLocale loc;
    const textstats::Result &r = m_result;
    auto row = [this](const QString &key, const QString &label, const QString &value, const QString &tip = QString()) {
        auto *it = new QTreeWidgetItem(m_overview, {label, value});
        it->setData(0, Qt::UserRole, key);
        if (!tip.isEmpty()) {
            it->setToolTip(0, tip);
            it->setToolTip(1, tip);
        }
        return it;
    };
    auto section = [this](const QString &label) {
        auto *it = new QTreeWidgetItem(m_overview, {label});
        QFont f = it->font(0);
        f.setBold(true);
        it->setFont(0, f);
        it->setFirstColumnSpanned(true);
    };
    auto num = [&loc](double v, int digits) { return loc.toString(v, 'f', digits); };
    section(tr("Counts"));
    row(QStringLiteral("words"), tr("Words"), loc.toString(r.words), tr("Counted as in the footer bar and the targets"));
    row(QStringLiteral("chars"), tr("Characters"), loc.toString(r.chars));
    row(QStringLiteral("charsNoSpaces"), tr("Characters without spaces"), loc.toString(r.charsNoSpaces));
    row(QStringLiteral("paragraphs"), tr("Paragraphs"), loc.toString(r.paragraphs));
    row(QStringLiteral("sentences"), tr("Sentences"), loc.toString(r.sentences));
    row(QStringLiteral("uniqueWords"), tr("Different words"), loc.toString(r.uniqueWords));
    row(QStringLiteral("pages"), tr("Pages (250 words each)"), num(r.words / 250.0, 1));
    row(QStringLiteral("readingTime"), tr("Reading time (230 words a minute)"),
        tr("%1 min").arg(num(std::max(0.0, r.words / 230.0), r.words < 2300 ? 1 : 0)));
    section(tr("Averages"));
    if (r.tokens > 0) {
        row(QStringLiteral("avgWordLength"), tr("Letters per word"), num(double(r.letters) / r.tokens, 1));
        row(QStringLiteral("avgSentence"), tr("Words per sentence"), r.sentences ? num(double(r.tokens) / r.sentences, 1) : QStringLiteral("–"));
        row(QStringLiteral("avgParagraph"), tr("Words per paragraph"), r.paragraphs ? num(double(r.tokens) / r.paragraphs, 1) : QStringLiteral("–"));
        row(QStringLiteral("longestSentence"), tr("Longest sentence"), tr("%n word(s)", nullptr, r.longestSentence), r.longestSentenceText);
        row(QStringLiteral("variety"), tr("Different words per 100 words"), num(100.0 * r.uniqueWords / r.tokens, 1),
            tr("Type-token ratio; it falls as a text gets longer, so compare texts of similar length"));
        row(QStringLiteral("syllablesPerWord"), tr("Syllables per word (estimate)"), num(double(r.syllables) / r.tokens, 2));
    }
    section(tr("Readability (estimates)"));
    QString langName;
    switch (r.language) {
    case textstats::Language::English: langName = tr("English"); break;
    case textstats::Language::German: langName = tr("German"); break;
    default: langName = tr("other"); break;
    }
    row(QStringLiteral("language"), tr("Language used for the scores"), langName);
    if (!r.hasReadability) {
        row(QStringLiteral("readability"), tr("Scores"), tr("too little text (at least 10 words and one sentence)"));
    } else {
        if (r.language == textstats::Language::English) {
            row(QStringLiteral("fleschEase"), tr("Flesch Reading Ease"), tr("%1 (%2)").arg(num(r.fleschEase, 0), textstats::ease(r.fleschEase)),
                tr("0–100, higher is easier (Flesch 1948); 60–70 is plain English"));
            row(QStringLiteral("fleschKincaid"), tr("Flesch–Kincaid Grade Level"), num(r.fleschKincaid, 1),
                tr("US school grade needed to follow the text (Kincaid et al. 1975)"));
        } else if (r.language == textstats::Language::German) {
            row(QStringLiteral("amstad"), tr("Flesch-Index (Amstad)"), tr("%1 (%2)").arg(num(r.amstad, 0), textstats::ease(r.amstad)),
                tr("Flesch Reading Ease adapted to German by Amstad (1978); 0–100, higher is easier"));
            row(QStringLiteral("wstf"), tr("Wiener Sachtextformel"), num(r.wstf, 1),
                tr("Approximate school grade (4 = easy, 15 = very hard); Bamberger and Vanecek (1984), first formula"));
        }
        row(QStringLiteral("lix"), tr("LIX"), num(r.lix, 0),
            tr("Words per sentence plus the share of words with more than 6 letters (Björnsson 1968); works for many languages. "
               "Rough guide: below 30 very easy, 30–40 easy, 40–50 medium, 50–60 hard, above 60 very hard"));
    }
    m_overview->resizeColumnToContents(0);
}

void TextStatisticsWindow::fillWords()
{
    m_host->setSetting(QString::fromLatin1(kTool), QStringLiteral("hideCommon"), m_hideCommon->isChecked());
    m_host->setSetting(QString::fromLatin1(kTool), QStringLiteral("minLength"), m_minLength->value());
    const auto list = textstats::frequency(m_result, m_hideCommon->isChecked(), m_minLength->value(), m_case->isChecked());
    QLocale loc;
    m_words->setSortingEnabled(false);
    m_words->setRowCount(0);
    const int rows = int(qMin<qsizetype>(list.size(), 5000));
    m_words->setRowCount(rows);
    for (int i = 0; i < rows; ++i) {
        m_words->setItem(i, 0, new QTableWidgetItem(list.at(i).first));
        m_words->setItem(i, 1, new NumberItem(list.at(i).second, loc.toString(list.at(i).second)));
        const double share = m_result.tokens ? 100.0 * list.at(i).second / m_result.tokens : 0;
        m_words->setItem(i, 2, new NumberItem(share, loc.toString(share, 'f', 2) + QStringLiteral(" %")));
    }
    m_words->setSortingEnabled(true);
    m_words->sortItems(1, Qt::DescendingOrder);
    m_words->resizeColumnToContents(0);
    if (!m_filter->text().isEmpty())
        emit m_filter->textChanged(m_filter->text());
}

void TextStatisticsWindow::fillPhrases()
{
    const auto list = textstats::phrases(m_inputs, m_result.language, m_phraseMin->value(), m_phraseLen->value());
    QLocale loc;
    m_phrases->setSortingEnabled(false);
    m_phrases->setRowCount(int(list.size()));
    for (int i = 0; i < list.size(); ++i) {
        const int words = int(list.at(i).first.count(QLatin1Char(' '))) + 1;
        m_phrases->setItem(i, 0, new QTableWidgetItem(list.at(i).first));
        m_phrases->setItem(i, 1, new NumberItem(words, loc.toString(words)));
        m_phrases->setItem(i, 2, new NumberItem(list.at(i).second, loc.toString(list.at(i).second)));
    }
    m_phrases->setSortingEnabled(true);
    m_phrases->sortItems(2, Qt::DescendingOrder);
    m_phrases->resizeColumnToContents(0);
}

void TextStatisticsWindow::fillDocuments()
{
    QLocale loc;
    m_docs->setSortingEnabled(false);
    m_docs->setRowCount(int(m_result.docs.size()));
    for (int i = 0; i < m_result.docs.size(); ++i) {
        const textstats::DocStats &d = m_result.docs.at(i);
        auto *title = new QTableWidgetItem(d.title);
        title->setData(Qt::UserRole, d.id);
        m_docs->setItem(i, 0, title);
        m_docs->setItem(i, 1, new QTableWidgetItem(d.path));
        m_docs->setItem(i, 2, new NumberItem(d.words, loc.toString(d.words)));
        m_docs->setItem(i, 3, new NumberItem(d.chars, loc.toString(d.chars)));
        m_docs->setItem(i, 4, new NumberItem(d.sentences, loc.toString(d.sentences)));
        const double wps = d.sentences ? double(d.words) / d.sentences : 0;
        m_docs->setItem(i, 5, new NumberItem(wps, d.sentences ? loc.toString(wps, 'f', 1) : QStringLiteral("–")));
        const double share = m_result.words ? double(d.words) / m_result.words : 0;
        auto *sh = new NumberItem(share, loc.toString(100 * share, 'f', 1) + QStringLiteral(" %"));
        sh->setData(Qt::UserRole, share);
        m_docs->setItem(i, 6, sh);
    }
    m_docs->setSortingEnabled(true);
    m_docs->resizeColumnsToContents();
}

QString TextStatisticsWindow::overviewValue(const QString &key) const
{
    for (int i = 0; i < m_overview->topLevelItemCount(); ++i)
        if (m_overview->topLevelItem(i)->data(0, Qt::UserRole).toString() == key)
            return m_overview->topLevelItem(i)->text(1);
    return QString();
}

QStringList TextStatisticsWindow::topWords(int n) const
{
    QStringList out;
    for (int r = 0; r < m_words->rowCount() && out.size() < n; ++r)
        if (!m_words->isRowHidden(r))
            out << m_words->item(r, 0)->text() + QLatin1Char('=') + m_words->item(r, 1)->data(Qt::UserRole + 1).toString();
    return out;
}

int TextStatisticsWindow::wordCountOf(const QString &word) const
{
    for (int r = 0; r < m_words->rowCount(); ++r)
        if (m_words->item(r, 0)->text().compare(word, m_case->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0)
            return m_words->item(r, 1)->data(Qt::UserRole + 1).toInt();
    return 0;
}

QStringList TextStatisticsWindow::phraseList() const
{
    QStringList out;
    for (int r = 0; r < m_phrases->rowCount(); ++r)
        out << m_phrases->item(r, 0)->text() + QLatin1Char('=') + m_phrases->item(r, 2)->data(Qt::UserRole + 1).toString();
    return out;
}

int TextStatisticsWindow::documentRows() const
{
    return m_docs->rowCount();
}

bool TextStatisticsWindow::exportCsv(const QString &file, int tab)
{
    auto quote = [](QString s) {
        if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"')) || s.contains(QLatin1Char('\n'))) {
            s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            s = QLatin1Char('"') + s + QLatin1Char('"');
        }
        return s;
    };
    QStringList lines;
    if (tab == 0) {
        lines << QStringLiteral("Measure,Value");
        for (int i = 0; i < m_overview->topLevelItemCount(); ++i) {
            const QTreeWidgetItem *it = m_overview->topLevelItem(i);
            if (!it->text(1).isEmpty())
                lines << quote(it->text(0)) + QLatin1Char(',') + quote(it->text(1));
        }
    } else {
        QTableWidget *t = tab == 1 ? m_words : tab == 2 ? m_phrases : m_docs;
        QStringList head;
        for (int c = 0; c < t->columnCount(); ++c)
            head << quote(t->horizontalHeaderItem(c)->text());
        lines << head.join(QLatin1Char(','));
        for (int r = 0; r < t->rowCount(); ++r) {
            if (t->isRowHidden(r))
                continue;
            QStringList cells;
            for (int c = 0; c < t->columnCount(); ++c) {
                const QTableWidgetItem *it = t->item(r, c);
                const QVariant num = it ? it->data(Qt::UserRole + 1) : QVariant();
                // numbers without thousands separators, so spreadsheets read them
                cells << quote(num.isValid() ? QString::number(num.toDouble(), 'g', 10) : (it ? it->text() : QString()));
            }
            lines << cells.join(QLatin1Char(','));
        }
    }
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write("\xEF\xBB\xBF"); // UTF-8 mark: spreadsheet programs then read umlauts correctly
    f.write(lines.join(QLatin1Char('\n')).toUtf8() + '\n');
    return f.commit();
}
