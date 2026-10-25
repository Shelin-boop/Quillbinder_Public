#include "ui/Zoom.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <cmath>

namespace qb {
namespace zoom {

const char *const kEditor = "editor";
const char *const kInspector = "inspector";
const char *const kBinder = "binder";
const char *const kOutliner = "outliner";
const char *const kCorkboard = "corkboard";
const char *const kComposition = "composition";
const char *const kMindMap = "mindmap";
const char *const kObjects = "objects";
const char *const kObjectsSheet = "objects.sheet";
const char *const kInspSynopsis = "inspector.synopsis";
const char *const kInspNotes = "inspector.notes";
const char *const kInspBookmarks = "inspector.bookmarkLists";
const char *const kInspPreview = "inspector.bookmarkPreview";
const char *const kInspSnapList = "inspector.snapshotList";
const char *const kInspSnapText = "inspector.snapshotText";
const char *const kInspObjectList = "inspector.objectList";
const char *const kInspObjectSheet = "inspector.objectSheet";

namespace {

// Widget properties used by the filter (names kept short and unique).
const char *const kAreaProp = "qbZoomArea";     // QString key
const char *const kOwnProp = "qbZoomOwn";       // bool: zooms itself
const char *const kSkipProp = "qbZoomSkip";     // bool: never scaled by applyFonts
const char *const kBaseProp = "qbZoomBase";     // QFont before scaling
const char *const kAppliedProp = "qbZoomApplied"; // QFont after scaling
const char *const kInheritProp = "qbZoomInherit"; // bool: the root had no font of its own

bool sameSize(const QFont &a, const QFont &b)
{
    if (a.pointSizeF() > 0 && b.pointSizeF() > 0)
        return qAbs(a.pointSizeF() - b.pointSizeF()) < 0.01;
    return a.pixelSize() == b.pixelSize();
}

QFont withSizeOf(QFont f, const QFont &sizeFrom)
{
    if (sizeFrom.pointSizeF() > 0)
        f.setPointSizeF(sizeFrom.pointSizeF());
    else if (sizeFrom.pixelSize() > 0)
        f.setPixelSize(sizeFrom.pixelSize());
    return f;
}

// The font a widget would have without any zoom: the font of the nearest widget
// above it that has a font of its own, before that one was zoomed (0.21.1: a part
// inside a zoomed area must not start from the area's zoomed font).
QFont unzoomedFont(QWidget *w)
{
    for (QWidget *x = w->parentWidget(); x; x = x->parentWidget()) {
        if (!x->testAttribute(Qt::WA_SetFont) && !x->property("qbZoomApplied").isValid())
            continue;
        const QVariant applied = x->property("qbZoomApplied");
        if (applied.isValid() && x->font() == applied.value<QFont>())
            return x->property("qbZoomBase").value<QFont>();
        return x->font();
    }
    return QApplication::font(w);
}

struct Roots {
    QList<std::pair<QPointer<QWidget>, QString>> list;
    bool hooked = false;
};
Roots &roots()
{
    static Roots r;
    return r;
}

// Scales one widget's font to `f`: `isRoot` for the widget an area was attached to.
// Remembers the font before scaling (kBaseProp) and after (kAppliedProp), so that the
// next zoom starts from the unscaled font. A font that somebody changed meanwhile
// without changing its size (bold, underline: the dots on the inspector's tabs) keeps
// the old unscaled size; one with another size (a theme) is the new starting point.
void scaleWidget(QWidget *w, bool isRoot, double f, QWidget *root)
{
    auto scaled = [f](const QFont &base) {
        QFont nf = base;
        if (base.pointSizeF() > 0)
            nf.setPointSizeF(base.pointSizeF() * f);
        else if (base.pixelSize() > 0)
            nf.setPixelSize(qMax(1, qRound(base.pixelSize() * f)));
        return nf;
    };
    QFont base;
    const QVariant applied = w->property(kAppliedProp);
    if (isRoot && w->property(kInheritProp).toBool()) {
        // the root had no font of its own: start from what it would have without zoom
        base = unzoomedFont(w);
    } else if (applied.isValid() && w->font() == applied.value<QFont>()) {
        base = w->property(kBaseProp).value<QFont>();
    } else if (applied.isValid() && sameSize(w->font(), applied.value<QFont>())) {
        base = withSizeOf(w->font(), w->property(kBaseProp).value<QFont>());
    } else if (applied.isValid() && !isRoot && root && root->property(kAppliedProp).isValid() && sameSize(w->font(), root->font())
               && sameSize(w->property(kBaseProp).value<QFont>(), root->property(kBaseProp).value<QFont>())) {
        // its size follows the area's (a font of its own without a size of its own): the
        // area was just zoomed, the widget's unscaled size is still the area's
        base = withSizeOf(w->font(), w->property(kBaseProp).value<QFont>());
    } else if (!applied.isValid() && !isRoot && root && root->property(kAppliedProp).isValid()
               && sameSize(w->font(), root->font())) {
        // made from the zoomed font of its area (a copy of it, made bold): the area's unscaled size
        base = withSizeOf(w->font(), root->property(kBaseProp).value<QFont>());
    } else {
        if (isRoot && !applied.isValid() && !w->testAttribute(Qt::WA_SetFont)) {
            w->setProperty(kInheritProp, true);
            base = unzoomedFont(w);
        } else {
            base = w->font(); // first time, or a font set meanwhile (theme)
        }
    }
    const QFont nf = scaled(base);
    if (qEnvironmentVariableIsSet("QB_ZOOM_DEBUG"))
        qInfo("zoom %s %s root=%d f=%.2f cur=%.2f base=%.2f -> %.2f", w->metaObject()->className(), qPrintable(w->objectName()), int(isRoot), f,
              w->font().pointSizeF(), base.pointSizeF(), nf.pointSizeF());
    w->setProperty(kBaseProp, base);
    // a root always gets its font set, also at 100 %: otherwise it would take the
    // zoomed font of an area around it (a part inside the inspector, 0.21.1)
    if (isRoot || w->font() != nf)
        w->setFont(nf);
    w->setProperty(kAppliedProp, w->font());
}

// one widget inside an area, made after the area got its zoom
void applyFontsTo(QWidget *root, QWidget *w, double f) { scaleWidget(w, false, f, root); }

// The function of setHandler, kept in a child object of the widget, so it goes with it.
class Handler : public QObject
{
public:
    Handler(QWidget *w, std::function<void(int)> fn) : QObject(w), fn(std::move(fn)) { setObjectName(QStringLiteral("qbZoomHandler")); }
    std::function<void(int)> fn;
};

double clamp(double f) { return qBound(kMin, std::round(f * 100.0) / 100.0, kMax); }

// A QSettings of its own each time: the settings file may be replaced from
// outside (tests, Options > Manage > Load Options from File).
struct Store {
    QSettings s;
    QSettings *operator->() { return &s; }
};
Store store() { return Store(); }

class Filter : public QObject
{
public:
    bool eventFilter(QObject *o, QEvent *e) override;
    void adopt(QWidget *w);

private:
    QPointer<QWidget> m_target;   // where the last wheel steps went
    int m_rest = 0;               // eighths of a degree not yet used (touchpads send small steps)
};

QString windowKey(QWidget *top)
{
    QString name = top->objectName();
    if (name.isEmpty())
        name = QString::fromLatin1(top->metaObject()->className()).section(QLatin1String("::"), -1);
    return QStringLiteral("window/") + name;
}

// May the whole window be zoomed? Not the main window, not menus, popups, tool tips.
bool windowZoomable(QWidget *top)
{
    if (!top || qobject_cast<QMainWindow *>(top) || qobject_cast<QMenu *>(top))
        return false;
    const Qt::WindowType t = top->windowType();
    if (t == Qt::Popup || t == Qt::ToolTip || t == Qt::SplashScreen)
        return false;
    return !top->property(kSkipProp).toBool();
}

// A widget made after its area was zoomed, with a font of its own (a bold label of
// the Objects sheet, a card of the comments): it gets the area's zoom at once
// instead of waiting for the next change of the zoom (0.21.1).
void Filter::adopt(QWidget *w)
{
    if (!w->testAttribute(Qt::WA_SetFont) || w->property(kAppliedProp).isValid() || w->isWindow())
        return;
    for (QWidget *x = w->parentWidget(); x; x = x->parentWidget()) {
        if (x->property(kSkipProp).toBool() || x->property(kOwnProp).toBool()
            || x->findChild<Handler *>(QStringLiteral("qbZoomHandler"), Qt::FindDirectChildrenOnly))
            return;
        if (!x->property("qbZoomAttached").toBool()) {
            if (!x->property(kAreaProp).toString().isEmpty())
                return; // an area that zooms by other means (a text, a snapshot)
            continue;
        }
        double f = 1.0;
        for (const auto &[root, k] : roots().list)
            if (root == x)
                f = factor(k);
        if (!qFuzzyCompare(f, 1.0))
            applyFontsTo(x, w, f);
        return;
    }
}

bool Filter::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::Polish) {
        if (auto *w = qobject_cast<QWidget *>(o))
            adopt(w);
        return false;
    }
    if (e->type() == QEvent::Show) {
        // a window that was zoomed before opens with that zoom
        auto *w = qobject_cast<QWidget *>(o);
        if (w && w->isWindow() && !w->property("qbZoomShown").toBool() && windowZoomable(w)) {
            w->setProperty("qbZoomShown", true);
            const QString key = windowKey(w);
            if (store()->contains(QStringLiteral("zoom/") + key))
                attachFonts(w, key);
        }
        return false;
    }
    if (e->type() != QEvent::Wheel)
        return false;
    auto *we = static_cast<QWheelEvent *>(e);
    const Qt::KeyboardModifiers mods = we->modifiers();
    if (!(mods & Qt::ControlModifier) || (mods & (Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier)))
        return false;
    auto *w = qobject_cast<QWidget *>(o);
    if (!w)
        return false;
    // the nearest marked widget, from the one under the mouse up to its window
    QWidget *target = nullptr;
    std::function<void(int)> fn;
    for (QWidget *x = w; x; x = x->parentWidget()) {
        if (x->property(kOwnProp).toBool())
            return false; // it zooms itself (the mind map board)
        if (auto *h = x->findChild<Handler *>(QStringLiteral("qbZoomHandler"), Qt::FindDirectChildrenOnly)) {
            target = x;
            fn = h->fn;
            break;
        }
        const QString key = x->property(kAreaProp).toString();
        if (!key.isEmpty()) {
            target = x;
            fn = [key](int steps) { setFactor(key, factor(key) + steps * kStep); };
            break;
        }
        if (x->isWindow()) {
            if (!windowZoomable(x))
                return false;
            target = x;
            const QString key = windowKey(x);
            QPointer<QWidget> win(x);
            fn = [key, win](int steps) {
                setFactor(key, factor(key) + steps * kStep);
                if (win && !win->property("qbZoomAttached").toBool())
                    attachFonts(win, key);
            };
            break;
        }
    }
    if (!target || !fn)
        return false;
    if (target != m_target) {
        m_target = target;
        m_rest = 0;
    }
    const QPoint d = we->angleDelta();
    m_rest += d.y() != 0 ? d.y() : d.x();
    const int steps = m_rest / 120;
    m_rest -= steps * 120;
    if (steps != 0)
        fn(steps);
    we->accept();
    return true; // the widget itself does not see it (QTextEdit would change its font)
}

void reapply(const QString &key)
{
    for (auto &[w, k] : roots().list)
        if (w && (key.isEmpty() || k == key))
            applyFonts(w, factor(k));
}

} // namespace

QStringList areas()
{
    QStringList l;
    for (const char *k : {kEditor, kInspector, kInspSynopsis, kInspNotes, kInspBookmarks, kInspPreview, kInspSnapList, kInspSnapText,
                          kInspObjectList, kInspObjectSheet, kBinder, kOutliner, kCorkboard, kComposition, kMindMap, kObjects, kObjectsSheet})
        l << QLatin1String(k);
    return l;
}

// The part a key belongs to, and what it fell back to before it had a zoom of its own.
static QString partFallback(const QString &key)
{
    if (key.startsWith(QLatin1String("inspector.")))
        return QLatin1String(kInspector);
    if (key == QLatin1String(kObjectsSheet))
        return QLatin1String(kObjects);
    return QString();
}

// the tab of an inspector part (for the zoom it had when each tab had its own)
static QString partTab(const QString &key)
{
    if (key == QLatin1String(kInspSynopsis) || key == QLatin1String(kInspNotes))
        return QStringLiteral("notes");
    if (key == QLatin1String(kInspBookmarks) || key == QLatin1String(kInspPreview))
        return QStringLiteral("bookmarks");
    if (key == QLatin1String(kInspSnapList) || key == QLatin1String(kInspSnapText))
        return QStringLiteral("snapshots");
    if (key == QLatin1String(kInspObjectList) || key == QLatin1String(kInspObjectSheet))
        return QStringLiteral("objects");
    return QString();
}

void migrateParts()
{
    Store st = store();
    if (st->value(QStringLiteral("zoomPartsMigrated")).toBool())
        return;
    const bool per = st->value(QStringLiteral("zoomInspectorPerTab"), false).toBool();
    for (const QString &k : areas()) {
        const QString fb = partFallback(k);
        if (fb.isEmpty())
            continue;
        QString old = fb;
        if (per && fb == QLatin1String(kInspector) && !partTab(k).isEmpty()
            && st->contains(QStringLiteral("zoom/inspector/") + partTab(k)))
            old = QStringLiteral("inspector/") + partTab(k);
        if (st->contains(QStringLiteral("zoom/") + old) && !st->contains(QStringLiteral("zoom/") + k))
            st->setValue(QStringLiteral("zoom/") + k, st->value(QStringLiteral("zoom/") + old));
    }
    st->setValue(QStringLiteral("zoomPartsMigrated"), true);
}

QString areaName(const QString &key)
{
    if (key == QLatin1String(kEditor))
        return QObject::tr("Main Text Editors");
    if (key == QLatin1String(kInspector))
        return QObject::tr("Inspector (Tabs, Metadata, Comments)");
    if (key == QLatin1String(kBinder))
        return QObject::tr("Binder");
    if (key == QLatin1String(kOutliner))
        return QObject::tr("Outliner");
    if (key == QLatin1String(kCorkboard))
        return QObject::tr("Corkboard");
    if (key == QLatin1String(kComposition))
        return QObject::tr("Composition Mode");
    if (key == QLatin1String(kMindMap))
        return QObject::tr("Mind Maps (until a board is zoomed)");
    if (key == QLatin1String(kObjects))
        return QObject::tr("Objects Window: Categories and List");
    if (key == QLatin1String(kObjectsSheet))
        return QObject::tr("Objects Window: Sheet");
    if (key == QLatin1String(kInspSynopsis))
        return QObject::tr("Inspector: Synopsis");
    if (key == QLatin1String(kInspNotes))
        return QObject::tr("Inspector: Notes");
    if (key == QLatin1String(kInspBookmarks))
        return QObject::tr("Inspector: Bookmark Lists");
    if (key == QLatin1String(kInspPreview))
        return QObject::tr("Inspector: Bookmarked Text");
    if (key == QLatin1String(kInspSnapList))
        return QObject::tr("Inspector: Snapshot List");
    if (key == QLatin1String(kInspSnapText))
        return QObject::tr("Inspector: Snapshot Text");
    if (key == QLatin1String(kInspObjectList))
        return QObject::tr("Inspector: Objects (Categories and List)");
    if (key == QLatin1String(kInspObjectSheet))
        return QObject::tr("Inspector: Object Sheet");
    if (key.startsWith(QLatin1String("inspector/")))
        return QObject::tr("Inspector: %1").arg(key.mid(10));
    return key;
}

double defaultFactor(const QString &key)
{
    // an inspector tab without a default of its own takes the inspector's
    QString k = key;
    if (k.startsWith(QLatin1String("inspector/")) && !store()->contains(QStringLiteral("zoomDefault/") + k))
        k = QLatin1String(kInspector);
    // a part without a default of its own takes the default of its area (0.21.1)
    if (!partFallback(k).isEmpty() && !store()->contains(QStringLiteral("zoomDefault/") + k))
        k = partFallback(k);
    if (k.startsWith(QLatin1String("window/")))
        return 1.0;
    return clamp(store()->value(QStringLiteral("zoomDefault/") + k, 1.0).toDouble());
}

void setDefaultFactor(const QString &key, double f)
{
    store()->setValue(QStringLiteral("zoomDefault/") + key, clamp(f));
}

double factor(const QString &key)
{
    const QVariant v = store()->value(QStringLiteral("zoom/") + key);
    if (v.isValid())
        return clamp(v.toDouble());
    return defaultFactor(key);
}

void setFactor(const QString &key, double f)
{
    f = clamp(f);
    const bool same = store()->contains(QStringLiteral("zoom/") + key) && qFuzzyCompare(factor(key), f);
    store()->setValue(QStringLiteral("zoom/") + key, f);
    if (!same)
        emit notifier().changed(key, f);
}

void reset(const QString &key)
{
    setFactor(key, defaultFactor(key));
}

bool perTab() { return store()->value(QStringLiteral("zoomInspectorPerTab"), false).toBool(); }

void setPerTab(bool on)
{
    if (on == perTab())
        return;
    store()->setValue(QStringLiteral("zoomInspectorPerTab"), on);
    emit notifier().modeChanged();
}

QString inspectorKey(const QString &tab)
{
    return perTab() && !tab.isEmpty() ? QStringLiteral("inspector/") + tab : QString::fromLatin1(kInspector);
}

Notifier &notifier()
{
    static Notifier n;
    return n;
}

void install()
{
    static Filter *f = nullptr;
    if (f || !qApp)
        return;
    migrateParts();
    f = new Filter;
    f->setParent(qApp);
    qApp->installEventFilter(f);
}

void mark(QWidget *w, const QString &key)
{
    if (w)
        w->setProperty(kAreaProp, key);
}

void setHandler(QWidget *w, const std::function<void(int)> &handler)
{
    if (!w)
        return;
    if (auto *old = w->findChild<Handler *>(QStringLiteral("qbZoomHandler"), Qt::FindDirectChildrenOnly))
        delete old;
    new Handler(w, handler);
}

void markOwn(QWidget *w)
{
    if (w)
        w->setProperty(kOwnProp, true);
}

void skip(QWidget *w)
{
    if (w)
        w->setProperty(kSkipProp, true);
}

void applyFonts(QWidget *root, double f)
{
    if (!root)
        return;
    scaleWidget(root, true, f, nullptr);
    for (QWidget *c : root->findChildren<QWidget *>()) {
        if (!c->testAttribute(Qt::WA_SetFont) || c->isWindow())
            continue;
        // not inside a part that zooms by itself or belongs to another area
        bool other = false;
        for (QWidget *x = c; x && x != root; x = x->parentWidget())
            if (x->property(kSkipProp).toBool() || x->property(kOwnProp).toBool() || !x->property(kAreaProp).toString().isEmpty()
                || x->findChild<Handler *>(QStringLiteral("qbZoomHandler"), Qt::FindDirectChildrenOnly)) {
                other = true;
                break;
            }
        if (!other)
            scaleWidget(c, false, f, root);
    }
}

void detachFonts(QWidget *root)
{
    if (!root)
        return;
    Roots &r = roots();
    for (auto it = r.list.begin(); it != r.list.end();) {
        if (!it->first || it->first == root)
            it = r.list.erase(it);
        else
            ++it;
    }
    applyFonts(root, 1.0);
    root->setProperty("qbZoomAttached", false);
}

void unmark(QWidget *w)
{
    if (w)
        w->setProperty(kAreaProp, QVariant());
}

void attachFonts(QWidget *root, const QString &key)
{
    if (!root)
        return;
    Roots &r = roots();
    if (!r.hooked) {
        r.hooked = true;
        QObject::connect(&notifier(), &Notifier::changed, qApp, [](const QString &k, double) { reapply(k); });
        // the theme sets its fonts first; the zoom goes on top of them afterwards
        QObject::connect(&theme::Notifier::instance(), &theme::Notifier::changed, qApp,
                         [] { QTimer::singleShot(0, qApp, [] { reapply(QString()); }); });
    }
    for (auto it = r.list.begin(); it != r.list.end();) {
        if (!it->first || it->first == root)
            it = r.list.erase(it);
        else
            ++it;
    }
    r.list.append({QPointer<QWidget>(root), key});
    root->setProperty("qbZoomAttached", true);
    applyFonts(root, factor(key));
}

} // namespace zoom
} // namespace qb
