#include "ui/FormattingChange.h"

#include "core/BinderItem.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/Snapshots.h"
#include "text/DisplayFormat.h"
#include "text/Styles.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTextDocument>
#include <QToolButton>
#include <functional>
#include <memory>

namespace qb {
namespace formatchange {

namespace {

// op on the text of every text document; a document that op would change gets
// a snapshot with the title first (op runs on a copy to find out).
int forEachText(Project *p, const QString &title, const std::function<int(QTextDocument *)> &op, QString *error)
{
    int changed = 0;
    QStringList failed;
    for (BinderItem *it : p->allItems()) {
        if (!it->hasText() || it->scriptMode)
            continue;
        const QString id = it->id;
        QString err;
        const int n = p->docs()->editStored(
            id, false,
            [&](QTextDocument *doc) {
                std::unique_ptr<QTextDocument> probe(doc->clone());
                if (op(probe.get()) <= 0)
                    return 0;
                snapshots::take(p, id, title, nullptr, doc);
                return op(doc);
            },
            &err);
        if (n < 0)
            failed << it->title + QStringLiteral(": ") + err;
        else if (n > 0)
            ++changed;
    }
    if (!failed.isEmpty()) {
        if (error)
            *error = failed.join(QLatin1Char('\n'));
        return -1;
    }
    return changed;
}

} // namespace

int followProject(Project *p, const Formatting &oldF, bool ownToo, QString *error)
{
    const Formatting newF = p->formatting;
    const int chars = direct::changedCharMembers(oldF, newF);
    const int para = direct::changedParaBits(oldF, newF);
    if (!chars && !para)
        return 0;
    return forEachText(
        p, QObject::tr("Before the project formatting changed"),
        [&](QTextDocument *doc) { return direct::followProject(doc, oldF, newF, chars, para, ownToo); }, error);
}

int followStyle(Project *p, const QString &style, bool character, int charMembers, const direct::CharSpec &oldValues,
                const direct::CharSpec &newValues, QString *error)
{
    const Formatting f = p->formatting;
    return forEachText(
        p, QObject::tr("Before the style “%1” changed").arg(style),
        [&](QTextDocument *doc) {
            return direct::followStyle(doc, f, style, character, charMembers, oldValues, newValues);
        },
        error);
}

direct::CharSpec styleFont(const Formatting &f, const QString &style)
{
    direct::CharSpec sp;
    const Style *s = styles::find(f, style);
    if (s && s->character) {
        sp.family = s->chars.family;
        sp.size = s->chars.size;
        return sp;
    }
    sp.family = s && !s->chars.family.isEmpty() ? s->chars.family : display::resolvedFontFamily(f);
    if (s && s->chars.size > 0)
        sp.size = s->chars.size;
    else if (s && s->sizeAdjust != 0)
        sp.size = styles::adjustedSize(f.fontSize, s->sizeAdjust);
    else
        sp.size = f.fontSize;
    return sp;
}

} // namespace formatchange

// ------------------------------------------------------------------ FontScopeBar

FontScopeBar::FontScopeBar(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("fontScopeBar"));
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(8, 3, 4, 3);
    m_text = new QLabel(this);
    m_text->setObjectName(QStringLiteral("fontScopeText"));
    m_text->setWordWrap(true);
    m_button = new QPushButton(this);
    m_button->setObjectName(QStringLiteral("fontScopeApply"));
    m_button->setFocusPolicy(Qt::NoFocus);
    auto *close = new QToolButton(this);
    close->setObjectName(QStringLiteral("fontScopeClose"));
    close->setAutoRaise(true);
    close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    close->setToolTip(tr("Only here (close)"));
    close->setFocusPolicy(Qt::NoFocus);
    l->addWidget(m_text, 1);
    l->addWidget(m_button);
    l->addWidget(close);
    connect(m_button, &QPushButton::clicked, this, [this] {
        hide();
        emit accepted();
    });
    connect(close, &QToolButton::clicked, this, &QWidget::hide);
    updateColors();
    hide();
}

void FontScopeBar::offer(const QString &text, const QString &button)
{
    m_text->setText(text);
    m_button->setText(button);
    show();
}

QString FontScopeBar::text() const { return m_text->text(); }

void FontScopeBar::changeEvent(QEvent *e)
{
    QFrame::changeEvent(e);
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange)
        updateColors();
}

void FontScopeBar::updateColors()
{
    // a light tint of the highlight colour, readable in light and dark themes
    if (m_coloring)
        return;
    m_coloring = true;
    QPalette pal = parentWidget() ? parentWidget()->palette() : QApplication::palette();
    const QColor base = pal.color(QPalette::Window);
    const QColor hl = pal.color(QPalette::Highlight);
    const double t = 0.18;
    pal.setColor(QPalette::Window,
                 QColor::fromRgbF(base.redF() * (1 - t) + hl.redF() * t, base.greenF() * (1 - t) + hl.greenF() * t,
                                  base.blueF() * (1 - t) + hl.blueF() * t));
    setPalette(pal);
    m_coloring = false;
}

} // namespace qb
