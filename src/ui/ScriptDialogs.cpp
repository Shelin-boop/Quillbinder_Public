#include "ui/ScriptDialogs.h"

#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "core/ItemIcons.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace qb {

namespace {
QString colorSheet(const QColor &c)
{
    return c.isValid() ? QStringLiteral("QToolButton { background: %1; }").arg(c.name()) : QString();
}
} // namespace

ScriptSettingsDialog::ScriptSettingsDialog(const script::Format &format, QWidget *parent) : QDialog(parent), m_f(format)
{
    setWindowTitle(tr("Script Settings"));
    setObjectName(QStringLiteral("scriptSettings"));
    resize(860, 640);
    for (const script::Element &e : m_f.elements)
        m_orig << e.name;

    auto *root = new QVBoxLayout(this);

    // ---- the format
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Format:"), this));
    m_formatName = new QLineEdit(m_f.name, this);
    m_formatName->setObjectName(QStringLiteral("formatName"));
    m_formatName->setMinimumWidth(170);
    top->addWidget(m_formatName, 2);
    top->addSpacing(12);
    top->addWidget(new QLabel(tr("Font:"), this));
    m_autoFont = new QCheckBox(tr("Typewriter font of the system"), this);
    m_autoFont->setObjectName(QStringLiteral("autoFont"));
    m_autoFont->setToolTip(tr("Courier Prime, Courier New or the nearest font that is installed (now: %1)")
                               .arg(script::resolvedFontFamily(script::Format())));
    top->addWidget(m_autoFont);
    m_font = new QFontComboBox(this);
    m_font->setObjectName(QStringLiteral("scriptFont"));
    top->addWidget(m_font, 2);
    m_fontSize = new QDoubleSpinBox(this);
    m_fontSize->setObjectName(QStringLiteral("scriptFontSize"));
    m_fontSize->setRange(6, 48);
    m_fontSize->setDecimals(1);
    m_fontSize->setSuffix(tr(" pt"));
    top->addWidget(m_fontSize);
    top->addSpacing(12);
    top->addWidget(new QLabel(tr("Units:"), this));
    m_unit = new QComboBox(this);
    m_unit->setObjectName(QStringLiteral("scriptUnit"));
    m_unit->addItem(tr("inches"), QStringLiteral("in"));
    m_unit->addItem(tr("centimeters"), QStringLiteral("cm"));
    top->addWidget(m_unit);
    root->addLayout(top);

    auto *mid = new QHBoxLayout;
    // ---- list of elements
    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("Elements:"), this));
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("elementList"));
    m_list->setMaximumWidth(210);
    left->addWidget(m_list, 1);
    auto *lb = new QHBoxLayout;
    auto button = [this, lb](const QString &text, const QString &tip, const QString &name) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setObjectName(name);
        lb->addWidget(b);
        return b;
    };
    connect(button(QStringLiteral("+"), tr("New element"), QStringLiteral("addElement")), &QToolButton::clicked, this,
            &ScriptSettingsDialog::addElement);
    connect(button(QStringLiteral("−"), tr("Remove the element"), QStringLiteral("removeElement")),
            &QToolButton::clicked, this, &ScriptSettingsDialog::removeElement);
    connect(button(QStringLiteral("↑"), tr("Move up"), QStringLiteral("elementUp")), &QToolButton::clicked, this,
            [this] { moveElement(-1); });
    connect(button(QStringLiteral("↓"), tr("Move down"), QStringLiteral("elementDown")), &QToolButton::clicked, this,
            [this] { moveElement(1); });
    lb->addStretch(1);
    left->addLayout(lb);
    mid->addLayout(left);

    // ---- the element
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("elementTabs"));
    {
        auto *w = new QWidget(m_tabs);
        auto *f = new QFormLayout(w);
        m_name = new QLineEdit(w);
        m_name->setObjectName(QStringLiteral("elementName"));
        f->addRow(tr("Name:"), m_name);
        m_role = new QComboBox(w);
        m_role->setObjectName(QStringLiteral("elementRole"));
        for (script::Role r : script::elementRoles())
            m_role->addItem(script::roleLabel(r), script::roleKey(r));
        m_role->setToolTip(tr("What the element is to other screenwriting programs (Fountain and Final Draft files)"));
        f->addRow(tr("Kind:"), m_role);
        m_key = new QLineEdit(w);
        m_key->setObjectName(QStringLiteral("elementKey"));
        m_key->setMaxLength(1);
        m_key->setMaximumWidth(40);
        m_key->setToolTip(tr("The letter that chooses the element in the list of elements (Enter on an empty line)"));
        f->addRow(tr("Letter in the list of elements:"), m_key);
        m_first = new QCheckBox(tr("An empty script starts with this element"), w);
        m_first->setObjectName(QStringLiteral("elementFirst"));
        f->addRow(QString(), m_first);
        m_tabs->addTab(w, tr("Element"));
    }
    {
        auto *w = new QWidget(m_tabs);
        auto *f = new QFormLayout(w);
        m_bold = new QCheckBox(tr("Bold"), w);
        m_italic = new QCheckBox(tr("Italic"), w);
        m_underline = new QCheckBox(tr("Underlined"), w);
        m_caps = new QCheckBox(tr("ALL CAPITALS"), w);
        m_caps->setObjectName(QStringLiteral("elementCaps"));
        m_bold->setObjectName(QStringLiteral("elementBold"));
        f->addRow(tr("Style:"), m_bold);
        f->addRow(QString(), m_italic);
        f->addRow(QString(), m_underline);
        f->addRow(QString(), m_caps);
        auto *row = new QHBoxLayout;
        m_color = new QToolButton(w);
        m_color->setObjectName(QStringLiteral("elementColor"));
        m_color->setText(tr("Choose…"));
        auto *noColor = new QPushButton(tr("Automatic"), w);
        row->addWidget(m_color);
        row->addWidget(noColor);
        row->addStretch(1);
        f->addRow(tr("Text color:"), row);
        connect(m_color, &QToolButton::clicked, this, [this] {
            script::Element *e = current();
            if (!e)
                return;
            const QColor c = QColorDialog::getColor(e->color.isValid() ? e->color : QColor(Qt::black), this);
            if (c.isValid()) {
                e->color = c;
                m_color->setStyleSheet(colorSheet(c));
                updatePreview();
            }
        });
        connect(noColor, &QPushButton::clicked, this, [this] {
            if (script::Element *e = current()) {
                e->color = QColor();
                m_color->setStyleSheet(QString());
                updatePreview();
            }
        });
        m_tabs->addTab(w, tr("Font"));
    }
    {
        auto *w = new QWidget(m_tabs);
        auto *f = new QFormLayout(w);
        m_align = new QComboBox(w);
        m_align->setObjectName(QStringLiteral("elementAlign"));
        m_align->addItems({tr("Left"), tr("Centered"), tr("Right")});
        f->addRow(tr("Alignment:"), m_align);
        auto length = [w](const QString &name) {
            auto *s = new QDoubleSpinBox(w);
            s->setObjectName(name);
            s->setDecimals(2);
            s->setSingleStep(0.1);
            return s;
        };
        m_left = length(QStringLiteral("elementLeft"));
        m_right = length(QStringLiteral("elementRight"));
        m_firstLine = length(QStringLiteral("elementFirstLine"));
        f->addRow(tr("Indent from the left:"), m_left);
        f->addRow(tr("Indent from the right:"), m_right);
        f->addRow(tr("First line:"), m_firstLine);
        m_before = new QDoubleSpinBox(w);
        m_before->setObjectName(QStringLiteral("elementBefore"));
        m_before->setRange(0, 10);
        m_before->setDecimals(1);
        m_before->setSingleStep(0.5);
        f->addRow(tr("Blank lines above:"), m_before);
        m_line = new QDoubleSpinBox(w);
        m_line->setRange(0.5, 4);
        m_line->setDecimals(2);
        m_line->setSingleStep(0.25);
        f->addRow(tr("Line spacing:"), m_line);
        m_keep = new QCheckBox(tr("Keep on one page with the next paragraph (compiled files)"), w);
        f->addRow(QString(), m_keep);
        m_tabs->addTab(w, tr("Paragraph"));
    }
    {
        auto *w = new QWidget(m_tabs);
        auto *f = new QFormLayout(w);
        m_enter = new QComboBox(w);
        m_enter->setObjectName(QStringLiteral("elementEnter"));
        m_tabEmpty = new QComboBox(w);
        m_tabEmpty->setObjectName(QStringLiteral("elementTabEmpty"));
        m_tabEnd = new QComboBox(w);
        m_tabEnd->setObjectName(QStringLiteral("elementTabEnd"));
        f->addRow(tr("Enter at the end goes to:"), m_enter);
        f->addRow(tr("Tab on an empty line changes it to:"), m_tabEmpty);
        f->addRow(tr("Tab after text goes to:"), m_tabEnd);
        m_tabInserts = new QLineEdit(w);
        m_tabInserts->setObjectName(QStringLiteral("elementTabInserts"));
        m_tabInserts->setToolTip(tr("If this is filled in, Tab types it instead of going to another element "
                                    "(scene headings: \" - \" before the time of day)"));
        f->addRow(tr("…or Tab types:"), m_tabInserts);
        auto *row = new QHBoxLayout;
        m_prefix = new QLineEdit(w);
        m_prefix->setObjectName(QStringLiteral("elementPrefix"));
        m_suffix = new QLineEdit(w);
        m_suffix->setObjectName(QStringLiteral("elementSuffix"));
        row->addWidget(m_prefix);
        row->addWidget(new QLabel(tr("text"), w));
        row->addWidget(m_suffix);
        f->addRow(tr("Typed around the text by itself:"), row);
        m_tabs->addTab(w, tr("Tab and Enter"));
    }
    {
        auto *w = new QWidget(m_tabs);
        auto *v = new QVBoxLayout(w);
        v->addWidget(new QLabel(tr("Offered while the paragraph is typed (one per line):"), w));
        m_completions = new QPlainTextEdit(w);
        m_completions->setObjectName(QStringLiteral("elementCompletions"));
        v->addWidget(m_completions, 2);
        v->addWidget(new QLabel(tr("Offered after the text Tab types (one per line):"), w));
        m_tabCompletions = new QPlainTextEdit(w);
        v->addWidget(m_tabCompletions, 2);
        m_fromScript = new QCheckBox(tr("Also offer what other paragraphs of this element in the document say "
                                        "(character names, locations)"),
                                     w);
        m_fromScript->setObjectName(QStringLiteral("elementFromScript"));
        v->addWidget(m_fromScript);
        m_tabs->addTab(w, tr("Auto-Complete"));
    }
    auto *right = new QVBoxLayout;
    right->addWidget(m_tabs, 3);
    right->addWidget(new QLabel(tr("Preview:"), this));
    m_preview = new QTextEdit(this);
    m_preview->setObjectName(QStringLiteral("scriptPreview"));
    m_preview->setReadOnly(true);
    right->addWidget(m_preview, 2);
    mid->addLayout(right, 1);
    root->addLayout(mid, 1);

    // ---- buttons
    auto *bottom = new QHBoxLayout;
    m_manage = new QToolButton(this);
    m_manage->setObjectName(QStringLiteral("manageFormats"));
    m_manage->setText(tr("Manage"));
    m_manage->setPopupMode(QToolButton::InstantPopup);
    m_manage->setMenu(new QMenu(m_manage));
    connect(m_manage->menu(), &QMenu::aboutToShow, this, &ScriptSettingsDialog::fillManageMenu);
    bottom->addWidget(m_manage);
    bottom->addStretch(1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bottom->addWidget(bb);
    root->addLayout(bottom);
    connect(bb, &QDialogButtonBox::accepted, this, &ScriptSettingsDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // ---- wiring
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_loading)
            return;
        m_cur = row;
        load();
    });
    connect(m_name, &QLineEdit::editingFinished, this, &ScriptSettingsDialog::commitName);
    auto changed = [this] {
        if (!m_loading) {
            store();
            updatePreview();
        }
    };
    for (QCheckBox *c : {m_bold, m_italic, m_underline, m_caps, m_keep, m_first, m_fromScript})
        connect(c, &QCheckBox::toggled, this, changed);
    for (QComboBox *c : {m_role, m_align, m_enter, m_tabEmpty, m_tabEnd})
        connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, changed);
    for (QDoubleSpinBox *s : {m_left, m_right, m_firstLine, m_before, m_line})
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, changed);
    for (QLineEdit *l : {m_key, m_tabInserts, m_prefix, m_suffix})
        connect(l, &QLineEdit::textEdited, this, changed);
    connect(m_completions, &QPlainTextEdit::textChanged, this, changed);
    connect(m_tabCompletions, &QPlainTextEdit::textChanged, this, changed);
    connect(m_formatName, &QLineEdit::textEdited, this, [this](const QString &t) { m_f.name = t.trimmed(); });
    connect(m_autoFont, &QCheckBox::toggled, this, [this](bool on) {
        m_font->setEnabled(!on);
        if (m_loading)
            return;
        m_f.fontFamily = on ? QString() : m_font->currentFont().family();
        updatePreview();
    });
    connect(m_font, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        if (m_loading || m_autoFont->isChecked())
            return;
        m_f.fontFamily = font.family();
        updatePreview();
    });
    connect(m_fontSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (m_loading)
            return;
        m_f.fontSize = v;
        updatePreview();
    });
    connect(m_unit, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (m_loading)
            return;
        m_f.unit = m_unit->currentData().toString();
        applyUnit();
        load();
    });

    useFormat(m_f);
}

void ScriptSettingsDialog::useFormat(const script::Format &f)
{
    // elements keep their history where the names agree
    QStringList orig;
    for (const script::Element &e : f.elements) {
        const int was = m_f.indexOf(e.name);
        orig << (was >= 0 && was < m_orig.size() ? m_orig.at(was) : QString());
    }
    m_f = f;
    m_orig = orig;
    m_loading = true;
    m_formatName->setText(m_f.name);
    m_autoFont->setChecked(m_f.fontFamily.isEmpty());
    m_font->setEnabled(!m_f.fontFamily.isEmpty());
    m_font->setCurrentFont(QFont(script::resolvedFontFamily(m_f)));
    m_fontSize->setValue(m_f.fontSize);
    m_unit->setCurrentIndex(m_f.unit == QLatin1String("cm") ? 1 : 0);
    m_loading = false;
    applyUnit();
    m_cur = m_f.elements.isEmpty() ? -1 : 0;
    rebuildList();
    load();
    updatePreview();
}

QHash<QString, QString> ScriptSettingsDialog::renamed() const
{
    QHash<QString, QString> out;
    for (int i = 0; i < m_f.elements.size() && i < m_orig.size(); ++i)
        if (!m_orig.at(i).isEmpty() && m_orig.at(i) != m_f.elements.at(i).name)
            out.insert(m_orig.at(i), m_f.elements.at(i).name);
    return out;
}

script::Element *ScriptSettingsDialog::current()
{
    return m_cur >= 0 && m_cur < m_f.elements.size() ? &m_f.elements[m_cur] : nullptr;
}

double ScriptSettingsDialog::toUnit(double mm) const { return m_f.unit == QLatin1String("cm") ? mm / 10.0 : mm / 25.4; }
double ScriptSettingsDialog::fromUnit(double v) const { return m_f.unit == QLatin1String("cm") ? v * 10.0 : v * 25.4; }

void ScriptSettingsDialog::applyUnit()
{
    const bool cm = m_f.unit == QLatin1String("cm");
    m_loading = true;
    for (QDoubleSpinBox *s : {m_left, m_right, m_firstLine}) {
        s->setSuffix(cm ? tr(" cm") : tr(" in"));
        s->setSingleStep(cm ? 0.25 : 0.1);
        s->setRange(s == m_firstLine ? (cm ? -10.0 : -4.0) : 0.0, cm ? 20.0 : 8.0);
    }
    m_loading = false;
}

void ScriptSettingsDialog::rebuildList()
{
    m_loading = true;
    m_list->clear();
    for (const script::Element &e : m_f.elements)
        m_list->addItem(e.name);
    m_list->setCurrentRow(m_cur);
    m_loading = false;
    fillCombos();
}

void ScriptSettingsDialog::fillCombos()
{
    const bool was = m_loading;
    m_loading = true;
    struct Combo {
        QComboBox *box;
        QString none;
    };
    for (const Combo &c : {Combo{m_enter, tr("(the same element)")}, Combo{m_tabEmpty, tr("(the next element in the list)")},
                           Combo{m_tabEnd, tr("(nothing)")}}) {
        c.box->clear();
        c.box->addItem(c.none, QString());
        for (const script::Element &e : m_f.elements)
            c.box->addItem(e.name, e.name);
    }
    m_loading = was;
}

void ScriptSettingsDialog::load()
{
    const script::Element *e = current();
    m_tabs->setEnabled(e != nullptr);
    if (!e)
        return;
    m_loading = true;
    m_name->setText(e->name);
    m_role->setCurrentIndex(qMax(0, m_role->findData(script::roleKey(e->role))));
    m_key->setText(e->key);
    m_first->setChecked(m_f.first() && m_f.first()->name == e->name);
    m_bold->setChecked(e->bold);
    m_italic->setChecked(e->italic);
    m_underline->setChecked(e->underline);
    m_caps->setChecked(e->allCaps);
    m_color->setStyleSheet(colorSheet(e->color));
    m_align->setCurrentIndex(qBound(0, e->align, 2));
    m_left->setValue(toUnit(e->leftMm));
    m_right->setValue(toUnit(e->rightMm));
    m_firstLine->setValue(toUnit(e->firstMm));
    m_before->setValue(e->linesBefore);
    m_line->setValue(e->lineSpacing);
    m_keep->setChecked(e->keepWithNext);
    m_enter->setCurrentIndex(qMax(0, m_enter->findData(e->onEnter)));
    m_tabEmpty->setCurrentIndex(qMax(0, m_tabEmpty->findData(e->onTabEmpty)));
    m_tabEnd->setCurrentIndex(qMax(0, m_tabEnd->findData(e->onTabEnd)));
    m_tabInserts->setText(e->tabInserts);
    m_prefix->setText(e->prefix);
    m_suffix->setText(e->suffix);
    m_completions->setPlainText(e->completions.join(QLatin1Char('\n')));
    m_tabCompletions->setPlainText(e->tabCompletions.join(QLatin1Char('\n')));
    m_fromScript->setChecked(e->completeFromScript);
    m_loading = false;
}

void ScriptSettingsDialog::store()
{
    script::Element *e = current();
    if (!e)
        return;
    e->role = script::roleFromKey(m_role->currentData().toString());
    e->key = m_key->text().toUpper();
    if (m_first->isChecked())
        m_f.firstElement = e->name;
    else if (m_f.firstElement.compare(e->name, Qt::CaseInsensitive) == 0)
        m_f.firstElement.clear();
    e->bold = m_bold->isChecked();
    e->italic = m_italic->isChecked();
    e->underline = m_underline->isChecked();
    e->allCaps = m_caps->isChecked();
    e->align = m_align->currentIndex();
    e->leftMm = fromUnit(m_left->value());
    e->rightMm = fromUnit(m_right->value());
    e->firstMm = fromUnit(m_firstLine->value());
    e->linesBefore = m_before->value();
    e->lineSpacing = m_line->value();
    e->keepWithNext = m_keep->isChecked();
    e->onEnter = m_enter->currentData().toString();
    e->onTabEmpty = m_tabEmpty->currentData().toString();
    e->onTabEnd = m_tabEnd->currentData().toString();
    e->tabInserts = m_tabInserts->text();
    e->prefix = m_prefix->text();
    e->suffix = m_suffix->text();
    auto lines = [](const QPlainTextEdit *t) {
        QStringList l;
        for (const QString &s : t->toPlainText().split(QLatin1Char('\n')))
            if (!s.trimmed().isEmpty())
                l << s;
        return l;
    };
    e->completions = lines(m_completions);
    e->tabCompletions = lines(m_tabCompletions);
    e->completeFromScript = m_fromScript->isChecked();
}

void ScriptSettingsDialog::commitName()
{
    script::Element *e = current();
    if (!e || m_loading)
        return;
    const QString name = m_name->text().trimmed();
    if (name == e->name)
        return;
    const int other = m_f.indexOf(name);
    if (!script::validElementName(name) || (other >= 0 && other != m_cur)) {
        m_loading = true;
        m_name->setText(e->name);
        m_loading = false;
        QMessageBox::information(this, windowTitle(),
                                 other >= 0 ? tr("There is an element called \"%1\" already.").arg(name)
                                            : tr("An element name cannot be empty or contain the characters : \" < > \\ { } ; /"));
        return;
    }
    const QString old = e->name;
    e->name = name;
    for (script::Element &o : m_f.elements)
        for (QString *ref : {&o.onEnter, &o.onTabEmpty, &o.onTabEnd})
            if (ref->compare(old, Qt::CaseInsensitive) == 0)
                *ref = name;
    if (m_f.firstElement.compare(old, Qt::CaseInsensitive) == 0)
        m_f.firstElement = name;
    rebuildList();
    load();
    updatePreview();
}

void ScriptSettingsDialog::selectElement(const QString &name)
{
    const int i = m_f.indexOf(name);
    if (i >= 0)
        m_list->setCurrentRow(i);
}

void ScriptSettingsDialog::addElement()
{
    script::Element e;
    QString name = tr("New Element");
    for (int n = 2; m_f.indexOf(name) >= 0; ++n)
        name = tr("New Element %1").arg(n);
    e.name = name;
    e.role = script::Role::Action;
    const int at = m_cur >= 0 ? m_cur + 1 : int(m_f.elements.size());
    m_f.elements.insert(at, e);
    m_orig.insert(at, QString());
    m_cur = at;
    rebuildList();
    load();
    updatePreview();
    m_tabs->setCurrentIndex(0);
    m_name->setFocus();
    m_name->selectAll();
}

void ScriptSettingsDialog::removeElement()
{
    if (!current() || m_f.elements.size() <= 1)
        return;
    const QString name = current()->name;
    m_f.elements.removeAt(m_cur);
    m_orig.removeAt(m_cur);
    for (script::Element &o : m_f.elements)
        for (QString *ref : {&o.onEnter, &o.onTabEmpty, &o.onTabEnd})
            if (ref->compare(name, Qt::CaseInsensitive) == 0)
                ref->clear();
    if (m_f.firstElement.compare(name, Qt::CaseInsensitive) == 0)
        m_f.firstElement = m_f.elements.first().name;
    m_cur = qMin(m_cur, int(m_f.elements.size()) - 1);
    rebuildList();
    load();
    updatePreview();
}

void ScriptSettingsDialog::moveElement(int delta)
{
    const int to = m_cur + delta;
    if (!current() || to < 0 || to >= m_f.elements.size())
        return;
    m_f.elements.swapItemsAt(m_cur, to);
    m_orig.swapItemsAt(m_cur, to);
    m_cur = to;
    rebuildList();
    load();
    updatePreview();
}

void ScriptSettingsDialog::updatePreview()
{
    const Formatting f = script::withFormat(Formatting(), m_f);
    QTextDocument *doc = m_preview->document();
    doc->clear();
    display::prepareDocument(doc, f, 1.0);
    QTextCursor c(doc);
    bool first = true;
    static const QHash<int, QString> samples = {
        {int(script::Role::SceneHeading), QStringLiteral("INT. LIGHTHOUSE - NIGHT")},
        {int(script::Role::Action), QStringLiteral("Rain against the lamp room windows. The keeper winds the clockwork and listens.")},
        {int(script::Role::Character), QStringLiteral("Keeper")},
        {int(script::Role::Parenthetical), QStringLiteral("to himself")},
        {int(script::Role::Dialogue), QStringLiteral("Forty years, and it has never once been late.")},
        {int(script::Role::Transition), QStringLiteral("Cut to:")},
        {int(script::Role::Shot), QStringLiteral("Close on the lamp")},
        {int(script::Role::Lyrics), QStringLiteral("Oh the wind blows cold tonight")},
        {int(script::Role::Centered), QStringLiteral("The End")},
        {int(script::Role::Section), QStringLiteral("Act One")},
    };
    for (const script::Element &e : m_f.elements) {
        const QString style = script::styleName(e.name);
        const QTextCharFormat cf = display::charFormatFor(f, style);
        if (first)
            c.setBlockFormat(display::blockFormatFor(f, style));
        else
            c.insertBlock(display::blockFormatFor(f, style));
        first = false;
        c.setBlockCharFormat(cf);
        const QString sample = samples.value(int(e.role), e.name);
        // inside parentheses a text starts small
        QString shown = sample;
        if (e.prefix.trimmed() == QLatin1String("(") && !shown.isEmpty()) {
            shown[0] = shown.at(0).toLower();
            if (shown.endsWith(QLatin1Char('.')))
                shown.chop(1);
        }
        c.insertText(e.prefix + shown + e.suffix, cf);
    }
    // the element being edited is marked
    if (m_cur >= 0 && m_cur < doc->blockCount()) {
        QTextCursor sel(doc->findBlockByNumber(m_cur));
        QTextBlockFormat bf = sel.blockFormat();
        bf.setBackground(palette().color(QPalette::AlternateBase));
        sel.setBlockFormat(bf);
        m_preview->setTextCursor(sel);
        m_preview->ensureCursorVisible();
    }
}

void ScriptSettingsDialog::fillManageMenu()
{
    QMenu *m = m_manage->menu();
    m->clear();
    QMenu *use = m->addMenu(tr("Start From"));
    use->setObjectName(QStringLiteral("startFromMenu"));
    script::Kind last = script::Kind::BuiltIn;
    for (const script::Entry &e : script::available()) {
        if (e.kind != last)
            use->addSeparator();
        last = e.kind;
        const script::Format f = e.format;
        use->addAction(f.name, this, [this, f] { useFormat(f); })->setToolTip(f.description);
    }
    m->addSeparator();
    m->addAction(tr("Load From File…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Load Script Format"), QString(),
                                                          tr("Script formats (*.qbscript *.json)"));
        if (path.isEmpty())
            return;
        script::Format f;
        QString err;
        if (!script::readFile(path, &f, &err)) {
            QMessageBox::warning(this, windowTitle(), err);
            return;
        }
        useFormat(f);
    });
    m->addAction(tr("Save to File…"), this, [this] {
        QString path = QFileDialog::getSaveFileName(this, tr("Save Script Format"), m_f.name + QLatin1String(".qbscript"),
                                                    tr("Script formats (*.qbscript)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(QLatin1String(".qbscript")))
            path += QLatin1String(".qbscript");
        QString err;
        if (!script::writeFile(path, m_f, &err))
            QMessageBox::warning(this, windowTitle(), err);
    });
    m->addSeparator();
    m->addAction(tr("Save for Use in Other Projects"), this, [this] {
        if (m_f.name.trimmed().isEmpty())
            return;
        QString err;
        if (!script::saveOwn(m_f, &err))
            QMessageBox::warning(this, windowTitle(), err);
        else
            QMessageBox::information(this, windowTitle(),
                                     tr("\"%1\" is now in the list of formats under Format > Scriptwriting > Script Format "
                                        "in every project.")
                                         .arg(m_f.name));
    });
    bool own = false;
    for (const script::Entry &e : script::available())
        if (e.kind == script::Kind::Own && e.format.name.compare(m_f.name, Qt::CaseInsensitive) == 0)
            own = true;
    QAction *rm = m->addAction(tr("Remove \"%1\" From My Formats").arg(m_f.name), this, [this] { script::removeOwn(m_f.name); });
    rm->setEnabled(own);
}

void ScriptSettingsDialog::accept()
{
    commitName();
    store();
    if (m_f.name.trimmed().isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Give the format a name."));
        return;
    }
    if (m_f.elements.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("A script format needs at least one element."));
        return;
    }
    if (m_f.indexOf(m_f.firstElement) < 0)
        m_f.firstElement = m_f.elements.first().name;
    QDialog::accept();
}

} // namespace qb
