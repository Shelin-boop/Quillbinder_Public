// Scriptwriting in the text editor: the Tab and Enter keys move between the
// elements of the script format, element texts are completed while typing.
// (Part of TextEditor; the format itself is in text/Script.h.)
#include "ui/TextEditor.h"
#include "ui/AppSettings.h"

#include "text/DisplayFormat.h"
#include "text/Script.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QMenu>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>

namespace qb {

namespace {

const script::Element *elementOf(const Project *p, const QTextBlock &b)
{
    return p && b.isValid() ? script::element(p->formatting, b.blockFormat().stringProperty(props::Style)) : nullptr;
}

// The paragraph holds nothing but what the element types by itself.
bool isBare(const script::Element *el, const QString &text)
{
    return text.isEmpty() || (el && text == el->prefix + el->suffix);
}

// Position in the block where the writer's text ends (before the suffix).
int textEnd(const script::Element *el, const QString &text)
{
    if (el && !el->suffix.isEmpty() && text.endsWith(el->suffix))
        return int(text.size() - el->suffix.size());
    return int(text.size());
}

} // namespace

bool TextEditor::scriptActive() const
{
    return m_project && m_item && m_item->scriptMode && !m_isNotes && !isReadOnly();
}

QString TextEditor::scriptElementAtCursor() const
{
    const script::Element *el = elementOf(m_project, textCursor().block());
    return el ? el->name : QString();
}

void TextEditor::setScriptElement(const QString &element)
{
    if (!m_project)
        return;
    const Formatting f = m_project->formatting;
    const script::Element *el = script::format(f).find(element);
    QTextCursor c = textCursor();
    const bool single = !c.hasSelection();
    const int blockStart = c.block().position();
    c.beginEditBlock();
    // what the old elements typed by themselves goes
    for (const QTextBlock &b : selectedBlocks()) {
        const script::Element *old = elementOf(m_project, b);
        if (!old || old == el || (old->prefix.isEmpty() && old->suffix.isEmpty()))
            continue;
        const QString t = b.text();
        if (t.size() < old->prefix.size() + old->suffix.size() || !t.startsWith(old->prefix) || !t.endsWith(old->suffix))
            continue;
        QTextCursor e(b);
        if (!old->suffix.isEmpty()) {
            e.setPosition(b.position() + int(t.size() - old->suffix.size()));
            e.setPosition(b.position() + int(t.size()), QTextCursor::KeepAnchor);
            e.removeSelectedText();
        }
        if (!old->prefix.isEmpty()) {
            e.setPosition(b.position());
            e.setPosition(b.position() + int(old->prefix.size()), QTextCursor::KeepAnchor);
            e.removeSelectedText();
        }
    }
    display::applyParagraphStyle(textCursor(), el ? script::styleName(el->name) : QString(), f);
    bool bare = false;
    if (el && (!el->prefix.isEmpty() || !el->suffix.isEmpty())) {
        for (const QTextBlock &b : selectedBlocks()) {
            const QString t = b.text();
            const QTextCharFormat cf = b.charFormat();
            if (single)
                bare = t.isEmpty();
            QTextCursor e(b);
            if (!el->suffix.isEmpty() && !t.endsWith(el->suffix)) {
                e.setPosition(b.position() + int(t.size()));
                e.insertText(el->suffix, cf);
            }
            if (!el->prefix.isEmpty() && !t.startsWith(el->prefix)) {
                e.setPosition(b.position());
                e.insertText(el->prefix, cf);
            }
        }
    }
    if (single) {
        QTextCursor n = textCursor();
        if (bare) // inside the parentheses
            n.setPosition(blockStart + int(el->prefix.size()));
        const QTextBlock b = n.block();
        if (b.text().isEmpty()) {
            // what is typed next has the look of the element
            n.setBlockCharFormat(display::charFormatFor(f, b.blockFormat().stringProperty(props::Style)));
            n.setCharFormat(n.blockCharFormat());
        }
        c.endEditBlock(); // one step for Undo
        setTextCursor(n);
    } else {
        c.endEditBlock();
    }
    hideScriptCompletion();
    emit scriptElementChanged(this);
}

void TextEditor::scriptNewParagraph(const QString &element)
{
    const Formatting f = m_project->formatting;
    const script::Element *el = script::format(f).find(element);
    const QString style = el ? script::styleName(el->name) : QString();
    QTextCursor c = textCursor();
    c.beginEditBlock();
    const QTextCharFormat cf = display::charFormatFor(f, style);
    c.insertBlock(display::blockFormatFor(f, style), cf);
    c.setCharFormat(cf);
    if (el && (!el->prefix.isEmpty() || !el->suffix.isEmpty())) {
        c.insertText(el->prefix + el->suffix, cf);
        c.setPosition(c.position() - int(el->suffix.size()));
    }
    c.endEditBlock();
    setTextCursor(c);
    ensureCursorVisible();
    hideScriptCompletion();
    emit scriptElementChanged(this);
}

QMenu *TextEditor::scriptElementsMenu()
{
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("scriptElementsMenu"));
    if (!m_project)
        return menu;
    const QString current = scriptElementAtCursor();
    auto entry = [&](const QString &name, const QString &label, const QString &key) {
        // the element's letter chooses it while the menu is open
        QString text = label;
        text.replace(QLatin1Char('&'), QLatin1String("&&"));
        const int at = key.isEmpty() ? -1 : int(text.indexOf(key, 0, Qt::CaseInsensitive));
        if (at >= 0)
            text.insert(at, QLatin1Char('&'));
        else if (!key.isEmpty())
            text += QStringLiteral(" (&%1)").arg(key);
        QAction *a = menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(name.compare(current, Qt::CaseInsensitive) == 0);
        a->setData(name);
        connect(a, &QAction::triggered, this, [this, name] { setScriptElement(name); });
    };
    for (const script::Element &e : script::format(m_project->formatting).elements)
        entry(e.name, e.name, e.key);
    menu->addSeparator();
    entry(QString(), tr("General Text"), QStringLiteral("G"));
    return menu;
}

void TextEditor::showScriptElementsMenu()
{
    hideScriptCompletion();
    QMenu *menu = scriptElementsMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QRect r = cursorRect();
    menu->popup(viewport()->mapToGlobal(r.bottomLeft() + QPoint(0, 2)));
}

QString TextEditor::scriptTabHint() const
{
    if (!m_project)
        return QString();
    const QTextCursor c = textCursor();
    const QTextBlock b = c.block();
    const script::Format &fmt = script::format(m_project->formatting);
    const script::Element *el = elementOf(m_project, b);
    const QString t = b.text();
    if (isBare(el, t)) {
        if (!el)
            return fmt.first() ? fmt.first()->name : QString();
        if (!el->onTabEmpty.isEmpty())
            return el->onTabEmpty;
        const int i = fmt.indexOf(el->name);
        return fmt.elements.isEmpty() ? QString() : fmt.elements.at((i + 1) % fmt.elements.size()).name;
    }
    if (!el)
        return QString();
    if (!el->tabInserts.isEmpty())
        return tr("types \"%1\"").arg(el->tabInserts);
    if (c.positionInBlock() >= textEnd(el, t))
        return el->onTabEnd;
    return QString();
}

QString TextEditor::scriptEnterHint() const
{
    if (!m_project)
        return QString();
    const QTextCursor c = textCursor();
    const QTextBlock b = c.block();
    const script::Element *el = elementOf(m_project, b);
    const QString t = b.text();
    if (isBare(el, t))
        return tr("list of elements");
    if (!el)
        return QString();
    if (c.positionInBlock() < textEnd(el, t))
        return el->name;
    return el->onEnter.isEmpty() ? el->name : el->onEnter;
}

// ------------------------------------------------------------------ keys

bool TextEditor::scriptKeyPress(QKeyEvent *e)
{
    const int key = e->key();
    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool plain = !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const bool isEnter = key == Qt::Key_Return || key == Qt::Key_Enter;
    const bool isTab = key == Qt::Key_Tab || key == Qt::Key_Backtab;

    // a list of completions is open: Enter and Tab take the marked one
    if (m_scriptCompleter && m_scriptCompleter->popup()->isVisible()) {
        if (key == Qt::Key_Escape) {
            hideScriptCompletion();
            return true;
        }
        if ((isEnter || key == Qt::Key_Tab) && plain) {
            const QModelIndex idx = m_scriptCompleter->popup()->currentIndex();
            const QString text = idx.isValid() ? idx.data().toString() : QString();
            hideScriptCompletion();
            if (!text.isEmpty()) {
                acceptScriptCompletion(text);
                return true;
            }
        }
    }
    if (plain && !isEnter && !isTab && !e->text().isEmpty() && e->text().at(0).isPrint()) {
        // the first letter of an empty paragraph gets the look of its element
        // (the cursor of the editor may still hold the look of what was there before)
        const QTextBlock b = textCursor().block();
        if (b.text().isEmpty() && !textCursor().hasSelection())
            if (elementOf(m_project, b)) {
                const QTextCharFormat want =
                    display::charFormatFor(m_project->formatting, b.blockFormat().stringProperty(props::Style));
                if (currentCharFormat().font() != want.font() || currentCharFormat().foreground() != want.foreground())
                    setCurrentCharFormat(want);
            }
    }
    if (!plain || (!isEnter && !isTab))
        return false;

    QTextCursor c = textCursor();
    const QTextBlock b = c.block();
    if (c.hasSelection() || b.textList())
        return false;
    const script::Format &fmt = script::format(m_project->formatting);
    const script::Element *el = elementOf(m_project, b);
    const QString t = b.text();
    const bool bare = isBare(el, t);
    const int end = textEnd(el, t);

    if (isEnter) {
        if (mods & Qt::ShiftModifier)
            return false; // line break
        if (bare) {
            showScriptElementsMenu();
            return true;
        }
        if (!el || c.positionInBlock() < end)
            return false; // general text; or in the middle: both halves keep the element
        c.setPosition(b.position() + int(t.size()));
        setTextCursor(c);
        scriptNewParagraph(el->onEnter.isEmpty() ? el->name : el->onEnter);
        return true;
    }

    // Tab
    if (bare) {
        QString target;
        if (fmt.elements.isEmpty())
            return false;
        if (key == Qt::Key_Backtab) {
            if (el) {
                for (const script::Element &o : fmt.elements)
                    if (o.onTabEmpty.compare(el->name, Qt::CaseInsensitive) == 0 && o.name != el->name) {
                        target = o.name;
                        break;
                    }
                if (target.isEmpty()) {
                    const int i = fmt.indexOf(el->name);
                    target = fmt.elements.at((i + fmt.elements.size() - 1) % fmt.elements.size()).name;
                }
            } else {
                target = fmt.elements.last().name;
            }
        } else if (!el) {
            target = fmt.first()->name;
        } else if (!el->onTabEmpty.isEmpty() && fmt.indexOf(el->onTabEmpty) >= 0) {
            target = el->onTabEmpty;
        } else {
            target = fmt.elements.at((fmt.indexOf(el->name) + 1) % fmt.elements.size()).name;
        }
        setScriptElement(target);
        return true;
    }
    if (!el)
        return false; // general text: a tab
    if (key == Qt::Key_Backtab)
        return true;
    if (!el->tabInserts.isEmpty()) {
        if (!t.left(c.positionInBlock()).endsWith(el->tabInserts)) {
            c.insertText(el->tabInserts);
            setTextCursor(c);
        }
        updateScriptCompletion(true);
        return true;
    }
    if (c.positionInBlock() >= end && !el->onTabEnd.isEmpty() && fmt.indexOf(el->onTabEnd) >= 0) {
        c.setPosition(b.position() + int(t.size()));
        setTextCursor(c);
        scriptNewParagraph(el->onTabEnd);
    }
    return true; // Tab inside the text of an element does nothing
}

void TextEditor::scriptAfterKey(QKeyEvent *e)
{
    const bool noMods = !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const bool isText = !e->text().isEmpty() && e->text().at(0).isPrint() && noMods;
    if (isText) {
        // elements in capitals: what is typed becomes capitals
        QTextCursor c = textCursor();
        const script::Element *el = elementOf(m_project, c.block());
        if (el && el->allCaps && !c.hasSelection() && c.positionInBlock() > 0) {
            QTextCursor s = c;
            s.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
            const QString typed = s.selectedText();
            if (typed != typed.toUpper()) {
                const QTextCharFormat cf = s.charFormat();
                s.joinPreviousEditBlock();
                s.insertText(typed.toUpper(), cf);
                s.endEditBlock();
            }
        }
        updateScriptCompletion(false);
        emit scriptElementChanged(this); // what Tab and Enter do may have changed
    } else if (e->key() == Qt::Key_Backspace || e->key() == Qt::Key_Delete) {
        updateScriptCompletion(false);
        emit scriptElementChanged(this);
    } else if (e->key() != Qt::Key_Shift && e->key() != Qt::Key_Control && e->key() != Qt::Key_Alt
               && e->key() != Qt::Key_Meta && e->key() != Qt::Key_Up && e->key() != Qt::Key_Down) {
        hideScriptCompletion();
    }
}

// ------------------------------------------------------------------ completion

// The piece of the paragraph that is being completed.
struct TextEditor::ScriptPiece {
    const script::Element *el = nullptr;
    int from = 0;          // position in the block
    int to = 0;
    int part = 0;          // 0: start of the paragraph, 1: after the text Tab types
    QString typed;
};

bool TextEditor::scriptPiece(ScriptPiece *out) const
{
    const QTextCursor c = textCursor();
    const QTextBlock b = c.block();
    const script::Element *el = elementOf(m_project, b);
    if (!el || c.hasSelection())
        return false;
    const QString t = b.text();
    const int end = textEnd(el, t);
    if (c.positionInBlock() != end)
        return false; // only at the end of the text
    int from = (!el->prefix.isEmpty() && t.startsWith(el->prefix)) ? int(el->prefix.size()) : 0;
    int part = 0;
    if (!el->tabInserts.isEmpty()) {
        const int at = int(t.lastIndexOf(el->tabInserts, end - 1));
        if (at >= from) {
            from = at + int(el->tabInserts.size());
            part = 1;
        }
    }
    if (from > end)
        return false;
    out->el = el;
    out->from = from;
    out->to = end;
    out->part = part;
    out->typed = t.mid(from, end - from);
    return true;
}

QStringList TextEditor::scriptCompletions(bool evenIfEmpty) const
{
    ScriptPiece p;
    if (!m_project || !scriptPiece(&p))
        return {};
    if (p.typed.trimmed().isEmpty() && !(evenIfEmpty && p.part == 1))
        return {};
    QStringList all = p.part == 0 ? p.el->completions : p.el->tabCompletions;
    if (p.el->completeFromScript || p.part == 1)
        for (const QString &u : script::usedTexts(document(), m_project->formatting, *p.el, p.part,
                                                  textCursor().block().blockNumber()))
            if (!all.contains(u, Qt::CaseInsensitive))
                all << u;
    QStringList out;
    for (const QString &cnd : all)
        if (cnd.startsWith(p.typed, Qt::CaseInsensitive) && cnd.compare(p.typed, Qt::CaseInsensitive) != 0
            && cnd.trimmed().compare(p.typed.trimmed(), Qt::CaseInsensitive) != 0)
            out << cnd;
    return out;
}

void TextEditor::updateScriptCompletion(bool evenIfEmpty)
{
    // File > Options > Corrections > Auto-Completion (0.21): off, the list comes only when asked for
    if (!evenIfEmpty && !AppSettings::instance().completions) {
        hideScriptCompletion();
        return;
    }
    const QStringList list = scriptCompletions(evenIfEmpty);
    if (list.isEmpty()) {
        hideScriptCompletion();
        return;
    }
    if (!m_scriptCompleter) {
        m_scriptModel = new QStringListModel(this);
        m_scriptCompleter = new QCompleter(m_scriptModel, this);
        m_scriptCompleter->setObjectName(QStringLiteral("scriptCompleter"));
        m_scriptCompleter->setWidget(this);
        m_scriptCompleter->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
        m_scriptCompleter->setCaseSensitivity(Qt::CaseInsensitive);
        connect(m_scriptCompleter, qOverload<const QString &>(&QCompleter::activated), this,
                [this](const QString &text) { acceptScriptCompletion(text); });
    }
    m_scriptModel->setStringList(list);
    QAbstractItemView *popup = m_scriptCompleter->popup();
    popup->setFont(font());
    QRect cr = cursorRect();
    cr.moveTopLeft(viewport()->mapTo(this, cr.topLeft()));
    cr.setWidth(qMax(160, popup->sizeHintForColumn(0) + popup->verticalScrollBar()->sizeHint().width() + 12));
    m_scriptCompleter->complete(cr);
    popup->setCurrentIndex(m_scriptModel->index(0, 0));
}

void TextEditor::hideScriptCompletion()
{
    if (m_scriptCompleter && m_scriptCompleter->popup()->isVisible())
        m_scriptCompleter->popup()->hide();
}

bool TextEditor::scriptCompletionVisible() const
{
    return m_scriptCompleter && m_scriptCompleter->popup()->isVisible();
}

void TextEditor::acceptScriptCompletion(const QString &text)
{
    ScriptPiece p;
    if (!m_project || !scriptPiece(&p))
        return;
    QTextCursor c = textCursor();
    const int base = c.block().position();
    const QTextCharFormat cf = c.charFormat();
    c.beginEditBlock();
    c.setPosition(base + p.from);
    c.setPosition(base + p.to, QTextCursor::KeepAnchor);
    c.insertText(text, cf);
    c.endEditBlock();
    setTextCursor(c);
    hideScriptCompletion();
    emit scriptElementChanged(this);
}

} // namespace qb
