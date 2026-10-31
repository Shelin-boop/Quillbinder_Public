#include "ui/StyleDialogs.h"

#include "core/ItemIcons.h"
#include "text/DisplayFormat.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/FormatDialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace qb {

namespace {

const int kNameRole = Qt::UserRole;      // style name; empty = "No Style"
const int kHeaderRole = Qt::UserRole + 1; // section header rows

QString num(double v, int decimals = 1)
{
    QString s = QString::number(v, 'f', decimals);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    return s;
}

void fillNextCombo(QComboBox *combo, const Formatting &f, const QString &self, const QString &current)
{
    combo->clear();
    combo->addItem(QObject::tr("The same style"), QString());
    combo->addItem(QObject::tr("No Style (body text)"), styles::noStyle());
    for (const QString &n : styles::names(f))
        if (n.compare(self, Qt::CaseInsensitive) != 0)
            combo->addItem(n, n);
    const int i = combo->findData(current);
    combo->setCurrentIndex(i >= 0 ? i : 0);
}

QTextEdit *makePreview(QWidget *parent)
{
    auto *view = new QTextEdit(parent);
    view->setReadOnly(true);
    view->setFocusPolicy(Qt::NoFocus);
    view->setFixedHeight(130);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return view;
}

} // namespace

namespace styleui {

QString describe(const Style &s)
{
    QStringList font;
    if (!s.chars.family.isEmpty())
        font << s.chars.family;
    if (s.chars.size > 0)
        font << QObject::tr("%1 pt").arg(num(s.chars.size));
    else if (s.sizeAdjust > 0)
        font << QObject::tr("larger than the text (%1 %)").arg(qRound(styles::adjustedSize(100, s.sizeAdjust)));
    else if (s.sizeAdjust < 0)
        font << QObject::tr("smaller than the text (%1 %)").arg(qRound(styles::adjustedSize(100, s.sizeAdjust)));
    if (s.bold)
        font << QObject::tr("bold");
    if (s.chars.weight > 0)
        font << QObject::tr("weight %1").arg(s.chars.weight);
    if (s.italic)
        font << QObject::tr("italic");
    if (s.underline)
        font << QObject::tr("underlined");
    if (s.strike)
        font << QObject::tr("struck through");
    if (s.chars.caps == 1)
        font << QObject::tr("small capitals");
    else if (s.chars.caps == 2)
        font << QObject::tr("all capitals");
    if (s.chars.spacing > 0)
        font << QObject::tr("letter spacing %1 %").arg(s.chars.spacing);
    if (s.chars.color.isValid())
        font << QObject::tr("color %1").arg(s.chars.color.name());

    QStringList para;
    using namespace direct;
    if (!s.character) {
        if (s.para.bits & Align)
            para << ((s.para.align & Qt::AlignHCenter) ? QObject::tr("centered")
                     : (s.para.align & Qt::AlignRight) ? QObject::tr("right-aligned")
                     : (s.para.align & Qt::AlignJustify) ? QObject::tr("justified")
                                                         : QObject::tr("left-aligned"));
        if (s.para.bits & Left)
            para << QObject::tr("left indent %1").arg(units::format(s.para.left));
        if (s.para.bits & Right)
            para << QObject::tr("right indent %1").arg(units::format(s.para.right));
        if (s.para.bits & First)
            para << QObject::tr("first line %1").arg(units::format(s.para.first));
        if (s.para.bits & Before)
            para << QObject::tr("space before %1 pt").arg(num(pxToPt(s.para.before)));
        if (s.para.bits & After)
            para << QObject::tr("space after %1 pt").arg(num(pxToPt(s.para.after)));
        if (s.para.bits & Line) {
            if (s.para.lineType == QTextBlockFormat::ProportionalHeight)
                para << QObject::tr("line spacing %1").arg(num(s.para.line / 100.0, 2));
            else if (s.para.lineType == QTextBlockFormat::FixedHeight)
                para << QObject::tr("line spacing exactly %1 pt").arg(num(pxToPt(s.para.line)));
            else
                para << QObject::tr("line spacing at least %1 pt").arg(num(pxToPt(s.para.line)));
        }
        if ((s.para.bits & Tabs) && !s.para.tabs.isEmpty())
            para << (s.para.tabs.size() == 1 ? QObject::tr("1 tab stop") : QObject::tr("%1 tab stops").arg(s.para.tabs.size()));
    }
    QStringList lines;
    if (!font.isEmpty())
        lines << QObject::tr("Characters: %1").arg(font.join(QStringLiteral(", ")));
    if (!para.isEmpty())
        lines << QObject::tr("Paragraph: %1").arg(para.join(QStringLiteral(", ")));
    if (lines.isEmpty())
        lines << (s.character ? QObject::tr("No character formatting yet.")
                              : QObject::tr("Like body text: everything follows the project's formatting."));
    else if (!s.character)
        lines << QObject::tr("Everything else follows the project's formatting.");
    return lines.join(QLatin1Char('\n'));
}

void fillPreview(QTextEdit *view, const Style &s, const Formatting &f, const QString &sample)
{
    // the style as part of a sheet of its own, so the usual code formats it
    StyleSheet sheet = styles::sheet(f);
    const int idx = sheet.indexOf(s.name);
    Style shown = s;
    if (shown.name.isEmpty())
        shown.name = QStringLiteral("Preview");
    if (idx >= 0)
        sheet.styles[idx] = shown;
    else
        sheet.styles.append(shown);
    const Formatting pf = styles::withSheet(f, sheet);

    QTextDocument *doc = view->document();
    doc->clear();
    display::prepareDocument(doc, pf, 1.0);
    doc->setProperty("qbZoom", 1.0);
    const QString body = QObject::tr("The quick brown fox jumps over the lazy dog, and the text runs on so that "
                                     "indents and line spacing can be seen.");
    const QString text = sample.trimmed().isEmpty()
                             ? QObject::tr("Sample text in this style. It runs on for more than one line so that "
                                           "indents and line spacing can be seen.")
                             : sample.trimmed();
    QTextCursor c(doc);
    if (s.character) {
        c.setBlockFormat(display::normalBlockFormat(pf));
        c.insertText(QObject::tr("Body text with "), QTextCharFormat());
        QTextCharFormat cf;
        styles::applyChars(cf, shown);
        c.insertText(sample.trimmed().isEmpty() ? QObject::tr("words in the character style") : sample.trimmed().left(60), cf);
        c.insertText(QObject::tr(" inside a paragraph."), QTextCharFormat());
    } else {
        const QTextCharFormat cf = display::charFormatFor(pf, shown.name);
        c.setBlockFormat(display::blockFormatFor(pf, shown.name));
        c.setBlockCharFormat(cf);
        c.insertText(text.left(200), cf);
        c.insertBlock(display::normalBlockFormat(pf), QTextCharFormat());
        c.insertText(body, QTextCharFormat());
    }
    view->moveCursor(QTextCursor::Start);
}

QFont rowFont(const Style &s, const QFont &base)
{
    QFont f = base;
    if (s.bold)
        f.setBold(true);
    if (s.italic)
        f.setItalic(true);
    if (s.underline)
        f.setUnderline(true);
    if (s.strike)
        f.setStrikeOut(true);
    if (!s.chars.family.isEmpty())
        f.setFamilies(QStringList{s.chars.family});
    if (s.chars.caps == 1)
        f.setCapitalization(QFont::SmallCaps);
    else if (s.chars.caps == 2)
        f.setCapitalization(QFont::AllUppercase);
    return f;
}

} // namespace styleui

// ------------------------------------------------------------------ from selection

StyleFromSelectionDialog::StyleFromSelectionDialog(Mode mode, const Formatting &f, const QTextBlockFormat &bf,
                                                   const QTextCharFormat &cf, const QString &preferred, QWidget *parent)
    : QDialog(parent), m_mode(mode), m_f(f), m_bf(bf), m_cf(cf)
{
    setObjectName(QStringLiteral("styleFromSelectionDialog"));
    setWindowTitle(mode == New ? tr("New Style From Selection") : tr("Redefine Style From Selection"));
    auto *v = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    v->addLayout(form);
    if (mode == New) {
        m_name = new QLineEdit(this);
        m_name->setObjectName(QStringLiteral("styleName"));
        m_name->setMaxLength(60);
        m_name->setText(styles::sheet(f).uniqueName(tr("New Style")));
        m_name->selectAll();
        form->addRow(tr("&Name:"), m_name);
    } else {
        m_target = new QComboBox(this);
        m_target->setObjectName(QStringLiteral("styleTarget"));
        for (const Style &s : styles::sheet(f).styles)
            m_target->addItem(styles::marker(s) + s.name, s.name);
        const int i = m_target->findData(preferred);
        m_target->setCurrentIndex(i >= 0 ? i : 0);
        form->addRow(tr("&Style:"), m_target);
        connect(m_target, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { targetChanged(); });
    }

    auto *box = new QGroupBox(tr("Formatting to save"), this);
    auto *bv = new QVBoxLayout(box);
    m_all = new QRadioButton(tr("Save &all formatting (paragraph style with character formatting)"), box);
    m_all->setObjectName(QStringLiteral("styleKindAll"));
    m_paraOnly = new QRadioButton(tr("Save &paragraph style (indents, spacing, alignment only)"), box);
    m_paraOnly->setObjectName(QStringLiteral("styleKindParagraph"));
    m_charOnly = new QRadioButton(tr("Save &character attributes (character style)"), box);
    m_charOnly->setObjectName(QStringLiteral("styleKindCharacter"));
    bv->addWidget(m_all);
    bv->addWidget(m_paraOnly);
    bv->addWidget(m_charOnly);
    m_family = new QCheckBox(tr("Include the &font"), box);
    m_family->setObjectName(QStringLiteral("styleWithFamily"));
    m_size = new QCheckBox(tr("Include the font si&ze"), box);
    m_size->setObjectName(QStringLiteral("styleWithSize"));
    const direct::CharSpec spec = direct::charSpec(cf);
    m_family->setChecked(!spec.family.isEmpty());
    m_size->setChecked(spec.size > 0 || cf.hasProperty(QTextFormat::FontSizeAdjustment));
    bv->addSpacing(4);
    bv->addWidget(m_family);
    bv->addWidget(m_size);
    v->addWidget(box);

    auto *form2 = new QFormLayout;
    m_next = new QComboBox(this);
    m_next->setObjectName(QStringLiteral("styleNext"));
    m_nextLabel = new QLabel(tr("Paragraph after &Enter:"), this);
    m_nextLabel->setBuddy(m_next);
    form2->addRow(m_nextLabel, m_next);
    v->addLayout(form2);

    m_preview = makePreview(this);
    m_preview->setObjectName(QStringLiteral("stylePreview"));
    v->addWidget(m_preview);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("styleSummary"));
    m_summary->setWordWrap(true);
    v->addWidget(m_summary);
    if (mode == Redefine) {
        auto *note = new QLabel(tr("All text in the project that uses the style is updated."), this);
        note->setWordWrap(true);
        v->addWidget(note);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);

    m_all->setChecked(true);
    for (QRadioButton *r : {m_all, m_paraOnly, m_charOnly})
        connect(r, &QRadioButton::toggled, this, [this](bool on) {
            if (on)
                kindChanged();
        });
    connect(m_family, &QCheckBox::toggled, this, [this] { kindChanged(); });
    connect(m_size, &QCheckBox::toggled, this, [this] { kindChanged(); });
    if (m_name)
        connect(m_name, &QLineEdit::textChanged, this, [this] { kindChanged(); });
    if (mode == Redefine)
        targetChanged();
    else {
        fillNextCombo(m_next, m_f, QString(), QString());
        kindChanged();
    }
    resize(qMax(width(), 460), sizeHint().height());
}

void StyleFromSelectionDialog::targetChanged()
{
    const Style *old = styles::sheet(m_f).find(m_target->currentData().toString());
    if (!old)
        return;
    // a paragraph style stays a paragraph style, a character style a character style
    m_charOnly->setEnabled(old->character);
    m_all->setEnabled(!old->character);
    m_paraOnly->setEnabled(!old->character);
    if (old->character)
        m_charOnly->setChecked(true);
    else if (m_charOnly->isChecked())
        m_all->setChecked(true);
    fillNextCombo(m_next, m_f, old->name, old->next);
    kindChanged();
}

styles::CaptureOptions StyleFromSelectionDialog::options() const
{
    styles::CaptureOptions o;
    o.character = m_charOnly->isChecked();
    o.withChars = !m_paraOnly->isChecked();
    o.withFamily = m_family->isChecked();
    o.withSize = m_size->isChecked();
    return o;
}

void StyleFromSelectionDialog::kindChanged()
{
    const bool chars = !m_paraOnly->isChecked();
    m_family->setEnabled(chars);
    m_size->setEnabled(chars);
    m_next->setEnabled(!m_charOnly->isChecked());
    m_nextLabel->setEnabled(!m_charOnly->isChecked());
    const Style s = style();
    m_summary->setText(styleui::describe(s));
    styleui::fillPreview(m_preview, s, m_f);
}

Style StyleFromSelectionDialog::style() const
{
    const styles::CaptureOptions o = options();
    QTextCharFormat cf = m_cf;
    if (o.character) {
        // the look the paragraph style gives is not part of a character style
        if (const Style *ps = styles::find(m_f, m_bf.stringProperty(props::Style)))
            styles::stripChars(cf, *ps);
    }
    Style s = styles::capture(m_name ? m_name->text().trimmed() : m_target->currentData().toString(), m_bf, cf, m_f, o);
    if (m_mode == Redefine) {
        if (const Style *old = styles::sheet(m_f).find(s.name)) {
            s.name = old->name;
            s.builtin = old->builtin;
            s.headingLevel = old->headingLevel;
            s.keepWithNext = old->keepWithNext;
        }
    }
    if (!s.character)
        s.next = m_next->currentData().toString();
    return s;
}

void StyleFromSelectionDialog::accept()
{
    if (m_mode == New) {
        const QString name = m_name->text().trimmed();
        if (!styles::validName(name)) {
            QMessageBox::information(this, windowTitle(),
                                     tr("Please enter a name of up to 60 characters without : \" < > \\ { } ;"));
            return;
        }
        if (styles::sheet(m_f).find(name)) {
            QMessageBox::information(this, windowTitle(),
                                     tr("There is a style named “%1” already. Choose another name, or use "
                                        "Redefine Style From Selection to change that style.").arg(name));
            return;
        }
    }
    QDialog::accept();
}

// ------------------------------------------------------------------ edit

StyleEditDialog::StyleEditDialog(const Style &style, const Formatting &f, QWidget *parent)
    : QDialog(parent), m_style(style), m_originalName(style.name), m_f(f)
{
    setObjectName(QStringLiteral("styleEditDialog"));
    setWindowTitle(tr("Edit Style"));
    auto *v = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    v->addLayout(form);
    m_name = new QLineEdit(style.name, this);
    m_name->setObjectName(QStringLiteral("styleName"));
    m_name->setMaxLength(60);
    if (styles::isFixedName(style.name)) {
        m_name->setReadOnly(true);
        m_name->setToolTip(tr("This style has a fixed role (Markdown short form, compile formats) and keeps its name."));
    }
    form->addRow(tr("&Name:"), m_name);
    form->addRow(tr("Type:"), new QLabel(style.character ? tr("Character style") : tr("Paragraph style"), this));

    auto *row = new QHBoxLayout;
    auto *font = new QPushButton(tr("&Font…"), this);
    font->setObjectName(QStringLiteral("styleFont"));
    connect(font, &QPushButton::clicked, this, [this] { editFont(); });
    row->addWidget(font);
    auto *noFont = new QPushButton(tr("&Remove Character Formatting"), this);
    noFont->setObjectName(QStringLiteral("styleNoFont"));
    connect(noFont, &QPushButton::clicked, this, [this] {
        m_style.bold = m_style.italic = m_style.underline = m_style.strike = false;
        m_style.sizeAdjust = 0;
        m_style.chars = direct::CharSpec();
        refresh();
    });
    if (!style.character) {
        auto *para = new QPushButton(tr("&Paragraph…"), this);
        para->setObjectName(QStringLiteral("styleParagraph"));
        connect(para, &QPushButton::clicked, this, [this] { editParagraph(); });
        row->addWidget(para);
    }
    row->addWidget(noFont);
    row->addStretch(1);
    v->addLayout(row);

    if (!style.character) {
        auto *form2 = new QFormLayout;
        m_next = new QComboBox(this);
        m_next->setObjectName(QStringLiteral("styleNext"));
        fillNextCombo(m_next, f, style.name, style.next);
        form2->addRow(tr("Paragraph after &Enter:"), m_next);
        m_keep = new QCheckBox(tr("&Keep on one page with the next paragraph (when compiled)"), this);
        m_keep->setChecked(style.keepWithNext);
        form2->addRow(QString(), m_keep);
        v->addLayout(form2);
    }

    m_preview = makePreview(this);
    m_preview->setObjectName(QStringLiteral("stylePreview"));
    v->addWidget(m_preview);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("styleSummary"));
    m_summary->setWordWrap(true);
    v->addWidget(m_summary);
    auto *note = new QLabel(tr("Apply and OK update all text in the project that uses the style, also where its old font "
                               "or size was set by hand."),
                            this);
    note->setWordWrap(true);
    v->addWidget(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    buttons->setObjectName(QStringLiteral("styleEditButtons"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
        if (takeInput())
            emit applyRequested();
    });
    v->addWidget(buttons);
    refresh();
    resize(qMax(width(), 480), sizeHint().height());
}

void StyleEditDialog::refresh()
{
    m_summary->setText(styleui::describe(m_style));
    styleui::fillPreview(m_preview, m_style, m_f);
}

void StyleEditDialog::editFont()
{
    FontChoice cur;
    cur.spec = m_style.chars;
    cur.bold = m_style.bold;
    cur.italic = m_style.italic;
    cur.underline = m_style.underline;
    cur.strike = m_style.strike;
    if (cur.spec.size <= 0 && m_style.sizeAdjust != 0)
        cur.spec.size = styles::adjustedSize(m_f.fontSize, m_style.sizeAdjust);
    const double shownSize = cur.spec.size;
    FontDialog dlg(cur, m_f, m_style.name, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const FontChoice ch = dlg.choice();
    const double oldSize = m_style.chars.size;
    m_style.chars = ch.spec;
    m_style.bold = ch.bold && ch.spec.weight == 0;
    m_style.italic = ch.italic;
    m_style.underline = ch.underline;
    m_style.strike = ch.strike;
    // a relative size (built-in headings) stays relative until the size is changed
    if (m_style.sizeAdjust != 0) {
        if (qAbs(ch.spec.size - shownSize) < 0.01)
            m_style.chars.size = oldSize;
        else
            m_style.sizeAdjust = 0;
    }
    refresh();
}

void StyleEditDialog::editParagraph()
{
    // what a paragraph of this style looks like now
    StyleSheet sheet = styles::sheet(m_f);
    const int idx = sheet.indexOf(m_originalName);
    Style shown = m_style;
    shown.name = m_originalName;
    if (idx >= 0)
        sheet.styles[idx] = shown;
    else
        sheet.styles.append(shown);
    const Formatting pf = styles::withSheet(m_f, sheet);
    direct::ParaSpec cur = direct::effectiveParaSpec(display::blockFormatFor(pf, shown.name));
    cur.bits = m_style.para.bits;
    ParagraphDialog dlg(cur, this);
    dlg.setWindowTitle(tr("Paragraph Formatting of the Style"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (dlg.resetRequested()) {
        m_style.para = direct::ParaSpec(); // follow the project in everything
    } else {
        const direct::ParaSpec r = dlg.result();
        using namespace direct;
        if (r.bits & Align)
            m_style.para.align = r.align;
        if (r.bits & Left)
            m_style.para.left = r.left;
        if (r.bits & Right)
            m_style.para.right = r.right;
        if (r.bits & First)
            m_style.para.first = r.first;
        if (r.bits & Before)
            m_style.para.before = r.before;
        if (r.bits & After)
            m_style.para.after = r.after;
        if (r.bits & Line) {
            m_style.para.line = r.line;
            m_style.para.lineType = r.lineType;
        }
        if (r.bits & Tabs)
            m_style.para.tabs = r.tabs;
        m_style.para.bits |= r.bits;
    }
    refresh();
}

void StyleEditDialog::accept()
{
    if (takeInput())
        QDialog::accept();
}

void StyleEditDialog::applied(const Style &s, const Formatting &f)
{
    m_style = s;
    m_originalName = s.name;
    m_f = f;
    refresh();
}

bool StyleEditDialog::takeInput()
{
    const QString name = m_name->text().trimmed();
    if (name.compare(m_originalName, Qt::CaseInsensitive) != 0) {
        if (!styles::validName(name)) {
            QMessageBox::information(this, windowTitle(),
                                     tr("Please enter a name of up to 60 characters without : \" < > \\ { } ;"));
            return false;
        }
        if (styles::sheet(m_f).indexOf(name) >= 0) {
            QMessageBox::information(this, windowTitle(), tr("There is a style named “%1” already.").arg(name));
            return false;
        }
    }
    m_style.name = name;
    if (m_next)
        m_style.next = m_next->currentData().toString();
    if (m_keep)
        m_style.keepWithNext = m_keep->isChecked();
    return true;
}

// ------------------------------------------------------------------ panel

StylesPanel::StylesPanel(QWidget *parent) : QDialog(parent, Qt::Tool)
{
    setObjectName(QStringLiteral("stylesPanel"));
    setWindowTitle(tr("Styles"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("stylesList"));
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setAlternatingRowColors(false);
    m_list->setUniformItemSizes(false);
    v->addWidget(m_list, 1);

    auto *hint = new QLabel(tr("Double-click applies a style to the text. “¶” paragraph style, “¶a” with character "
                               "formatting, “a” character style."), this);
    hint->setWordWrap(true);
    QFont small = hint->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    hint->setFont(small);
    v->addWidget(hint);

    auto *row = new QHBoxLayout;
    auto tool = [&](const char *name, const QString &icon, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setObjectName(QLatin1String(name));
        b->setIcon(icons::action(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        row->addWidget(b);
        return b;
    };
    m_new = tool("stylesNew", QStringLiteral("list-add"), tr("New Style From Selection…"));
    m_redefine = tool("stylesRedefine", QStringLiteral("view-refresh"), tr("Redefine the Style From the Selection…"));
    m_edit = tool("stylesEdit", QStringLiteral("document-edit"), tr("Edit Style…"));
    m_delete = tool("stylesDelete", QStringLiteral("list-remove"), tr("Delete Style"));
    m_up = tool("stylesUp", QStringLiteral("go-up"), tr("Move Up in the List"));
    m_down = tool("stylesDown", QStringLiteral("go-down"), tr("Move Down in the List"));
    m_more = tool("stylesMore", QStringLiteral("configure"), tr("More"));
    m_more->setPopupMode(QToolButton::InstantPopup);
    auto *more = new QMenu(m_more);
    more->addAction(tr("Reset to the Built-in Definition"), this, [this] { emit resetRequested(selectedStyle()); })
        ->setObjectName(QStringLiteral("stylesReset"));
    more->addSeparator();
    more->addAction(tr("Import Styles From Another Project…"), this, [this] { emit importRequested(); });
    more->addAction(tr("Use These Styles in New Projects"), this, [this] { emit saveAsDefaultRequested(); });
    m_more->setMenu(more);
    connect(more, &QMenu::aboutToShow, this, [this, more] {
        const QString n = selectedStyle();
        const Style *s = styles::sheet(m_f).find(n);
        const Style *def = styles::builtinDefault(n);
        more->actions().first()->setEnabled(m_hasProject && s && def && !s->sameFormatting(*def));
        for (int i = 2; i < more->actions().size(); ++i)
            more->actions().at(i)->setEnabled(m_hasProject);
    });
    row->addStretch(1);
    m_apply = new QPushButton(tr("&Apply"), this);
    m_apply->setObjectName(QStringLiteral("stylesApply"));
    row->addWidget(m_apply);
    v->addLayout(row);

    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        if (it && !it->data(kHeaderRole).toBool())
            emit applyRequested(it->data(kNameRole).toString());
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &StylesPanel::showMenu);
    connect(m_apply, &QPushButton::clicked, this, [this] {
        QListWidgetItem *it = m_list->currentItem();
        if (it && !it->data(kHeaderRole).toBool())
            emit applyRequested(it->data(kNameRole).toString());
    });
    connect(m_new, &QToolButton::clicked, this, [this] { emit newRequested(); });
    connect(m_redefine, &QToolButton::clicked, this, [this] {
        if (noStyleSelected())
            emit redefineNoStyleRequested();
        else
            emit redefineRequested(selectedStyle());
    });
    connect(m_edit, &QToolButton::clicked, this, [this] { emit editRequested(selectedStyle()); });
    connect(m_delete, &QToolButton::clicked, this, [this] { emit deleteRequested(selectedStyle()); });
    connect(m_up, &QToolButton::clicked, this, [this] { emit moveRequested(selectedStyle(), -1); });
    connect(m_down, &QToolButton::clicked, this, [this] { emit moveRequested(selectedStyle(), 1); });
    resize(290, 470);
    updateButtons();
}

bool StylesPanel::noStyleSelected() const
{
    const QListWidgetItem *it = m_list->currentItem();
    return it && !it->data(kHeaderRole).toBool() && it->data(kNameRole).toString().isEmpty();
}

QString StylesPanel::selectedStyle() const
{
    const QListWidgetItem *it = m_list->currentItem();
    if (!it || it->data(kHeaderRole).toBool())
        return QString();
    return it->data(kNameRole).toString();
}

void StylesPanel::setStyles(const Formatting &f, bool hasProject)
{
    const QString keep = selectedStyle();
    m_f = f;
    m_hasProject = hasProject;
    m_list->clear();
    auto header = [&](const QString &text) {
        auto *h = new QListWidgetItem(text, m_list);
        h->setData(kHeaderRole, true);
        h->setFlags(Qt::NoItemFlags);
        QFont hf = m_list->font();
        hf.setPointSizeF(hf.pointSizeF() * 0.85);
        hf.setCapitalization(QFont::AllUppercase);
        h->setFont(hf);
    };
    auto *none = new QListWidgetItem(tr("No Style"), m_list);
    none->setData(kNameRole, QString());
    none->setToolTip(tr("Body text: follows the project's formatting."));
    header(tr("Paragraph styles"));
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1)
            header(tr("Character styles"));
        for (const Style &s : styles::sheet(f).styles) {
            if (s.character != (pass == 1))
                continue;
            auto *it = new QListWidgetItem(styles::marker(s) + s.name, m_list);
            it->setData(kNameRole, s.name);
            it->setFont(styleui::rowFont(s, m_list->font()));
            it->setToolTip(styleui::describe(s));
            if (s.chars.color.isValid())
                it->setForeground(s.chars.color);
        }
    }
    for (int i = 0; i < m_list->count(); ++i)
        if (!keep.isEmpty() && m_list->item(i)->data(kNameRole).toString() == keep)
            m_list->setCurrentRow(i);
    setCurrent(m_curPara, m_curChar);
    updateButtons();
}

void StylesPanel::setCurrent(const QString &paragraph, const QString &character)
{
    m_curPara = paragraph;
    m_curChar = character;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *it = m_list->item(i);
        if (it->data(kHeaderRole).toBool())
            continue;
        const QString n = it->data(kNameRole).toString();
        const bool used = n.isEmpty() ? (paragraph.isEmpty() && character.isEmpty()) : (n == paragraph || n == character);
        it->setIcon(used ? icons::action(QStringLiteral("go-next")) : QIcon());
    }
    // follow the cursor while the panel itself is not being used
    if (!isActiveWindow()) {
        const QString want = !character.isEmpty() ? character : paragraph;
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem *it = m_list->item(i);
            if (!it->data(kHeaderRole).toBool() && it->data(kNameRole).toString() == want) {
                m_list->setCurrentRow(i);
                break;
            }
        }
    }
}

void StylesPanel::updateButtons()
{
    const QString n = selectedStyle();
    const bool isStyle = m_hasProject && !n.isEmpty();
    m_new->setEnabled(m_hasProject);
    m_apply->setEnabled(m_hasProject && m_list->currentItem() && !m_list->currentItem()->data(kHeaderRole).toBool());
    m_redefine->setEnabled(isStyle || (m_hasProject && noStyleSelected()));
    m_redefine->setToolTip(noStyleSelected() ? tr("Make the Formatting at the Cursor the Main Text Style…")
                                             : tr("Redefine the Style From the Selection…"));
    m_edit->setEnabled(isStyle);
    m_delete->setEnabled(isStyle && !styles::isFixedName(n));
    m_up->setEnabled(isStyle);
    m_down->setEnabled(isStyle);
}

void StylesPanel::showMenu(const QPoint &pos)
{
    QListWidgetItem *it = m_list->itemAt(pos);
    if (!it || it->data(kHeaderRole).toBool() || !m_hasProject)
        return;
    m_list->setCurrentItem(it);
    const QString n = it->data(kNameRole).toString();
    QMenu menu(this);
    menu.addAction(tr("Apply"), this, [this, n] { emit applyRequested(n); });
    if (n.isEmpty()) {
        // "No Style" is the project's main text style (Project Settings > Formatting)
        menu.addSeparator();
        menu.addAction(tr("Redefine From Selection…"), this, [this] { emit redefineNoStyleRequested(); })
            ->setObjectName(QStringLiteral("stylesRedefineNoStyle"));
    } else {
        menu.addSeparator();
        menu.addAction(tr("Redefine From Selection…"), this, [this, n] { emit redefineRequested(n); });
        menu.addAction(tr("Edit…"), this, [this, n] { emit editRequested(n); });
        const Style *s = styles::sheet(m_f).find(n);
        const Style *def = styles::builtinDefault(n);
        if (s && def && !s->sameFormatting(*def))
            menu.addAction(tr("Reset to the Built-in Definition"), this, [this, n] { emit resetRequested(n); });
        if (!styles::isFixedName(n)) {
            menu.addSeparator();
            menu.addAction(tr("Delete"), this, [this, n] { emit deleteRequested(n); });
        }
    }
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

} // namespace qb
