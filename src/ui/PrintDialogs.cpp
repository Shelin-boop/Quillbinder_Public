#include "ui/PrintDialogs.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

namespace qb {

namespace {
QCheckBox *box(QWidget *parent, const char *name, const QString &text, QLayout *into)
{
    auto *c = new QCheckBox(text, parent);
    c->setObjectName(QLatin1String(name));
    into->addWidget(c);
    return c;
}
} // namespace

PrintSettingsDialog::PrintSettingsDialog(const PrintSettings &current, const PageSetup &pageView, QWidget *parent)
    : QDialog(parent), m_metric(units::metric()), m_pageView(pageView)
{
    setWindowTitle(tr("Print Settings"));
    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("How File > Print Current Document puts what the editor shows on paper: the text, "
                                "the index cards of the corkboard, or the outliner. The settings belong to this project."),
                             this);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("print_tabs"));
    lay->addWidget(m_tabs, 1);
    const QString suffix = m_metric ? tr(" cm") : tr(" in");
    auto spin = [&](QWidget *parent, const char *name, double max) {
        auto *s = new QDoubleSpinBox(parent);
        s->setObjectName(QLatin1String(name));
        s->setDecimals(2);
        s->setRange(0, max);
        s->setSingleStep(m_metric ? 0.1 : 0.05);
        s->setSuffix(suffix);
        return s;
    };
    auto fontSize = [](QWidget *parent, const char *name) {
        auto *s = new QDoubleSpinBox(parent);
        s->setObjectName(QLatin1String(name));
        s->setDecimals(1);
        s->setRange(5, 36);
        s->setSingleStep(0.5);
        s->setSuffix(tr(" pt"));
        return s;
    };

    // ---- page
    auto *page = new QWidget(m_tabs);
    auto *pl = new QVBoxLayout(page);
    auto *row = new QHBoxLayout;
    auto *paperBox = new QGroupBox(tr("Paper"), page);
    auto *pf = new QFormLayout(paperBox);
    m_paper = new QComboBox(paperBox);
    m_paper->setObjectName(QStringLiteral("print_paper"));
    for (const PageSetup::Paper &p : PageSetup::papers())
        m_paper->addItem(p.name, p.name);
    m_paper->addItem(tr("Custom"), QStringLiteral("Custom"));
    pf->addRow(tr("&Size:"), m_paper);
    m_width = spin(paperBox, "print_width", m_metric ? 100 : 40);
    m_height = spin(paperBox, "print_height", m_metric ? 150 : 60);
    pf->addRow(tr("&Width:"), m_width);
    pf->addRow(tr("&Height:"), m_height);
    m_portrait = new QRadioButton(tr("&Portrait"), paperBox);
    m_landscape = new QRadioButton(tr("&Landscape"), paperBox);
    m_landscape->setObjectName(QStringLiteral("print_landscape"));
    auto *orient = new QHBoxLayout;
    orient->addWidget(m_portrait);
    orient->addWidget(m_landscape);
    orient->addStretch(1);
    pf->addRow(tr("Orientation:"), orient);
    auto *sameAsView = new QPushButton(tr("Paper of the Page &View"), paperBox);
    sameAsView->setObjectName(QStringLiteral("print_pageViewPaper"));
    sameAsView->setToolTip(tr("The paper and margins of View > Text Editing > Page Size and Margins"));
    pf->addRow(QString(), sameAsView);
    row->addWidget(paperBox);
    auto *marginBox = new QGroupBox(tr("Margins"), page);
    auto *mf = new QFormLayout(marginBox);
    m_top = spin(marginBox, "print_top", m_metric ? 10 : 4);
    m_bottom = spin(marginBox, "print_bottom", m_metric ? 10 : 4);
    m_left = spin(marginBox, "print_left", m_metric ? 10 : 4);
    m_right = spin(marginBox, "print_right", m_metric ? 10 : 4);
    mf->addRow(tr("&Top:"), m_top);
    mf->addRow(tr("&Bottom:"), m_bottom);
    mf->addRow(tr("L&eft:"), m_left);
    mf->addRow(tr("&Right:"), m_right);
    row->addWidget(marginBox);
    pl->addLayout(row);
    auto *hfBox = new QGroupBox(tr("Header and Footer"), page);
    auto *g = new QGridLayout(hfBox);
    g->addWidget(new QLabel(tr("Left"), hfBox), 0, 1);
    g->addWidget(new QLabel(tr("Center"), hfBox), 0, 2);
    g->addWidget(new QLabel(tr("Right"), hfBox), 0, 3);
    auto field = [hfBox](const char *name) {
        auto *e = new QLineEdit(hfBox);
        e->setObjectName(QLatin1String(name));
        return e;
    };
    m_hl = field("print_headerLeft");
    m_hc = field("print_headerCenter");
    m_hr = field("print_headerRight");
    m_fl = field("print_footerLeft");
    m_fc = field("print_footerCenter");
    m_fr = field("print_footerRight");
    g->addWidget(new QLabel(tr("Header:"), hfBox), 1, 0);
    g->addWidget(m_hl, 1, 1);
    g->addWidget(m_hc, 1, 2);
    g->addWidget(m_hr, 1, 3);
    g->addWidget(new QLabel(tr("Footer:"), hfBox), 2, 0);
    g->addWidget(m_fl, 2, 1);
    g->addWidget(m_fc, 2, 2);
    g->addWidget(m_fr, 2, 3);
    m_firstPage = new QCheckBox(tr("Header and footer on the &first page as well"), hfBox);
    m_firstPage->setObjectName(QStringLiteral("print_firstPage"));
    g->addWidget(m_firstPage, 3, 0, 1, 4);
    auto *ph = new QLabel(tr("<$title> what is printed (document, folder), <$projecttitle> the project, <$author> you "
                             "(File > Options > General), <$date> today, <$p> the page, <$pages> the number of pages."),
                          hfBox);
    ph->setWordWrap(true);
    ph->setTextFormat(Qt::PlainText);
    g->addWidget(ph, 4, 0, 1, 4);
    pl->addWidget(hfBox);
    pl->addStretch(1);
    m_tabs->addTab(page, tr("Page"));

    // ---- text
    auto *text = new QWidget(m_tabs);
    auto *tl = new QVBoxLayout(text);
    auto *what = new QGroupBox(tr("Print with the text"), text);
    auto *wl = new QVBoxLayout(what);
    m_titles = box(what, "print_titles", tr("The &title of each document above its text"), wl);
    m_synopses = box(what, "print_synopses", tr("The s&ynopsis below the title"), wl);
    m_notes = box(what, "print_notes", tr("The document &notes after the text"), wl);
    m_comments = box(what, "print_comments", tr("&Comments and inline annotations as notes at the bottom of the page"), wl);
    m_endnotes = box(what, "print_endnotes", tr("&Footnotes at the end of the text (instead of the bottom of each page)"), wl);
    m_highlights = box(what, "print_highlights", tr("Keep &highlighted text highlighted"), wl);
    tl->addWidget(what);
    auto *fontBox = new QGroupBox(tr("Font and Spacing"), text);
    auto *fl = new QGridLayout(fontBox);
    m_editorFont = new QRadioButton(tr("As in the &editor (the fonts of the project, its styles and the text)"), fontBox);
    m_editorFont->setObjectName(QStringLiteral("print_editorFont"));
    m_ownFont = new QRadioButton(tr("&One font:"), fontBox);
    m_ownFont->setObjectName(QStringLiteral("print_ownFont"));
    m_font = new QFontComboBox(fontBox);
    m_font->setObjectName(QStringLiteral("print_font"));
    m_fontSize = fontSize(fontBox, "print_fontSize");
    fl->addWidget(m_editorFont, 0, 0, 1, 3);
    fl->addWidget(m_ownFont, 1, 0);
    fl->addWidget(m_font, 1, 1);
    fl->addWidget(m_fontSize, 1, 2);
    fl->addWidget(new QLabel(tr("Line spacing:"), fontBox), 2, 0);
    m_lineSpacing = new QComboBox(fontBox);
    m_lineSpacing->setObjectName(QStringLiteral("print_lineSpacing"));
    m_lineSpacing->addItem(tr("As in the editor"), 0.0);
    m_lineSpacing->addItem(tr("Single"), 1.0);
    m_lineSpacing->addItem(tr("1.5 lines"), 1.5);
    m_lineSpacing->addItem(tr("Double (room for corrections)"), 2.0);
    fl->addWidget(m_lineSpacing, 2, 1, 1, 2);
    tl->addWidget(fontBox);
    auto *textNote = new QLabel(tr("Script documents keep their script layout. The selected text alone: choose "
                                   "\"Selection\" in the print window."),
                                text);
    textNote->setWordWrap(true);
    tl->addWidget(textNote);
    tl->addStretch(1);
    m_tabs->addTab(text, tr("Text"));

    // ---- index cards
    auto *cards = new QWidget(m_tabs);
    auto *cl = new QVBoxLayout(cards);
    auto *sizeBox = new QGroupBox(tr("Cards"), cards);
    auto *sf = new QFormLayout(sizeBox);
    m_cardSize = new QComboBox(sizeBox);
    m_cardSize->setObjectName(QStringLiteral("print_cardSize"));
    for (const PrintSettings::CardSize &c : PrintSettings::cardSizes()) {
        const QString shown = c.name.contains(QLatin1String(" x ")) ? QString(c.name).replace(QLatin1String(" x "), QStringLiteral(" × ")) : c.name;
        m_cardSize->addItem(shown, c.name);
    }
    m_cardSize->addItem(tr("Custom"), QStringLiteral("Custom"));
    sf->addRow(tr("Card si&ze:"), m_cardSize);
    m_cardW = spin(sizeBox, "print_cardWidth", m_metric ? 40 : 16);
    m_cardH = spin(sizeBox, "print_cardHeight", m_metric ? 40 : 16);
    auto *wh = new QHBoxLayout;
    wh->addWidget(m_cardW);
    wh->addWidget(new QLabel(QStringLiteral("×"), sizeBox));
    wh->addWidget(m_cardH);
    wh->addStretch(1);
    sf->addRow(tr("Width × height:"), wh);
    m_cardsGrid = new QRadioButton(tr("As many cards on a sheet as &fit (with lines to cut along)"), sizeBox);
    m_cardsGrid->setObjectName(QStringLiteral("print_cardsGrid"));
    m_cardPerPage = new QRadioButton(tr("One card on each sheet: for index card stock (the paper is as large as a card)"), sizeBox);
    m_cardPerPage->setObjectName(QStringLiteral("print_cardPerPage"));
    sf->addRow(m_cardsGrid);
    sf->addRow(m_cardPerPage);
    m_cardInfo = new QLabel(sizeBox);
    m_cardInfo->setObjectName(QStringLiteral("print_cardInfo"));
    m_cardInfo->setWordWrap(true);
    sf->addRow(m_cardInfo);
    cl->addWidget(sizeBox);
    auto *showBox = new QGroupBox(tr("On each card"), cards);
    auto *show = new QGridLayout(showBox);
    auto *col1 = new QVBoxLayout, *col2 = new QVBoxLayout;
    m_cardTitle = box(showBox, "print_cardTitle", tr("Title"), col1);
    m_cardSynopsis = box(showBox, "print_cardSynopsis", tr("Synopsis"), col1);
    m_cardText = box(showBox, "print_cardText", tr("The first lines of the text when there is no synopsis"), col1);
    m_cardPictures = box(showBox, "print_cardPictures", tr("Pictures (picture documents, pictures of index cards)"), col1);
    m_cardLabel = box(showBox, "print_cardLabel", tr("Label color (a stripe at the top)"), col2);
    m_cardStatus = box(showBox, "print_cardStatus", tr("Status"), col2);
    m_cardKeywords = box(showBox, "print_cardKeywords", tr("Keywords"), col2);
    m_cardRuled = box(showBox, "print_cardRuled", tr("The lines of an index card"), col2);
    m_cardBorder = box(showBox, "print_cardBorder", tr("A line around each card"), col2);
    show->addLayout(col1, 0, 0);
    show->addLayout(col2, 0, 1);
    cl->addWidget(showBox);
    auto *cf = new QHBoxLayout;
    cf->addWidget(new QLabel(tr("Font:"), cards));
    m_cardFont = new QFontComboBox(cards);
    m_cardFont->setObjectName(QStringLiteral("print_cardFont"));
    m_cardFontSize = fontSize(cards, "print_cardFontSize");
    cf->addWidget(m_cardFont, 1);
    cf->addWidget(m_cardFontSize);
    cl->addLayout(cf);
    auto *cardNote = new QLabel(tr("The cards of the corkboard in the order shown. Only some: select them and choose "
                                   "\"Selection\" in the print window."),
                                cards);
    cardNote->setWordWrap(true);
    cl->addWidget(cardNote);
    cl->addStretch(1);
    m_tabs->addTab(cards, tr("Index Cards"));

    // ---- outliner
    auto *outl = new QWidget(m_tabs);
    auto *ol = new QVBoxLayout(outl);
    auto *of = new QFormLayout;
    m_outSynopses = new QComboBox(outl);
    m_outSynopses->setObjectName(QStringLiteral("print_outlinerSynopses"));
    m_outSynopses->addItem(tr("As the outliner shows them"), 0);
    m_outSynopses->addItem(tr("Always"), 1);
    m_outSynopses->addItem(tr("Never"), 2);
    of->addRow(tr("S&ynopses:"), m_outSynopses);
    ol->addLayout(of);
    m_outAll = box(outl, "print_outlinerAllRows", tr("Also the rows inside &closed groups"), ol);
    m_outHeader = box(outl, "print_outlinerHeader", tr("Column titles at the top of &every page"), ol);
    m_outLines = box(outl, "print_outlinerLines", tr("A &line between rows"), ol);
    m_outLabels = box(outl, "print_outlinerLabels", tr("A dot of the &label color"), ol);
    auto *off = new QHBoxLayout;
    off->addWidget(new QLabel(tr("Font:"), outl));
    m_outFont = new QFontComboBox(outl);
    m_outFont->setObjectName(QStringLiteral("print_outlinerFont"));
    m_outFontSize = fontSize(outl, "print_outlinerFontSize");
    off->addWidget(m_outFont, 1);
    off->addWidget(m_outFontSize);
    ol->addLayout(off);
    auto *outNote = new QLabel(tr("The columns of the outliner in their order and widths (View > Outliner Options); "
                                  "with many columns, Landscape on the Page tab gives them more room. Only some rows: "
                                  "select them and choose \"Selection\" in the print window."),
                               outl);
    outNote->setWordWrap(true);
    ol->addWidget(outNote);
    ol->addStretch(1);
    m_tabs->addTab(outl, tr("Outliner"));

    m_newProjects = new QCheckBox(tr("Use these settings for new projects as well"), this);
    m_newProjects->setObjectName(QStringLiteral("print_newProjects"));
    lay->addWidget(m_newProjects);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    bb->setObjectName(QStringLiteral("print_buttons"));
    m_previewButton = bb->addButton(tr("Pre&view…"), QDialogButtonBox::ActionRole);
    m_previewButton->setObjectName(QStringLiteral("print_preview"));
    m_previewButton->setEnabled(false);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this] {
        PrintSettings d;
        d.page = m_pageView;
        setSettings(d);
    });
    connect(m_previewButton, &QPushButton::clicked, this, [this] {
        if (m_preview)
            m_preview(settings());
    });
    connect(sameAsView, &QPushButton::clicked, this, [this] {
        PrintSettings s = settings();
        s.page = m_pageView;
        setSettings(s);
    });
    connect(m_paper, qOverload<int>(&QComboBox::activated), this, [this] { paperChosen(); });
    for (QDoubleSpinBox *s : {m_width, m_height})
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] {
            const int custom = m_paper->findData(QStringLiteral("Custom"));
            if (m_paper->currentIndex() != custom) {
                QSignalBlocker b(m_paper);
                m_paper->setCurrentIndex(custom);
            }
        });
    connect(m_cardSize, qOverload<int>(&QComboBox::activated), this, [this] { cardSizeChosen(); });
    for (QDoubleSpinBox *s : {m_cardW, m_cardH})
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] {
            const int custom = m_cardSize->findData(QStringLiteral("Custom"));
            if (m_cardSize->currentIndex() != custom) {
                QSignalBlocker b(m_cardSize);
                m_cardSize->setCurrentIndex(custom);
            }
            updateEnabled();
        });
    for (QAbstractButton *b : std::initializer_list<QAbstractButton *>{m_editorFont, m_ownFont, m_cardsGrid, m_cardPerPage, m_cardSynopsis})
        connect(b, &QAbstractButton::toggled, this, [this] { updateEnabled(); });
    setSettings(current);
}

double PrintSettingsDialog::toUnit(double mm) const { return m_metric ? mm / 10.0 : mm / 25.4; }
double PrintSettingsDialog::fromUnit(double v) const { return m_metric ? v * 10.0 : v * 25.4; }

void PrintSettingsDialog::setPreviewFunction(const std::function<void(const PrintSettings &)> &f)
{
    m_preview = f;
    m_previewButton->setEnabled(bool(f));
}

void PrintSettingsDialog::showTab(int tab) { m_tabs->setCurrentIndex(qBound(0, tab, m_tabs->count() - 1)); }

bool PrintSettingsDialog::useForNewProjects() const { return m_newProjects->isChecked(); }

void PrintSettingsDialog::setSettings(const PrintSettings &in)
{
    const PrintSettings s = PrintSettings::fromVariant(in.toVariant(), in);
    const PageSetup pg = s.page.normalized();
    QList<QWidget *> blocked;
    for (QWidget *w : findChildren<QWidget *>())
        if (!w->signalsBlocked()) {
            w->blockSignals(true);
            blocked << w;
        }
    int idx = m_paper->findData(pg.paper);
    if (idx < 0)
        idx = m_paper->findData(QStringLiteral("Custom"));
    m_paper->setCurrentIndex(idx);
    m_width->setValue(toUnit(pg.width));
    m_height->setValue(toUnit(pg.height));
    m_landscape->setChecked(pg.landscape);
    m_portrait->setChecked(!pg.landscape);
    m_left->setValue(toUnit(pg.left));
    m_right->setValue(toUnit(pg.right));
    m_top->setValue(toUnit(pg.top));
    m_bottom->setValue(toUnit(pg.bottom));
    m_hl->setText(s.headerLeft);
    m_hc->setText(s.headerCenter);
    m_hr->setText(s.headerRight);
    m_fl->setText(s.footerLeft);
    m_fc->setText(s.footerCenter);
    m_fr->setText(s.footerRight);
    m_firstPage->setChecked(s.headerOnFirstPage);
    m_titles->setChecked(s.titles);
    m_synopses->setChecked(s.synopses);
    m_notes->setChecked(s.notes);
    m_comments->setChecked(s.comments);
    m_endnotes->setChecked(s.endnotes);
    m_highlights->setChecked(s.highlights);
    m_editorFont->setChecked(s.editorFont);
    m_ownFont->setChecked(!s.editorFont);
    m_font->setCurrentFont(QFont(s.fontFamily.isEmpty() ? QStringLiteral("Times New Roman") : s.fontFamily));
    m_fontSize->setValue(s.fontSize);
    int ls = m_lineSpacing->findData(s.lineSpacing);
    m_lineSpacing->setCurrentIndex(ls < 0 ? 0 : ls);
    int cs = m_cardSize->findData(s.cardSize);
    if (cs < 0)
        cs = m_cardSize->findData(QStringLiteral("Custom"));
    m_cardSize->setCurrentIndex(cs);
    m_cardW->setValue(toUnit(s.cardWidth));
    m_cardH->setValue(toUnit(s.cardHeight));
    m_cardPerPage->setChecked(s.cardPerPage);
    m_cardsGrid->setChecked(!s.cardPerPage);
    m_cardTitle->setChecked(s.cardTitle);
    m_cardSynopsis->setChecked(s.cardSynopsis);
    m_cardText->setChecked(s.cardTextIfEmpty);
    m_cardLabel->setChecked(s.cardLabel);
    m_cardStatus->setChecked(s.cardStatus);
    m_cardKeywords->setChecked(s.cardKeywords);
    m_cardPictures->setChecked(s.cardPictures);
    m_cardRuled->setChecked(s.cardRuled);
    m_cardBorder->setChecked(s.cardBorder);
    m_cardFont->setCurrentFont(QFont(s.cardFont.isEmpty() ? QApplication::font().family() : s.cardFont));
    m_cardFontSize->setValue(s.cardFontSize);
    m_outSynopses->setCurrentIndex(qMax(0, m_outSynopses->findData(s.outlinerSynopses)));
    m_outAll->setChecked(s.outlinerAllRows);
    m_outHeader->setChecked(s.outlinerHeader);
    m_outLines->setChecked(s.outlinerLines);
    m_outLabels->setChecked(s.outlinerLabels);
    m_outFont->setCurrentFont(QFont(s.outlinerFont.isEmpty() ? QApplication::font().family() : s.outlinerFont));
    m_outFontSize->setValue(s.outlinerFontSize);
    for (QWidget *w : std::as_const(blocked))
        w->blockSignals(false);
    updateEnabled();
}

PrintSettings PrintSettingsDialog::settings() const
{
    PrintSettings s;
    s.page.paper = m_paper->currentData().toString();
    s.page.width = fromUnit(m_width->value());
    s.page.height = fromUnit(m_height->value());
    for (const PageSetup::Paper &p : PageSetup::papers())
        if (p.name == s.page.paper) {
            s.page.width = p.w;
            s.page.height = p.h;
        }
    s.page.landscape = m_landscape->isChecked();
    s.page.left = fromUnit(m_left->value());
    s.page.right = fromUnit(m_right->value());
    s.page.top = fromUnit(m_top->value());
    s.page.bottom = fromUnit(m_bottom->value());
    s.page = s.page.normalized();
    s.headerLeft = m_hl->text();
    s.headerCenter = m_hc->text();
    s.headerRight = m_hr->text();
    s.footerLeft = m_fl->text();
    s.footerCenter = m_fc->text();
    s.footerRight = m_fr->text();
    s.headerOnFirstPage = m_firstPage->isChecked();
    s.titles = m_titles->isChecked();
    s.synopses = m_synopses->isChecked();
    s.notes = m_notes->isChecked();
    s.comments = m_comments->isChecked();
    s.endnotes = m_endnotes->isChecked();
    s.highlights = m_highlights->isChecked();
    s.editorFont = m_editorFont->isChecked();
    s.fontFamily = m_font->currentFont().family();
    s.fontSize = m_fontSize->value();
    s.lineSpacing = m_lineSpacing->currentData().toDouble();
    s.cardSize = m_cardSize->currentData().toString();
    s.cardWidth = fromUnit(m_cardW->value());
    s.cardHeight = fromUnit(m_cardH->value());
    for (const PrintSettings::CardSize &c : PrintSettings::cardSizes())
        if (c.name == s.cardSize) {
            s.cardWidth = c.w;
            s.cardHeight = c.h;
        }
    s.cardPerPage = m_cardPerPage->isChecked();
    s.cardTitle = m_cardTitle->isChecked();
    s.cardSynopsis = m_cardSynopsis->isChecked();
    s.cardTextIfEmpty = m_cardText->isChecked();
    s.cardLabel = m_cardLabel->isChecked();
    s.cardStatus = m_cardStatus->isChecked();
    s.cardKeywords = m_cardKeywords->isChecked();
    s.cardPictures = m_cardPictures->isChecked();
    s.cardRuled = m_cardRuled->isChecked();
    s.cardBorder = m_cardBorder->isChecked();
    s.cardFont = m_cardFont->currentFont().family() == QApplication::font().family() ? QString() : m_cardFont->currentFont().family();
    s.cardFontSize = m_cardFontSize->value();
    s.outlinerSynopses = m_outSynopses->currentData().toInt();
    s.outlinerAllRows = m_outAll->isChecked();
    s.outlinerHeader = m_outHeader->isChecked();
    s.outlinerLines = m_outLines->isChecked();
    s.outlinerLabels = m_outLabels->isChecked();
    s.outlinerFont = m_outFont->currentFont().family() == QApplication::font().family() ? QString() : m_outFont->currentFont().family();
    s.outlinerFontSize = m_outFontSize->value();
    return s;
}

void PrintSettingsDialog::paperChosen()
{
    const QString name = m_paper->currentData().toString();
    for (const PageSetup::Paper &p : PageSetup::papers())
        if (p.name == name) {
            QSignalBlocker b1(m_width), b2(m_height);
            m_width->setValue(toUnit(p.w));
            m_height->setValue(toUnit(p.h));
        }
}

void PrintSettingsDialog::cardSizeChosen()
{
    const QString name = m_cardSize->currentData().toString();
    for (const PrintSettings::CardSize &c : PrintSettings::cardSizes())
        if (c.name == name) {
            QSignalBlocker b1(m_cardW), b2(m_cardH);
            m_cardW->setValue(toUnit(c.w));
            m_cardH->setValue(toUnit(c.h));
        }
    updateEnabled();
}

void PrintSettingsDialog::updateEnabled()
{
    m_font->setEnabled(m_ownFont->isChecked());
    m_fontSize->setEnabled(m_ownFont->isChecked());
    m_cardText->setEnabled(m_cardSynopsis->isChecked());
    // how many cards go on a sheet of the paper on the Page tab
    const PrintSettings s = settings();
    const QSizeF paper = s.page.sizeMm();
    const double w = paper.width() - s.page.left - s.page.right, h = paper.height() - s.page.top - s.page.bottom;
    const double gap = 5;
    const int cols = qMax(1, int((w + gap) / (s.cardWidth + gap))), rows = qMax(1, int((h + gap) / (s.cardHeight + gap)));
    const bool fits = s.cardWidth <= w && s.cardHeight <= h;
    if (s.cardPerPage)
        m_cardInfo->setText(tr("Each card is a sheet of its own: choose index card paper in the print window "
                               "if the printer asks."));
    else if (!fits)
        m_cardInfo->setText(tr("A card is larger than the paper inside its margins: one card on each sheet, made smaller."));
    else
        m_cardInfo->setText(tr("%1 on each sheet (%2 × %3) of the paper on the Page tab.")
                                .arg(text::plural(cols * rows, tr("card"), tr("cards")))
                                .arg(cols)
                                .arg(rows));
}

} // namespace qb
