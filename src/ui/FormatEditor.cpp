// Edit Compile Format: the sections of a compile format, each a page of its own.
#include "ui/CompileDialog.h"

#include "compile/Compiler.h"
#include "text/Styles.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <memory>

namespace qb {

using namespace compile;

namespace {

QWidget *scrolled(QWidget *inner)
{
    auto *sa = new QScrollArea;
    sa->setWidget(inner);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    return sa;
}

QDoubleSpinBox *spin(QWidget *parent, double lo, double hi, double step, const QString &suffix, const QString &name,
                     int decimals = 1)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    if (!suffix.isEmpty())
        s->setSuffix(suffix);
    s->setObjectName(name);
    return s;
}

// a check box with a third state: "as the style has it"
QCheckBox *tri(QWidget *parent, const QString &text, int value, const QString &name)
{
    auto *c = new QCheckBox(text, parent);
    c->setTristate(true);
    c->setCheckState(value < 0 ? Qt::PartiallyChecked : value ? Qt::Checked : Qt::Unchecked);
    c->setToolTip(QObject::tr("Half ticked: as the style has it"));
    c->setObjectName(name);
    return c;
}

int triValue(const QCheckBox *c) { return c->checkState() == Qt::PartiallyChecked ? -1 : c->isChecked() ? 1 : 0; }

QString styleLabel(Paragraph::Style s)
{
    switch (s) {
    case Paragraph::Title: return QObject::tr("Title");
    case Paragraph::Subtitle: return QObject::tr("Subtitle");
    case Paragraph::Heading1: return QObject::tr("Heading 1");
    case Paragraph::Heading2: return QObject::tr("Heading 2");
    case Paragraph::Heading3: return QObject::tr("Heading 3");
    default: return QObject::tr("Normal");
    }
}

} // namespace

FormatEditorDialog::FormatEditorDialog(const Format &f, Project *p, QWidget *parent) : QDialog(parent), m_format(f), m_project(p)
{
    setWindowTitle(tr("Edit Compile Format"));
    setObjectName(QStringLiteral("formatEditor"));
    resize(980, 640);
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Format name:"), this));
    auto *name = new QLineEdit(f.name, this);
    name->setObjectName(QStringLiteral("fmt_name"));
    top->addWidget(name, 1);
    lay->addLayout(top);

    auto *split = new QSplitter(Qt::Horizontal, this);
    m_sections = new QListWidget(split);
    m_sections->setObjectName(QStringLiteral("formatSections"));
    m_sections->setMaximumWidth(220);
    m_pages = new QStackedWidget(split);
    split->addWidget(m_sections);
    split->addWidget(m_pages);
    split->setStretchFactor(1, 1);
    lay->addWidget(split, 1);
    auto addPage = [this](const QString &title, QWidget *w) {
        m_sections->addItem(title);
        m_pages->addWidget(scrolled(w));
    };
    connect(m_sections, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);

    // ------------------------------------------------------------ text
    auto *gw = new QWidget;
    auto *gf = new QFormLayout(gw);
    auto *editorFmt = new QCheckBox(tr("Use the project's editor formatting instead of the settings below"), gw);
    editorFmt->setObjectName(QStringLiteral("fmt_editorFormatting"));
    editorFmt->setChecked(f.useEditorFormatting);
    auto *font = new QFontComboBox(gw);
    font->setObjectName(QStringLiteral("fmt_font"));
    font->setCurrentFont(QFont(f.fontFamily));
    font->setEditText(f.fontFamily);
    auto *hfont = new QComboBox(gw);
    hfont->setObjectName(QStringLiteral("fmt_headingFont"));
    hfont->setEditable(true);
    hfont->addItem(tr("same as text"), QString());
    {
        QFontComboBox fc;
        for (int i = 0; i < fc.count(); ++i)
            hfont->addItem(fc.itemText(i), fc.itemText(i));
    }
    hfont->setCurrentIndex(qMax(0, hfont->findData(f.headingFontFamily)));
    auto *size = spin(gw, 6, 36, 0.5, tr(" pt"), QStringLiteral("fmt_size"));
    size->setValue(f.fontSize);
    auto *spacing = spin(gw, 0.8, 3, 0.05, QString(), QStringLiteral("fmt_lineSpacing"), 2);
    spacing->setValue(f.lineSpacing);
    auto *indent = spin(gw, 0, 50, 0.5, tr(" mm"), QStringLiteral("fmt_indent"));
    indent->setValue(f.firstLineIndentMm);
    auto *after = spin(gw, 0, 48, 1, tr(" pt"), QStringLiteral("fmt_spaceAfter"));
    after->setValue(f.paragraphSpacingPt);
    auto *justify = new QCheckBox(tr("Justify"), gw);
    justify->setObjectName(QStringLiteral("fmt_justify"));
    justify->setChecked(f.justify);
    auto *hscale = spin(gw, 1, 3, 0.1, tr(" × text"), QStringLiteral("fmt_headingScale"), 2);
    hscale->setValue(f.headingScale);
    auto *hbold = new QCheckBox(tr("Bold headings"), gw);
    hbold->setObjectName(QStringLiteral("fmt_headingBold"));
    hbold->setChecked(f.headingBold);
    gf->addRow(QString(), editorFmt);
    gf->addRow(tr("Font:"), font);
    gf->addRow(tr("Size:"), size);
    gf->addRow(tr("Line spacing:"), spacing);
    gf->addRow(tr("First line indent:"), indent);
    gf->addRow(tr("Space after paragraphs:"), after);
    gf->addRow(QString(), justify);
    gf->addRow(tr("Heading font:"), hfont);
    gf->addRow(tr("Heading size:"), hscale);
    gf->addRow(QString(), hbold);
    for (QWidget *w : QList<QWidget *>{font, size, spacing, indent, after, justify})
        w->setEnabled(!f.useEditorFormatting);
    connect(editorFmt, &QCheckBox::toggled, this, [=](bool on) {
        for (QWidget *w : QList<QWidget *>{font, size, spacing, indent, after, justify})
            w->setEnabled(!on);
    });
    addPage(tr("Text"), gw);

    // ------------------------------------------------------------ section layouts
    auto *lw = new QWidget;
    auto *lh = new QHBoxLayout(lw);
    auto *left = new QVBoxLayout;
    auto *list = new QListWidget(lw);
    list->setObjectName(QStringLiteral("fmt_layouts"));
    left->addWidget(list, 1);
    auto *lb = new QHBoxLayout;
    auto *addL = new QPushButton(tr("Add"), lw);
    addL->setObjectName(QStringLiteral("fmt_addLayout"));
    auto *dupL = new QPushButton(tr("Duplicate"), lw);
    dupL->setObjectName(QStringLiteral("fmt_duplicateLayout"));
    auto *remL = new QPushButton(tr("Remove"), lw);
    remL->setObjectName(QStringLiteral("fmt_removeLayout"));
    lb->addWidget(addL);
    lb->addWidget(dupL);
    lb->addWidget(remL);
    left->addLayout(lb);
    lh->addLayout(left, 1);
    auto *right = new QVBoxLayout;
    auto *form = new QFormLayout;
    auto *lname = new QLineEdit(lw);
    lname->setObjectName(QStringLiteral("fmt_layoutName"));
    auto *ltitle = new QCheckBox(tr("Include a heading"), lw);
    ltitle->setObjectName(QStringLiteral("fmt_layoutTitle"));
    auto *lshow = new QCheckBox(tr("Heading shows the document title"), lw);
    auto *lprefix = new QLineEdit(lw);
    lprefix->setObjectName(QStringLiteral("fmt_layoutPrefix"));
    lprefix->setPlaceholderText(tr("e.g. Chapter <$W:chapter>\\n  (\\n = new line)"));
    auto *lsuffix = new QLineEdit(lw);
    auto *lupper = new QCheckBox(tr("Upper case heading"), lw);
    auto *lstyle = new QComboBox(lw);
    lstyle->setObjectName(QStringLiteral("fmt_layoutStyle"));
    const QList<Paragraph::Style> titleStyles = {Paragraph::Normal, Paragraph::Title, Paragraph::Subtitle,
                                                 Paragraph::Heading1, Paragraph::Heading2, Paragraph::Heading3};
    for (Paragraph::Style s : titleStyles)
        lstyle->addItem(styleLabel(s), int(s));
    auto *lalign = new QComboBox(lw);
    lalign->addItems({tr("Left"), tr("Center"), tr("Right")});
    auto *lblank = new QSpinBox(lw);
    lblank->setRange(0, 20);
    auto *ltext = new QCheckBox(tr("Include the text"), lw);
    auto *lsyn = new QCheckBox(tr("Include the synopsis"), lw);
    auto *lnotes = new QCheckBox(tr("Include the notes"), lw);
    auto *lbreak = new QCheckBox(tr("Start on a new page"), lw);
    lbreak->setObjectName(QStringLiteral("fmt_layoutPageBreak"));
    auto *lsep = new QComboBox(lw);
    lsep->setObjectName(QStringLiteral("fmt_layoutSeparator"));
    lsep->setEditable(true);
    lsep->addItems({QStringLiteral("none"), QStringLiteral("blank"), QStringLiteral("page"), QStringLiteral("#"),
                    QStringLiteral("* * *")});
    auto *lnoind = new QCheckBox(tr("No indent on the first paragraph"), lw);
    form->addRow(tr("Name:"), lname);
    form->addRow(QString(), ltitle);
    form->addRow(QString(), lshow);
    form->addRow(tr("Heading prefix:"), lprefix);
    form->addRow(tr("Heading suffix:"), lsuffix);
    form->addRow(QString(), lupper);
    form->addRow(tr("Heading style:"), lstyle);
    form->addRow(tr("Heading alignment:"), lalign);
    form->addRow(tr("Blank lines before heading:"), lblank);
    form->addRow(QString(), ltext);
    form->addRow(QString(), lsyn);
    form->addRow(QString(), lnotes);
    form->addRow(QString(), lbreak);
    form->addRow(tr("Separator before (after text):"), lsep);
    form->addRow(QString(), lnoind);
    right->addLayout(form);
    auto *preview = new QTextBrowser(lw);
    preview->setObjectName(QStringLiteral("fmt_layoutPreview"));
    preview->setMinimumHeight(140);
    preview->setToolTip(tr("How a section with this layout begins"));
    right->addWidget(new QLabel(tr("Preview:"), lw));
    right->addWidget(preview, 1);
    lh->addLayout(right, 2);

    auto current = std::make_shared<int>(-1);
    auto store = [=]() {
        const int i = *current;
        if (i < 0 || i >= m_format.layouts.size())
            return;
        SectionLayout &l = m_format.layouts[i];
        l.name = lname->text();
        l.includeTitle = ltitle->isChecked();
        l.showItemTitle = lshow->isChecked();
        l.titlePrefix = QString(lprefix->text()).replace(QLatin1String("\\n"), QLatin1String("\n"));
        l.titleSuffix = QString(lsuffix->text()).replace(QLatin1String("\\n"), QLatin1String("\n"));
        l.titleUppercase = lupper->isChecked();
        l.titleStyle = Paragraph::Style(lstyle->currentData().toInt());
        l.titleAlign = lalign->currentIndex() == 1 ? Qt::AlignHCenter : lalign->currentIndex() == 2 ? Qt::AlignRight : Qt::AlignLeft;
        l.blankLinesBeforeTitle = lblank->value();
        l.includeText = ltext->isChecked();
        l.includeSynopsis = lsyn->isChecked();
        l.includeNotes = lnotes->isChecked();
        l.pageBreakBefore = lbreak->isChecked();
        l.separator = lsep->currentText();
        l.noIndentFirstParagraph = lnoind->isChecked();
        if (list->item(i))
            list->item(i)->setText(l.name);
    };
    auto showPreview = [=]() {
        store();
        const int i = *current;
        if (i < 0 || i >= m_format.layouts.size()) {
            preview->clear();
            return;
        }
        // the same picture as in the compile window (CompileDialog::layoutPreviewHtml, 0.21)
        const QString html = CompileDialog::layoutPreviewHtml(m_format, m_format.layouts[i], p);
        preview->setHtml(html);
    };
    auto load = [=](int i) {
        *current = i;
        if (i < 0 || i >= m_format.layouts.size())
            return;
        const SectionLayout &l = m_format.layouts[i];
        lname->setText(l.name);
        ltitle->setChecked(l.includeTitle);
        lshow->setChecked(l.showItemTitle);
        lprefix->setText(QString(l.titlePrefix).replace(QLatin1Char('\n'), QLatin1String("\\n")));
        lsuffix->setText(QString(l.titleSuffix).replace(QLatin1Char('\n'), QLatin1String("\\n")));
        lupper->setChecked(l.titleUppercase);
        lstyle->setCurrentIndex(qMax(0, lstyle->findData(int(l.titleStyle))));
        lalign->setCurrentIndex((l.titleAlign & Qt::AlignHCenter) ? 1 : (l.titleAlign & Qt::AlignRight) ? 2 : 0);
        lblank->setValue(l.blankLinesBeforeTitle);
        ltext->setChecked(l.includeText);
        lsyn->setChecked(l.includeSynopsis);
        lnotes->setChecked(l.includeNotes);
        lbreak->setChecked(l.pageBreakBefore);
        lsep->setCurrentText(l.separator);
        lnoind->setChecked(l.noIndentFirstParagraph);
        showPreview();
    };
    for (const SectionLayout &l : m_format.layouts)
        list->addItem(l.name);
    connect(list, &QListWidget::currentRowChanged, this, [=](int r) {
        store();
        load(r);
    });
    for (QCheckBox *c : {ltitle, lshow, lupper, ltext, lsyn, lnotes, lbreak, lnoind})
        connect(c, &QCheckBox::toggled, this, showPreview);
    for (QLineEdit *e : {lprefix, lsuffix})
        connect(e, &QLineEdit::textChanged, this, showPreview);
    connect(lname, &QLineEdit::editingFinished, this, store);
    connect(lstyle, qOverload<int>(&QComboBox::currentIndexChanged), this, showPreview);
    connect(lalign, qOverload<int>(&QComboBox::currentIndexChanged), this, showPreview);
    connect(lblank, qOverload<int>(&QSpinBox::valueChanged), this, showPreview);
    connect(lsep, &QComboBox::currentTextChanged, this, showPreview);
    connect(addL, &QPushButton::clicked, this, [=] {
        store();
        SectionLayout l;
        l.name = tr("New Layout");
        m_format.layouts << l;
        list->addItem(l.name);
        list->setCurrentRow(list->count() - 1);
    });
    connect(dupL, &QPushButton::clicked, this, [=] {
        store();
        const int r = list->currentRow();
        if (r < 0)
            return;
        SectionLayout l = m_format.layouts[r];
        l.name += tr(" (copy)");
        m_format.layouts << l;
        list->addItem(l.name);
        list->setCurrentRow(list->count() - 1);
    });
    connect(remL, &QPushButton::clicked, this, [=] {
        const int r = list->currentRow();
        if (r < 0)
            return;
        *current = -1;
        m_format.layouts.removeAt(r);
        delete list->takeItem(r);
    });
    addPage(tr("Section Layouts"), lw);
    if (list->count())
        list->setCurrentRow(0);

    // ------------------------------------------------------------ assignments
    auto *aw = new QWidget;
    auto *al = new QVBoxLayout(aw);
    auto *ainfo = new QLabel(tr("Which layout a section type uses by default in this format (by section type name; "
                                "projects can override this in the Compile window). \"*\" matches every type."),
                             aw);
    ainfo->setWordWrap(true);
    al->addWidget(ainfo);
    auto *table = new QTableWidget(0, 2, aw);
    table->setObjectName(QStringLiteral("fmt_assignments"));
    table->setHorizontalHeaderLabels({tr("Section type name"), tr("Layout")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(0, 200);
    QStringList names;
    for (auto it = m_format.defaultAssignments.cbegin(); it != m_format.defaultAssignments.cend(); ++it)
        names << it.key();
    if (p)
        for (const SectionType &st : p->sectionTypes)
            if (!names.contains(st.name.toLower()))
                names << st.name.toLower();
    for (const QString &n : names) {
        const int r = table->rowCount();
        table->insertRow(r);
        table->setItem(r, 0, new QTableWidgetItem(n));
        table->setItem(r, 1, new QTableWidgetItem(m_format.defaultAssignments.value(n)));
    }
    al->addWidget(table, 1);
    addPage(tr("Assignments"), aw);

    // ------------------------------------------------------------ styles
    auto *sw = new QWidget;
    auto *sl = new QHBoxLayout(sw);
    auto *sleft = new QVBoxLayout;
    auto *sinfo = new QLabel(tr("How the styles of the text look in this format. Styles not in the list keep their look "
                                "(own styles) or get the format's (headings, title, block quote ...)."),
                             sw);
    sinfo->setWordWrap(true);
    sleft->addWidget(sinfo);
    auto *slist = new QListWidget(sw);
    slist->setObjectName(QStringLiteral("fmt_styleOverrides"));
    sleft->addWidget(slist, 1);
    auto *sb = new QHBoxLayout;
    auto *addS = new QComboBox(sw);
    addS->setObjectName(QStringLiteral("fmt_addStyleOverride"));
    addS->addItem(tr("Add a style…"));
    {
        QStringList styleNames;
        if (p)
            for (const Style &st : styles::sheet(p->formatting).styles)
                styleNames << st.name;
        for (const QString &n : {QStringLiteral("Title"), QStringLiteral("Subtitle"), QStringLiteral("Heading 1"),
                                 QStringLiteral("Heading 2"), QStringLiteral("Heading 3"), QStringLiteral("Block Quote"),
                                 QStringLiteral("Centered"), QStringLiteral("Caption")})
            if (!styleNames.contains(n))
                styleNames << n;
        addS->addItems(styleNames);
    }
    auto *remS = new QPushButton(tr("Remove"), sw);
    remS->setObjectName(QStringLiteral("fmt_removeStyleOverride"));
    sb->addWidget(addS, 1);
    sb->addWidget(remS);
    sleft->addLayout(sb);
    sl->addLayout(sleft, 1);
    auto *sform = new QFormLayout;
    auto *smode = new QComboBox(sw);
    smode->setObjectName(QStringLiteral("fmt_styleMode"));
    smode->addItems({tr("Keep its look"), tr("Change its look"), tr("Make it body text"), tr("Leave the text out")});
    auto *sfont = new QComboBox(sw);
    sfont->setObjectName(QStringLiteral("fmt_styleFont"));
    sfont->setEditable(true);
    sfont->addItem(tr("unchanged"), QString());
    {
        QFontComboBox fc;
        for (int i = 0; i < fc.count(); ++i)
            sfont->addItem(fc.itemText(i), fc.itemText(i));
    }
    auto *ssize = spin(sw, 0, 72, 0.5, tr(" pt"), QStringLiteral("fmt_styleSize"));
    ssize->setSpecialValueText(tr("unchanged"));
    auto *sbold = tri(sw, tr("Bold"), -1, QStringLiteral("fmt_styleBold"));
    auto *sital = tri(sw, tr("Italic"), -1, QStringLiteral("fmt_styleItalic"));
    auto *sunder = tri(sw, tr("Underline"), -1, QStringLiteral("fmt_styleUnderline"));
    auto *scaps = new QComboBox(sw);
    scaps->addItems({tr("unchanged"), tr("as typed"), tr("small capitals"), tr("all capitals")});
    auto *scolor = new QPushButton(tr("unchanged"), sw);
    scolor->setObjectName(QStringLiteral("fmt_styleColor"));
    auto colorValue = std::make_shared<QColor>();
    auto *salign = new QComboBox(sw);
    salign->setObjectName(QStringLiteral("fmt_styleAlign"));
    salign->addItems({tr("unchanged"), tr("left"), tr("centered"), tr("right"), tr("justified")});
    auto *sleftI = spin(sw, -1, 100, 0.5, tr(" mm"), QStringLiteral("fmt_styleLeft"));
    sleftI->setSpecialValueText(tr("unchanged"));
    auto *srightI = spin(sw, -1, 100, 0.5, tr(" mm"), QStringLiteral("fmt_styleRight"));
    srightI->setSpecialValueText(tr("unchanged"));
    auto *sfirst = spin(sw, -1000, 100, 0.5, tr(" mm"), QStringLiteral("fmt_styleFirst"));
    sfirst->setSpecialValueText(tr("unchanged"));
    // one step up from "unchanged" gives 0 mm, not -999.5 mm (negative values are hanging indents)
    sfirst->setProperty("qbLast", -1000.0);
    connect(sfirst, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [sfirst](double v) {
        const double last = sfirst->property("qbLast").toDouble();
        if (last <= -999 && v > -1000 && v < -900) {
            sfirst->setProperty("qbLast", 0.0);
            sfirst->setValue(0);
            return;
        }
        sfirst->setProperty("qbLast", v);
    });
    auto *sbefore = spin(sw, -1, 100, 1, tr(" pt"), QStringLiteral("fmt_styleBefore"));
    sbefore->setSpecialValueText(tr("unchanged"));
    auto *safter = spin(sw, -1, 100, 1, tr(" pt"), QStringLiteral("fmt_styleAfter"));
    safter->setSpecialValueText(tr("unchanged"));
    auto *sline = spin(sw, 0, 3, 0.05, QString(), QStringLiteral("fmt_styleLine"), 2);
    sline->setSpecialValueText(tr("unchanged"));
    auto *sprefix = new QLineEdit(sw);
    sprefix->setObjectName(QStringLiteral("fmt_stylePrefix"));
    auto *ssuffix = new QLineEdit(sw);
    ssuffix->setObjectName(QStringLiteral("fmt_styleSuffix"));
    auto *supper = new QCheckBox(tr("Upper case"), sw);
    auto *sbreak = new QCheckBox(tr("Start on a new page"), sw);
    sform->addRow(tr("In this format:"), smode);
    sform->addRow(tr("Font:"), sfont);
    sform->addRow(tr("Size:"), ssize);
    auto *styleRow = new QHBoxLayout;
    styleRow->addWidget(sbold);
    styleRow->addWidget(sital);
    styleRow->addWidget(sunder);
    sform->addRow(QString(), styleRow);
    sform->addRow(tr("Capitals:"), scaps);
    sform->addRow(tr("Color:"), scolor);
    sform->addRow(tr("Alignment:"), salign);
    sform->addRow(tr("Left indent:"), sleftI);
    sform->addRow(tr("Right indent:"), srightI);
    sform->addRow(tr("First line:"), sfirst);
    sform->addRow(tr("Space before:"), sbefore);
    sform->addRow(tr("Space after:"), safter);
    sform->addRow(tr("Line spacing:"), sline);
    sform->addRow(tr("Text before:"), sprefix);
    sform->addRow(tr("Text after:"), ssuffix);
    sform->addRow(QString(), supper);
    sform->addRow(QString(), sbreak);
    sl->addLayout(sform, 2);
    auto scur = std::make_shared<int>(-1);
    auto sstore = [=]() {
        const int i = *scur;
        if (i < 0 || i >= m_format.styleOverrides.size())
            return;
        StyleOverride &o = m_format.styleOverrides[i];
        o.mode = StyleOverride::Mode(smode->currentIndex());
        o.fontFamily = sfont->currentIndex() > 0 ? sfont->currentText() : QString();
        o.fontSize = ssize->value();
        o.bold = triValue(sbold);
        o.italic = triValue(sital);
        o.underline = triValue(sunder);
        o.capitals = scaps->currentIndex() - 1;
        o.color = *colorValue;
        o.align = salign->currentIndex() - 1;
        o.leftIndentMm = sleftI->value() <= -1 ? -1 : sleftI->value();
        o.rightIndentMm = srightI->value() <= -1 ? -1 : srightI->value();
        o.firstLineMm = sfirst->value() <= -999 ? -1000 : sfirst->value();
        o.spaceBeforePt = sbefore->value() <= -1 ? -1 : sbefore->value();
        o.spaceAfterPt = safter->value() <= -1 ? -1 : safter->value();
        o.lineSpacing = sline->value();
        o.prefix = sprefix->text();
        o.suffix = ssuffix->text();
        o.uppercase = supper->isChecked();
        o.pageBreakBefore = sbreak->isChecked();
    };
    auto senable = [=]() {
        const bool look = smode->currentIndex() == StyleOverride::Restyle;
        for (QWidget *w : QList<QWidget *>{sfont, ssize, sbold, sital, sunder, scaps, scolor, salign, sleftI, srightI, sfirst,
                                           sbefore, safter, sline, sprefix, ssuffix, supper, sbreak})
            w->setEnabled(look);
    };
    auto sload = [=](int i) {
        *scur = i;
        if (i < 0 || i >= m_format.styleOverrides.size()) {
            senable();
            return;
        }
        const StyleOverride &o = m_format.styleOverrides[i];
        smode->setCurrentIndex(int(o.mode));
        const int fi = sfont->findText(o.fontFamily);
        if (o.fontFamily.isEmpty())
            sfont->setCurrentIndex(0);
        else if (fi >= 0)
            sfont->setCurrentIndex(fi);
        else
            sfont->setEditText(o.fontFamily);
        ssize->setValue(o.fontSize);
        sbold->setCheckState(o.bold < 0 ? Qt::PartiallyChecked : o.bold ? Qt::Checked : Qt::Unchecked);
        sital->setCheckState(o.italic < 0 ? Qt::PartiallyChecked : o.italic ? Qt::Checked : Qt::Unchecked);
        sunder->setCheckState(o.underline < 0 ? Qt::PartiallyChecked : o.underline ? Qt::Checked : Qt::Unchecked);
        scaps->setCurrentIndex(o.capitals + 1);
        *colorValue = o.color;
        scolor->setText(o.color.isValid() ? o.color.name() : tr("unchanged"));
        salign->setCurrentIndex(o.align + 1);
        sleftI->setValue(o.leftIndentMm);
        srightI->setValue(o.rightIndentMm);
        sfirst->setValue(o.firstLineMm);
        sbefore->setValue(o.spaceBeforePt);
        safter->setValue(o.spaceAfterPt);
        sline->setValue(o.lineSpacing);
        sprefix->setText(o.prefix);
        ssuffix->setText(o.suffix);
        supper->setChecked(o.uppercase);
        sbreak->setChecked(o.pageBreakBefore);
        senable();
    };
    for (const StyleOverride &o : m_format.styleOverrides)
        slist->addItem(o.style);
    connect(slist, &QListWidget::currentRowChanged, this, [=](int r) {
        sstore();
        sload(r);
    });
    connect(smode, qOverload<int>(&QComboBox::currentIndexChanged), this, senable);
    connect(addS, qOverload<int>(&QComboBox::activated), this, [=](int i) {
        if (i <= 0)
            return;
        const QString n = addS->itemText(i);
        addS->setCurrentIndex(0);
        for (int k = 0; k < m_format.styleOverrides.size(); ++k)
            if (m_format.styleOverrides[k].style == n) {
                slist->setCurrentRow(k);
                return;
            }
        sstore();
        StyleOverride o;
        o.style = n;
        m_format.styleOverrides << o;
        slist->addItem(n);
        slist->setCurrentRow(slist->count() - 1);
    });
    connect(remS, &QPushButton::clicked, this, [=] {
        const int r = slist->currentRow();
        if (r < 0)
            return;
        *scur = -1;
        m_format.styleOverrides.removeAt(r);
        delete slist->takeItem(r);
        sload(slist->currentRow());
    });
    connect(scolor, &QPushButton::clicked, this, [=] {
        const QColor c = QColorDialog::getColor(colorValue->isValid() ? *colorValue : QColor(Qt::black), this, tr("Color"));
        *colorValue = c;
        scolor->setText(c.isValid() ? c.name() : tr("unchanged"));
    });
    addPage(tr("Styles"), sw);
    if (slist->count())
        slist->setCurrentRow(0);
    else
        senable();

    // ------------------------------------------------------------ page
    auto *pw = new QWidget;
    auto *pf = new QFormLayout(pw);
    auto *paper = new QComboBox(pw);
    paper->setObjectName(QStringLiteral("fmt_paper"));
    paper->addItem(tr("Custom"));
    for (const PaperSize &ps : paperSizes())
        paper->addItem(ps.name);
    auto *pwid = spin(pw, 50, 600, 0.5, tr(" mm"), QStringLiteral("fmt_pageWidth"));
    auto *phei = spin(pw, 50, 600, 0.5, tr(" mm"), QStringLiteral("fmt_pageHeight"));
    pwid->setValue(f.page.widthMm);
    phei->setValue(f.page.heightMm);
    // the paper list sets width and height; a size typed in shows its name or "Custom"
    // (the list did nothing before; found by looking at the page in the 0.14.0 checks)
    auto matchPaper = [paper, pwid, phei] {
        int idx = 0;
        const QList<PaperSize> sizes = paperSizes();
        for (int i = 0; i < sizes.size(); ++i)
            if (qAbs(sizes[i].w - pwid->value()) < 0.3 && qAbs(sizes[i].h - phei->value()) < 0.3)
                idx = i + 1;
        QSignalBlocker block(paper);
        paper->setCurrentIndex(idx);
    };
    matchPaper();
    connect(paper, qOverload<int>(&QComboBox::activated), pw, [pwid, phei](int index) {
        const QList<PaperSize> sizes = paperSizes();
        if (index < 1 || index > sizes.size())
            return;
        pwid->setValue(sizes[index - 1].w);
        phei->setValue(sizes[index - 1].h);
    });
    connect(pwid, qOverload<double>(&QDoubleSpinBox::valueChanged), pw, matchPaper);
    connect(phei, qOverload<double>(&QDoubleSpinBox::valueChanged), pw, matchPaper);
    auto *mt = spin(pw, 0, 100, 0.5, tr(" mm"), QStringLiteral("fmt_marginTop"));
    auto *mb = spin(pw, 0, 100, 0.5, tr(" mm"), QStringLiteral("fmt_marginBottom"));
    auto *ml = spin(pw, 0, 100, 0.5, tr(" mm"), QStringLiteral("fmt_marginLeft"));
    auto *mr = spin(pw, 0, 100, 0.5, tr(" mm"), QStringLiteral("fmt_marginRight"));
    mt->setValue(f.page.marginTopMm);
    mb->setValue(f.page.marginBottomMm);
    ml->setValue(f.page.marginLeftMm);
    mr->setValue(f.page.marginRightMm);
    auto *mirror = new QCheckBox(tr("Mirror margins (books: inside and outside)"), pw);
    mirror->setObjectName(QStringLiteral("fmt_mirror"));
    mirror->setChecked(f.page.mirrorMargins);
    auto *hl = new QLineEdit(f.page.headerLeft, pw);
    hl->setObjectName(QStringLiteral("fmt_headerLeft"));
    auto *hc = new QLineEdit(f.page.headerCenter, pw);
    hc->setObjectName(QStringLiteral("fmt_headerCenter"));
    auto *hr = new QLineEdit(f.page.headerRight, pw);
    hr->setObjectName(QStringLiteral("fmt_headerRight"));
    auto *fl = new QLineEdit(f.page.footerLeft, pw);
    fl->setObjectName(QStringLiteral("fmt_footerLeft"));
    auto *fc = new QLineEdit(f.page.footerCenter, pw);
    fc->setObjectName(QStringLiteral("fmt_footerCenter"));
    auto *fr = new QLineEdit(f.page.footerRight, pw);
    fr->setObjectName(QStringLiteral("fmt_footerRight"));
    auto *facing = new QCheckBox(tr("Facing pages: left texts on left-hand pages, right texts on right-hand pages"), pw);
    facing->setObjectName(QStringLiteral("fmt_facingHeaders"));
    facing->setChecked(f.page.mirrorHeaders);
    auto *hfirst = new QCheckBox(tr("Header and footer on the first page"), pw);
    hfirst->setObjectName(QStringLiteral("fmt_headerFirst"));
    hfirst->setChecked(f.page.headerOnFirstPage);
    auto *hchap = new QCheckBox(tr("Header and footer on pages where a chapter starts (PDF)"), pw);
    hchap->setObjectName(QStringLiteral("fmt_headerChapter"));
    hchap->setChecked(f.page.headerOnChapterPages);
    auto *recto = new QCheckBox(tr("Chapters start on a right-hand page (PDF)"), pw);
    recto->setObjectName(QStringLiteral("fmt_rightPage"));
    recto->setChecked(f.page.chaptersOnRightPage);
    auto *pstart = new QSpinBox(pw);
    pstart->setObjectName(QStringLiteral("fmt_firstPageNumber"));
    pstart->setRange(0, 9999);
    pstart->setValue(f.page.firstPageNumber);
    auto *pstyle = new QComboBox(pw);
    pstyle->setObjectName(QStringLiteral("fmt_pageNumberStyle"));
    pstyle->addItems({QStringLiteral("1, 2, 3"), QStringLiteral("i, ii, iii"), QStringLiteral("I, II, III")});
    pstyle->setCurrentIndex(qBound(0, f.page.pageNumberStyle, 2));
    auto *hsize = spin(pw, 0, 24, 0.5, tr(" pt"), QStringLiteral("fmt_headerSize"));
    hsize->setSpecialValueText(tr("text size − 2 pt"));
    hsize->setValue(f.page.headerFontSize);
    pf->addRow(tr("Paper:"), paper);
    pf->addRow(tr("Width:"), pwid);
    pf->addRow(tr("Height:"), phei);
    pf->addRow(tr("Top margin:"), mt);
    pf->addRow(tr("Bottom margin:"), mb);
    pf->addRow(tr("Left / inside margin:"), ml);
    pf->addRow(tr("Right / outside margin:"), mr);
    pf->addRow(QString(), mirror);
    pf->addRow(tr("Header left:"), hl);
    pf->addRow(tr("Header center:"), hc);
    pf->addRow(tr("Header right:"), hr);
    pf->addRow(tr("Footer left:"), fl);
    pf->addRow(tr("Footer center:"), fc);
    pf->addRow(tr("Footer right:"), fr);
    pf->addRow(QString(), facing);
    pf->addRow(QString(), hfirst);
    pf->addRow(QString(), hchap);
    pf->addRow(QString(), recto);
    pf->addRow(tr("First page number:"), pstart);
    pf->addRow(tr("Page numbers:"), pstyle);
    pf->addRow(tr("Header and footer size:"), hsize);
    auto *phint = new QLabel(tr("<$p> = page number, <$pages> = number of pages (PDF), <$author>, <$surname>, <$shorttitle>, "
                                "<$projecttitle>, <$date>"),
                             pw);
    phint->setWordWrap(true);
    phint->setEnabled(false);
    pf->addRow(QString(), phint);
    connect(paper, qOverload<int>(&QComboBox::activated), this, [=](int i) {
        if (i <= 0)
            return;
        const PaperSize ps = paperSizes().at(i - 1);
        pwid->setValue(ps.w);
        phei->setValue(ps.h);
    });
    addPage(tr("Page"), pw);

    // ------------------------------------------------------------ footnotes and comments
    auto *nw = new QWidget;
    auto *nf = new QFormLayout(nw);
    auto *place = new QComboBox(nw);
    place->setObjectName(QStringLiteral("fmt_footnotePlacement"));
    place->addItems({tr("At the bottom of the page (e-books: pop-up notes)"), tr("At the end of each chapter"),
                     tr("At the end of the text")});
    place->setCurrentIndex(qBound(0, f.footnotePlacement, 2));
    auto *ntitle = new QLineEdit(f.endnotesTitle, nw);
    ntitle->setObjectName(QStringLiteral("fmt_endnotesTitle"));
    auto *nsize = spin(nw, 0, 24, 0.5, tr(" pt"), QStringLiteral("fmt_footnoteSize"));
    nsize->setSpecialValueText(tr("text size − 2 pt"));
    nsize->setValue(f.footnoteSize);
    auto *annot = new QCheckBox(tr("Export inline annotations as comments (otherwise they are removed)"), nw);
    annot->setObjectName(QStringLiteral("fmt_annotations"));
    annot->setChecked(f.keepAnnotationsAsComments);
    auto *cmts = new QCheckBox(tr("Export linked comments as document comments (Word)"), nw);
    cmts->setObjectName(QStringLiteral("fmt_comments"));
    cmts->setChecked(f.keepComments);
    auto *hl2 = new QCheckBox(tr("Remove text highlights"), nw);
    hl2->setObjectName(QStringLiteral("fmt_removeHighlights"));
    hl2->setChecked(f.removeHighlights);
    nf->addRow(tr("Footnotes:"), place);
    nf->addRow(tr("Title of the notes:"), ntitle);
    nf->addRow(tr("Size of footnotes:"), nsize);
    auto *nhint = new QLabel(tr("Word files always have their footnotes at the bottom of the page (Word can turn them into "
                                "endnotes)."),
                             nw);
    nhint->setWordWrap(true);
    nhint->setEnabled(false);
    nf->addRow(QString(), nhint);
    nf->addRow(annot);
    nf->addRow(cmts);
    nf->addRow(hl2);
    addPage(tr("Footnotes and Comments"), nw);

    // ------------------------------------------------------------ replacements
    auto *rw = new QWidget;
    auto *rl = new QVBoxLayout(rw);
    auto *rinfo = new QLabel(tr("Replaced in the compiled text only; the project stays as it is. With \"Regular expression\", "
                                "\\1 … \\9 in \"With\" stand for the parts in brackets."),
                             rw);
    rinfo->setWordWrap(true);
    rl->addWidget(rinfo);
    auto *rtable = new QTableWidget(0, 6, rw);
    rtable->setObjectName(QStringLiteral("fmt_replacements"));
    rtable->setHorizontalHeaderLabels({tr("On"), tr("Replace"), tr("With"), tr("Case"), tr("Whole words"), tr("Regular expression")});
    rtable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    rtable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    rtable->verticalHeader()->hide();
    auto checkItem = [](bool on) {
        auto *it = new QTableWidgetItem;
        it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        return it;
    };
    auto addRow = [=](const Replacement &r) {
        const int row = rtable->rowCount();
        rtable->insertRow(row);
        rtable->setItem(row, 0, checkItem(r.enabled));
        rtable->setItem(row, 1, new QTableWidgetItem(r.find));
        rtable->setItem(row, 2, new QTableWidgetItem(r.replace));
        rtable->setItem(row, 3, checkItem(r.matchCase));
        rtable->setItem(row, 4, checkItem(r.wholeWords));
        rtable->setItem(row, 5, checkItem(r.regex));
    };
    for (const Replacement &r : f.replacements)
        addRow(r);
    rl->addWidget(rtable, 1);
    auto *rb = new QHBoxLayout;
    auto *addR = new QPushButton(tr("Add"), rw);
    addR->setObjectName(QStringLiteral("fmt_addReplacement"));
    auto *remR = new QPushButton(tr("Remove"), rw);
    remR->setObjectName(QStringLiteral("fmt_removeReplacement"));
    rb->addWidget(addR);
    rb->addWidget(remR);
    rb->addStretch(1);
    rl->addLayout(rb);
    // Remove acts on the selected row; without one it is off (it did nothing before; 0.14.0 checks)
    auto updateRemR = [rtable, remR] { remR->setEnabled(rtable->currentRow() >= 0 && !rtable->selectedItems().isEmpty()); };
    connect(rtable, &QTableWidget::itemSelectionChanged, this, updateRemR);
    updateRemR();
    connect(addR, &QPushButton::clicked, this, [=] {
        Replacement r;
        addRow(r);
        rtable->setCurrentCell(rtable->rowCount() - 1, 1);
        rtable->editItem(rtable->item(rtable->rowCount() - 1, 1));
    });
    connect(remR, &QPushButton::clicked, this, [=] {
        if (rtable->currentRow() >= 0)
            rtable->removeRow(rtable->currentRow());
    });
    addPage(tr("Replacements"), rw);

    // ------------------------------------------------------------ transformations
    auto *tw = new QWidget;
    auto *tl = new QVBoxLayout(tw);
    auto tbox = [&](const QString &text, bool on, const QString &objName) {
        auto *c = new QCheckBox(text, tw);
        c->setChecked(on);
        c->setObjectName(objName);
        tl->addWidget(c);
        return c;
    };
    auto *tstraight = tbox(tr("Straight quotes (“ ” ‘ ’ become \" ')"), f.transform.straightQuotes, QStringLiteral("fmt_straightQuotes"));
    auto *tsmart = tbox(tr("Typographic quotes (\" ' become “ ” ‘ ’, English)"), f.transform.smartQuotes, QStringLiteral("fmt_smartQuotes"));
    auto *tdash = tbox(tr("Dashes as hyphens (— becomes --, – becomes -)"), f.transform.dashesToHyphens, QStringLiteral("fmt_dashes"));
    auto *tital = tbox(tr("Italics as underlining (manuscript convention)"), f.transform.italicsToUnderline,
                       QStringLiteral("fmt_italicsToUnderline"));
    auto *tspace = tbox(tr("Several spaces become one"), f.transform.collapseSpaces, QStringLiteral("fmt_collapseSpaces"));
    auto *ttrail = tbox(tr("Remove spaces at the end of paragraphs"), f.transform.removeTrailingSpaces, QStringLiteral("fmt_trailingSpaces"));
    auto *tplain = tbox(tr("Plain text: no bold, italics, fonts or colors"), f.transform.plainText, QStringLiteral("fmt_plainText"));
    tl->addStretch(1);
    addPage(tr("Transformations"), tw);

    // ------------------------------------------------------------ contents, PDF and e-book
    auto *cw = new QWidget;
    auto *cf = new QFormLayout(cw);
    auto *toc = new QCheckBox(tr("Include a table of contents (PDF, e-book, Word)"), cw);
    toc->setObjectName(QStringLiteral("fmt_toc"));
    toc->setChecked(f.toc);
    auto *tocTitle = new QLineEdit(f.tocTitle, cw);
    tocTitle->setObjectName(QStringLiteral("fmt_tocTitle"));
    auto *tocLevels = new QSpinBox(cw);
    tocLevels->setObjectName(QStringLiteral("fmt_tocLevels"));
    tocLevels->setRange(1, 6);
    tocLevels->setValue(f.tocLevels);
    auto *bookmarks = new QCheckBox(tr("PDF: bookmarks for the headings"), cw);
    bookmarks->setObjectName(QStringLiteral("fmt_pdfBookmarks"));
    bookmarks->setChecked(f.pdfBookmarks);
    auto *plinks = new QCheckBox(tr("PDF: web links can be clicked"), cw);
    plinks->setObjectName(QStringLiteral("fmt_pdfLinks"));
    plinks->setChecked(f.pdfLinks);
    auto *esplit = new QCheckBox(tr("E-book: a file per section that starts on a new page"), cw);
    esplit->setObjectName(QStringLiteral("fmt_epubSplit"));
    esplit->setChecked(f.splitAtPageBreaks);
    auto *epage = new QCheckBox(tr("E-book: the table of contents as a page of the book as well"), cw);
    epage->setObjectName(QStringLiteral("fmt_epubTocPage"));
    epage->setChecked(f.tocPage);
    cf->addRow(QString(), toc);
    cf->addRow(tr("Title:"), tocTitle);
    cf->addRow(tr("Levels:"), tocLevels);
    cf->addRow(QString(), bookmarks);
    cf->addRow(QString(), plinks);
    cf->addRow(QString(), esplit);
    cf->addRow(QString(), epage);
    auto *chint = new QLabel(tr("The table of contents lists the headings of the section layouts and the headings in the text. "
                                "E-books always have one in the reader's menu; title, author, cover and the like are set "
                                "under Compile > Metadata."),
                             cw);
    chint->setWordWrap(true);
    chint->setEnabled(false);
    cf->addRow(QString(), chint);
    addPage(tr("Contents, PDF and E-book"), cw);

    m_sections->setCurrentRow(0);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &FormatEditorDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_apply = [=]() {
        store();
        sstore();
        m_format.name = name->text().trimmed().isEmpty() ? tr("My Format") : name->text().trimmed();
        m_format.builtin = false;
        m_format.useEditorFormatting = editorFmt->isChecked();
        m_format.fontFamily = font->currentText();
        m_format.headingFontFamily = hfont->currentIndex() > 0 ? hfont->currentText() : QString();
        m_format.fontSize = size->value();
        m_format.lineSpacing = spacing->value();
        m_format.firstLineIndentMm = indent->value();
        m_format.paragraphSpacingPt = after->value();
        m_format.justify = justify->isChecked();
        m_format.headingScale = hscale->value();
        m_format.headingBold = hbold->isChecked();
        m_format.page.widthMm = pwid->value();
        m_format.page.heightMm = phei->value();
        m_format.page.marginTopMm = mt->value();
        m_format.page.marginBottomMm = mb->value();
        m_format.page.marginLeftMm = ml->value();
        m_format.page.marginRightMm = mr->value();
        m_format.page.mirrorMargins = mirror->isChecked();
        m_format.page.headerLeft = hl->text();
        m_format.page.headerCenter = hc->text();
        m_format.page.headerRight = hr->text();
        m_format.page.footerLeft = fl->text();
        m_format.page.footerCenter = fc->text();
        m_format.page.footerRight = fr->text();
        m_format.page.mirrorHeaders = facing->isChecked();
        m_format.page.headerOnFirstPage = hfirst->isChecked();
        m_format.page.headerOnChapterPages = hchap->isChecked();
        m_format.page.chaptersOnRightPage = recto->isChecked();
        m_format.page.firstPageNumber = pstart->value();
        m_format.page.pageNumberStyle = pstyle->currentIndex();
        m_format.page.headerFontSize = hsize->value();
        m_format.defaultAssignments.clear();
        for (int r = 0; r < table->rowCount(); ++r) {
            const QString k = table->item(r, 0) ? table->item(r, 0)->text().trimmed().toLower() : QString();
            const QString v = table->item(r, 1) ? table->item(r, 1)->text().trimmed() : QString();
            if (!k.isEmpty() && !v.isEmpty())
                m_format.defaultAssignments.insert(k, v);
        }
        m_format.footnotePlacement = place->currentIndex();
        m_format.endnotesTitle = ntitle->text();
        m_format.footnoteSize = nsize->value();
        m_format.keepAnnotationsAsComments = annot->isChecked();
        m_format.keepComments = cmts->isChecked();
        m_format.removeHighlights = hl2->isChecked();
        m_format.replacements.clear();
        for (int r = 0; r < rtable->rowCount(); ++r) {
            Replacement x;
            x.enabled = rtable->item(r, 0)->checkState() == Qt::Checked;
            x.find = rtable->item(r, 1) ? rtable->item(r, 1)->text() : QString();
            x.replace = rtable->item(r, 2) ? rtable->item(r, 2)->text() : QString();
            x.matchCase = rtable->item(r, 3)->checkState() == Qt::Checked;
            x.wholeWords = rtable->item(r, 4)->checkState() == Qt::Checked;
            x.regex = rtable->item(r, 5)->checkState() == Qt::Checked;
            if (!x.find.isEmpty())
                m_format.replacements << x;
        }
        m_format.transform.straightQuotes = tstraight->isChecked();
        m_format.transform.smartQuotes = tsmart->isChecked();
        m_format.transform.dashesToHyphens = tdash->isChecked();
        m_format.transform.italicsToUnderline = tital->isChecked();
        m_format.transform.collapseSpaces = tspace->isChecked();
        m_format.transform.removeTrailingSpaces = ttrail->isChecked();
        m_format.transform.plainText = tplain->isChecked();
        m_format.toc = toc->isChecked();
        m_format.tocTitle = tocTitle->text();
        m_format.tocLevels = tocLevels->value();
        m_format.pdfBookmarks = bookmarks->isChecked();
        m_format.pdfLinks = plinks->isChecked();
        m_format.splitAtPageBreaks = esplit->isChecked();
        m_format.tocPage = epage->isChecked();
    };
}

void FormatEditorDialog::showSection(int index) { m_sections->setCurrentRow(index); }

int FormatEditorDialog::sectionCount() const { return m_sections->count(); }

void FormatEditorDialog::accept()
{
    if (m_apply)
        m_apply();
    QDialog::accept();
}

} // namespace qb
