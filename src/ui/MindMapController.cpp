#include "ui/MindMapController.h"
#include "core/IconCatalog.h"
#include "ui/IconDialogs.h"
#include "ui/ColorPalette.h"
#include "ui/MindMapHelp.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "import/FileImporter.h"
#include "text/RtfIO.h"
#include "ui/MindMapDoc.h"
#include "ui/MindMapScene.h"
#include "ui/MindMapUi.h"
#include "ui/MindMapView.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QDialog>
#include <QDesktopServices>
#include <QTimer>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPageSetupDialog>
#include <QPainter>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QUndoStack>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

namespace qb {

using namespace mindmap;

static const char *kBoardMime = "application/x-quillbinder-mindmap";
static const char *kStyleMime = "application/x-quillbinder-mindmap-style";

MindMapController::MindMapController(MindMapDoc *doc, MindMapView *view, QWidget *owner, bool window)
    : QObject(owner), m_doc(doc), m_view(view), m_owner(owner), m_window(window)
{
    createActions();
    view->setController(this);
    if (!window)
        view->setKeyActions(actions());
    connect(scene(), &MindMapScene::selectionOrderChanged, this, &MindMapController::updateActions);
    connect(scene(), &MindMapScene::editingChanged, this, &MindMapController::updateActions);
    connect(doc, &MindMapDoc::boardChanged, this, &MindMapController::updateActions);
    connect(view, &MindMapView::movementModeChanged, this, [this](bool on) { m_a[QStringLiteral("mm_movement")]->setChecked(on); });
    connect(view, &MindMapView::contextMenuRequested, this, [this](const QPoint &g, const QPointF &) {
        QMenu menu;
        const auto line = m_view->contextLine();
        if (line.first && !m_doc->locked())
            fillLineMenu(&menu, line.first, line.second);
        else
            fillContextMenu(&menu);
        menu.exec(g);
    });
    updateActions();
}

MindMapScene *MindMapController::scene() const { return m_doc->scene(); }
Board &MindMapController::board() const { return m_doc->board; }

QAction *MindMapController::add(const QString &key, const QString &text, const QKeySequence &ks, const QString &icon)
{
    auto *a = new QAction(text, this);
    a->setObjectName(key);
    if (!ks.isEmpty())
        a->setShortcut(ks);
    if (!icon.isEmpty())
        a->setIcon(icons::action(icon));
    // in the window the shortcuts work as usual; in the inspector the view hands the keys on
    a->setShortcutContext(m_window ? Qt::WindowShortcut : Qt::WidgetShortcut);
    if (m_window && m_owner)
        m_owner->addAction(a);
    m_a.insert(key, a);
    return a;
}

void MindMapController::createActions()
{
    auto on = [this](QAction *a, auto fn) { connect(a, &QAction::triggered, this, fn); };
    // ---- file
    on(add(QStringLiteral("mm_exportPdf"), tr("&PDF…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), QStringLiteral("application-pdf")),
       [this] { exportDialog(0); });
    on(add(QStringLiteral("mm_exportPng"), tr("P&NG…"), QKeySequence(), QStringLiteral("image-x-generic")), [this] { exportDialog(1); });
    on(add(QStringLiteral("mm_exportText"), tr("Plain &Text (.txt)…")), [this] { exportDialog(2); });
    on(add(QStringLiteral("mm_exportRtf"), tr("&Rich Text (.rtf)…")), [this] { exportDialog(3); });
    on(add(QStringLiteral("mm_exportList"), tr("Plain Text &List (.txt)…")), [this] { exportDialog(4); });
    on(add(QStringLiteral("mm_exportOpml"), tr("&Outline Markup (.opml)…")), [this] { exportDialog(5); });
    on(add(QStringLiteral("mm_exportImages"), tr("&Images…")), [this] { exportDialog(6); });
    on(add(QStringLiteral("mm_exportScap"), tr("&Scapple File (.scap)…")), [this] { exportDialog(7); });
    on(add(QStringLiteral("mm_importScap"), tr("&Import Mind Map or Scapple File…")), [this] { importBoard(); });
    QAction *lock = add(QStringLiteral("mm_lock"), tr("Prevent &Editing"), QKeySequence(), QStringLiteral("object-locked"));
    lock->setCheckable(true);
    on(lock, [this](bool b) {
        // the lock itself is a change of the board (and can be undone)
        m_doc->begin(tr("Prevent Editing"));
        board().settings.locked = b;
        m_doc->commit();
        updateActions();
    });
    on(add(QStringLiteral("mm_pageSetup"), tr("Page Set&up…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P)), [this] { pageSetup(); });
    on(add(QStringLiteral("mm_printPreview"), tr("Print Pre&view…"), QKeySequence(), QStringLiteral("document-print-preview")),
       [this] { printPreview(); });
    on(add(QStringLiteral("mm_print"), tr("&Print…"), QKeySequence::Print, QStringLiteral("document-print")), [this] { print(); });
    on(add(QStringLiteral("mm_options"), tr("Mind Map &Options…"), QKeySequence(), QStringLiteral("configure")), [this] { showOptions(); });
    on(add(QStringLiteral("mm_help"), tr("Mind Map &Help"), QKeySequence(), QStringLiteral("help-contents")),
       [this] { mindmaphelp::showHelp(m_owner); });
    on(add(QStringLiteral("mm_example"), tr("Open &Example Board"), QKeySequence(), QStringLiteral("qb-mindmap")),
       [this] { mindmaphelp::openExampleBoard(m_owner); });
    on(add(QStringLiteral("mm_openWindow"), tr("Open in &Window"), QKeySequence(), QStringLiteral("window-new")),
       [this] { emit openWindowRequested(); });

    // ---- edit
    QAction *undo = add(QStringLiteral("mm_undo"), tr("&Undo"), QKeySequence::Undo, QStringLiteral("edit-undo"));
    on(undo, [this] {
        if (NoteItem *n = scene()->editingNote())
            n->textItem()->document()->undo();
        else
            m_doc->undoStack()->undo();
    });
    QAction *redo = add(QStringLiteral("mm_redo"), tr("&Redo"), QKeySequence::Redo, QStringLiteral("edit-redo"));
    on(redo, [this] {
        if (NoteItem *n = scene()->editingNote())
            n->textItem()->document()->redo();
        else
            m_doc->undoStack()->redo();
    });
    on(add(QStringLiteral("mm_cut"), tr("Cu&t"), QKeySequence::Cut, QStringLiteral("edit-cut")), [this] { copy(true); });
    on(add(QStringLiteral("mm_copy"), tr("&Copy"), QKeySequence::Copy, QStringLiteral("edit-copy")), [this] { copy(false); });
    on(add(QStringLiteral("mm_paste"), tr("&Paste"), QKeySequence::Paste, QStringLiteral("edit-paste")),
       [this] { paste(m_view->visibleCenter(), false); });
    on(add(QStringLiteral("mm_delete"), tr("&Delete"), QKeySequence(Qt::Key_Delete), QStringLiteral("edit-delete")),
       [this] { deleteSelection(); });
    QAction *mv = add(QStringLiteral("mm_movement"), tr("Arrow Keys &Move Notes"), QKeySequence(Qt::Key_M));
    mv->setCheckable(true);
    on(mv, [this](bool b) { m_view->setMovementMode(b); });
    on(add(QStringLiteral("mm_selectAll"), tr("Select &All"), QKeySequence::SelectAll), [this] {
        if (NoteItem *n = scene()->editingNote()) {
            QTextCursor c = n->textItem()->textCursor();
            c.select(QTextCursor::Document);
            n->textItem()->setTextCursor(c);
            return;
        }
        QList<int> ids;
        for (const Note &n : board().notes)
            ids << n.id;
        for (const Shape &s : board().shapes)
            ids << s.id;
        scene()->selectIds(ids);
    });
    on(add(QStringLiteral("mm_selStacked"), tr("Notes Stacked &Below")), [this] { selectStackedBelow(); });
    on(add(QStringLiteral("mm_selOverlap"), tr("Notes &Overlapping Selected Shapes"), QKeySequence(Qt::ALT | Qt::Key_A)),
       [this] { selectOverlapping(); });
    on(add(QStringLiteral("mm_selConnected"), tr("&Connected Notes"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A)),
       [this] { selectConnected(false); });
    on(add(QStringLiteral("mm_selCluster"), tr("Connected C&lusters"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_A)),
       [this] { selectConnected(true); });
    on(add(QStringLiteral("mm_invert"), tr("&Invert Selection"), QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_I)), [this] { invertSelection(); });
    on(add(QStringLiteral("mm_deselect"), tr("D&eselect"), QKeySequence(Qt::CTRL | Qt::Key_D)), [this] {
        scene()->endEditing();
        scene()->selectIds({});
    });
    on(add(QStringLiteral("mm_link"), tr("Add &Link…"), QKeySequence(Qt::CTRL | Qt::Key_K), QStringLiteral("insert-link")),
       [this] { addLink(); });
    on(add(QStringLiteral("mm_find"), tr("&Find…"), QKeySequence::Find, QStringLiteral("edit-find")), [this] { emit findRequested(); });
    on(add(QStringLiteral("mm_findNext"), tr("Find &Next"), QKeySequence(Qt::Key_F3)), [this] {
        if (m_find)
            m_find->findNext(false);
    });
    on(add(QStringLiteral("mm_findPrev"), tr("Find Pre&vious"), QKeySequence(Qt::SHIFT | Qt::Key_F3)), [this] {
        if (m_find)
            m_find->findNext(true);
    });
    on(add(QStringLiteral("mm_useSelection"), tr("&Use Selection for Find"), QKeySequence(Qt::CTRL | Qt::Key_E)),
       [this] { useSelectionForFind(); });
    on(add(QStringLiteral("mm_jump"), tr("&Jump to Selection"), QKeySequence(Qt::CTRL | Qt::Key_J)), [this] { jumpToSelection(); });

    // ---- view
    QAction *insp = add(QStringLiteral("mm_inspector"), tr("Show &Inspector"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I),
                        QStringLiteral("view-right-close"));
    insp->setCheckable(true);
    insp->setChecked(true);
    on(insp, [this](bool b) { emit inspectorToggled(b); });
    QAction *foot = add(QStringLiteral("mm_footer"), tr("Show &Footer Bar"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Slash));
    foot->setCheckable(true);
    on(foot, [this](bool b) {
        board().settings.footer = b;
        m_doc->save();
        emit footerToggled(b);
    });
    QAction *guides = add(QStringLiteral("mm_pageGuides"), tr("&Page Guides"));
    guides->setCheckable(true);
    on(guides, [this](bool b) {
        board().settings.pageGuides = b;
        m_doc->save();
        m_view->viewport()->update();
    });
    on(add(QStringLiteral("mm_actualSize"), tr("&Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_0), QStringLiteral("zoom-original")),
       [this] { m_view->actualSize(); });
    on(add(QStringLiteral("mm_zoomFit"), tr("Zoom to &Fit"), QKeySequence(Qt::CTRL | Qt::Key_9), QStringLiteral("zoom-fit-best")),
       [this] { m_view->zoomToFit(); });
    QAction *zin = add(QStringLiteral("mm_zoomIn"), tr("Zoom &In"), QKeySequence::ZoomIn, QStringLiteral("zoom-in"));
    zin->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    on(zin, [this] { m_view->zoomBy(1.2); });
    on(add(QStringLiteral("mm_zoomOut"), tr("Zoom &Out"), QKeySequence::ZoomOut, QStringLiteral("zoom-out")), [this] { m_view->zoomBy(1 / 1.2); });
    on(add(QStringLiteral("mm_zoomSelection"), tr("Zoom to &Selection"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_8)),
       [this] { zoomToSelection(); });
    on(add(QStringLiteral("mm_zoomRandom"), tr("Zoom to &Random"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R)), [this] { zoomToRandom(); });

    // ---- notes
    on(add(QStringLiteral("mm_newNote"), tr("&New Note"), QKeySequence(Qt::CTRL | Qt::Key_Return), QStringLiteral("list-add")),
       [this] { newNote(); });
    on(add(QStringLiteral("mm_newShape"), tr("New &Background Shape"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G),
           QStringLiteral("qb-mm-shape")),
       [this] { newShape(); });
    struct Dir {
        const char *key;
        const char *text;
        int dx, dy;
        Qt::Key k;
    };
    for (const Dir &d : {Dir{"Left", QT_TR_NOOP("On &Left"), -1, 0, Qt::Key_Left}, Dir{"Right", QT_TR_NOOP("On &Right"), 1, 0, Qt::Key_Right},
                         Dir{"Above", QT_TR_NOOP("&Above"), 0, -1, Qt::Key_Up}, Dir{"Below", QT_TR_NOOP("&Below"), 0, 1, Qt::Key_Down}}) {
        const int dx = d.dx, dy = d.dy;
        on(add(QStringLiteral("mm_conn%1").arg(QLatin1String(d.key)), tr(d.text), QKeySequence(Qt::ALT | d.k)),
           [this, dx, dy] { newConnectedNote(dx, dy, false); });
        on(add(QStringLiteral("mm_connArrow%1").arg(QLatin1String(d.key)), tr("%1 With Arrow").arg(tr(d.text)),
               QKeySequence(Qt::ALT | Qt::SHIFT | d.k)),
           [this, dx, dy] { newConnectedNote(dx, dy, true); });
    }
    on(add(QStringLiteral("mm_connect"), tr("&Connect"), QKeySequence(Qt::CTRL | Qt::Key_Period)), [this] { connectSelected(0); });
    on(add(QStringLiteral("mm_connectAll"), tr("Connect A&ll")), [this] { connectSelected(1); });
    on(add(QStringLiteral("mm_connectArrow"), tr("Connect &With Arrow"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Period)),
       [this] { connectSelected(2); });
    on(add(QStringLiteral("mm_removeArrows"), tr("Remove &Arrows"), QKeySequence(Qt::CTRL | Qt::Key_Comma)), [this] { connectSelected(3); });
    on(add(QStringLiteral("mm_disconnect"), tr("&Disconnect"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Period)),
       [this] { connectSelected(4); });
    on(add(QStringLiteral("mm_label"), tr("Connection La&bel…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L)), [this] {
        const QList<int> s = scene()->selectedIds();
        if (s.size() == 2)
            editLineLabel(s[0], s[1]);
    });
    QAction *mag = add(QStringLiteral("mm_magnetic"), tr("&Magnetic"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    mag->setCheckable(true);
    on(mag, [this] { toggleMagnetic(); });
    on(add(QStringLiteral("mm_stack"), tr("&Stack"), QKeySequence(Qt::CTRL | Qt::Key_Apostrophe)), [this] { stack(); });
    on(add(QStringLiteral("mm_autoFit"), tr("Auto-&Fit")), [this] { autoFit(); });
    const QStringList alignNames = {tr("&Left Edges"), tr("&Right Edges"), tr("&Top Edges"), tr("&Bottom Edges"), tr("&Horizontal Centers"),
                                    tr("&Vertical Centers")};
    for (int i = 0; i < alignNames.size(); ++i)
        on(add(QStringLiteral("mm_align%1").arg(i), alignNames[i]), [this, i] { align(i); });
    on(add(QStringLiteral("mm_centerShape"), tr("&Center in Background Shape")), [this] { centerInShape(0); });
    on(add(QStringLiteral("mm_centerShapeH"), tr("Center Hori&zontally in Background Shape")), [this] { centerInShape(1); });
    on(add(QStringLiteral("mm_centerShapeV"), tr("Center Verticall&y in Background Shape")), [this] { centerInShape(2); });
    on(add(QStringLiteral("mm_distH"), tr("&Horizontally")), [this] { distribute(true); });
    on(add(QStringLiteral("mm_distV"), tr("&Vertically")), [this] { distribute(false); });
    on(add(QStringLiteral("mm_sameWidth"), tr("Make Same &Width")), [this] { makeSame(true); });
    on(add(QStringLiteral("mm_sameHeight"), tr("Make Same &Height")), [this] { makeSame(false); });
    on(add(QStringLiteral("mm_split"), tr("S&plit"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K)), [this] { splitNote(); });
    on(add(QStringLiteral("mm_merge"), tr("Mer&ge")), [this] { mergeNotes(); });
    on(add(QStringLiteral("mm_front"), tr("Bring to &Front"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BraceRight)), [this] {
        const QList<int> ids = targets();
        m_doc->change(tr("Bring to Front"), [&] { board().bringToFront(ids); });
    });
    on(add(QStringLiteral("mm_back"), tr("Send to Bac&k"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BraceLeft)), [this] {
        const QList<int> ids = targets();
        m_doc->change(tr("Send to Back"), [&] { board().sendToBack(ids); });
    });
    on(add(QStringLiteral("mm_toBinder"), tr("Add Notes to the B&inder"), QKeySequence(), QStringLiteral("document-new")),
       [this] { addToBinder(); });

    // ---- format
    on(add(QStringLiteral("mm_newStyle"), tr("&New Note Style From Selection…")), [this] { newStyleFromSelection(); });
    on(add(QStringLiteral("mm_copyStyle"), tr("&Copy Note Style"), QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_C)), [this] { copyStyle(); });
    on(add(QStringLiteral("mm_pasteStyle"), tr("&Paste Note Style"), QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_V)), [this] { pasteStyle(); });
    on(add(QStringLiteral("mm_importStyles"), tr("&Import Note Styles…")), [this] { importStyles(); });
    on(add(QStringLiteral("mm_defaultStyle"), tr("&Default"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_0)), [this] { applyStyleByIndex(0); });
    for (int i = 1; i <= 9; ++i)
        on(add(QStringLiteral("mm_style%1").arg(i), QString(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key(Qt::Key_0 + i))),
           [this, i] { applyStyleByIndex(i); });
    on(add(QStringLiteral("mm_bold"), tr("&Bold"), QKeySequence::Bold, QStringLiteral("format-text-bold")), [this] { toggleFont(0); });
    on(add(QStringLiteral("mm_italic"), tr("&Italic"), QKeySequence::Italic, QStringLiteral("format-text-italic")), [this] { toggleFont(1); });
    on(add(QStringLiteral("mm_underline"), tr("&Underline"), QKeySequence::Underline, QStringLiteral("format-text-underline")),
       [this] { toggleFont(2); });
    on(add(QStringLiteral("mm_strike"), tr("&Strikethrough"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Minus),
           QStringLiteral("format-text-strikethrough")),
       [this] { toggleFont(3); });
    on(add(QStringLiteral("mm_bigger"), tr("Bi&gger"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Greater), QStringLiteral("format-font-size-more")),
       [this] { forNotes(tr("Bigger"), [](Note &n) { n.fontSize = qMin(96.0, n.fontSize + 1); }); });
    m_a[QStringLiteral("mm_bigger")]->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Greater), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Period)});
    on(add(QStringLiteral("mm_smaller"), tr("S&maller"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Less), QStringLiteral("format-font-size-less")),
       [this] { forNotes(tr("Smaller"), [](Note &n) { n.fontSize = qMax(6.0, n.fontSize - 1); }); });
    m_a[QStringLiteral("mm_smaller")]->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Less), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Comma)});
    auto *alignGroup = new QActionGroup(this);
    const QList<std::pair<Align, QString>> aligns = {{Align::Left, tr("Align &Left")}, {Align::Center, tr("&Center")},
                                                      {Align::Justify, tr("&Justify")}, {Align::Right, tr("Align &Right")}};
    const QList<QKeySequence> alignKeys = {QKeySequence(Qt::CTRL | Qt::Key_BraceLeft), QKeySequence(Qt::CTRL | Qt::Key_Bar),
                                           QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Bar), QKeySequence(Qt::CTRL | Qt::Key_BraceRight)};
    for (int i = 0; i < aligns.size(); ++i) {
        const Align al = aligns[i].first;
        QAction *a = add(QStringLiteral("mm_textAlign%1").arg(i), aligns[i].second, alignKeys[i]);
        a->setCheckable(true);
        alignGroup->addAction(a);
        on(a, [this, al] { setAlignment(al); });
    }
    on(add(QStringLiteral("mm_indent"), tr("&Increase Indent Level"), QKeySequence(), QStringLiteral("format-indent-more")),
       [this] { indent(1); });
    on(add(QStringLiteral("mm_outdent"), tr("&Decrease Indent Level"), QKeySequence(), QStringLiteral("format-indent-less")),
       [this] { indent(-1); });
    on(add(QStringLiteral("mm_noIndent"), tr("&Remove Indents")), [this] { forNotes(tr("Remove Indents"), [](Note &n) { n.indent = 0; }); });
    on(add(QStringLiteral("mm_textColor"), tr("Change &Text Color…"), QKeySequence(), QStringLiteral("format-text-color")),
       [this] { chooseColor(0); });
    on(add(QStringLiteral("mm_clearText"), tr("C&lear Text Color")), [this] { clearColor(0); });
    on(add(QStringLiteral("mm_fillColor"), tr("Change &Fill Color…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
           QStringLiteral("format-fill-color")),
       [this] { chooseColor(1); });
    on(add(QStringLiteral("mm_clearFill"), tr("Cl&ear Fill Color")), [this] { clearColor(1); });
    on(add(QStringLiteral("mm_borderColor"), tr("Change &Border Color…"), QKeySequence(), QStringLiteral("format-stroke-color")),
       [this] { chooseColor(2); });
    on(add(QStringLiteral("mm_clearBorder"), tr("Clea&r Border Color")), [this] { clearColor(2); });
    auto *weights = new QActionGroup(this);
    for (int w = 0; w <= 5; ++w) {
        QAction *a = add(QStringLiteral("mm_border%1").arg(w), w == 0 ? tr("&No Border") : tr("&%1 Pixel(s)").arg(w));
        if (w == 1)
            a->setText(tr("&1 Pixel"));
        else if (w > 1)
            a->setText(tr("&%1 Pixels").arg(w));
        a->setCheckable(true);
        weights->addAction(a);
        on(a, [this, w] { setBorderWeight(w); });
    }
    auto *styleGroup = new QActionGroup(this);
    // names of the shapes as the writer sees them (0.21: "Rectangle", "Rounded Rectangle"; the file keeps Scapple's words)
    const QList<std::pair<BorderStyle, QString>> bstyles = {{BorderStyle::Rounded, tr("R&ounded Rectangle")}, {BorderStyle::Square, tr("&Rectangle")},
                                                            {BorderStyle::Jagged, tr("&Jagged")}, {BorderStyle::Cloud, tr("&Cloud")}};
    for (int i = 0; i < bstyles.size(); ++i) {
        const BorderStyle bs = bstyles[i].first;
        QAction *a = add(QStringLiteral("mm_borderStyle%1").arg(i), bstyles[i].second);
        a->setCheckable(true);
        styleGroup->addAction(a);
        on(a, [this, bs] { setBorderStyle(bs); });
    }
    auto *dirGroup = new QActionGroup(this);
    QAction *ltr = add(QStringLiteral("mm_ltr"), tr("&Left to Right"));
    QAction *rtl = add(QStringLiteral("mm_rtl"), tr("&Right to Left"));
    for (QAction *a : {ltr, rtl}) {
        a->setCheckable(true);
        dirGroup->addAction(a);
    }
    on(ltr, [this] { setWritingDirection(false); });
    on(rtl, [this] { setWritingDirection(true); });
    QAction *shadow = add(QStringLiteral("mm_shadow"), tr("S&hadow"));
    shadow->setCheckable(true);
    on(shadow, [this] { toggleShadow(); });
    QAction *fade = add(QStringLiteral("mm_fade"), tr("F&ade"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    fade->setCheckable(true);
    on(fade, [this] { toggleFade(); });
}

// ------------------------------------------------------------------ menus

void MindMapController::fillFileMenu(QMenu *m)
{
    QMenu *ex = m->addMenu(icons::action(QStringLiteral("document-export")), tr("&Export"));
    for (const char *k : {"mm_exportPdf", "mm_exportPng", "", "mm_exportText", "mm_exportRtf", "mm_exportList", "mm_exportOpml", "",
                          "mm_exportImages", "mm_exportScap"}) {
        if (!*k)
            ex->addSeparator();
        else
            ex->addAction(m_a[QLatin1String(k)]);
    }
    m->addAction(m_a[QStringLiteral("mm_importScap")]);
    m->addSeparator();
    m->addAction(m_a[QStringLiteral("mm_lock")]);
    m->addSeparator();
    for (const char *k : {"mm_pageSetup", "mm_printPreview", "mm_print"})
        m->addAction(m_a[QLatin1String(k)]);
    m->addSeparator();
    m->addAction(m_a[QStringLiteral("mm_options")]);
}

void MindMapController::fillHelpMenu(QMenu *m)
{
    m->addAction(m_a[QStringLiteral("mm_help")]);
    m->addAction(m_a[QStringLiteral("mm_example")]);
}

void MindMapController::fillEditMenu(QMenu *m)
{
    for (const char *k : {"mm_undo", "mm_redo", "", "mm_cut", "mm_copy", "mm_paste", "mm_delete", "", "mm_movement", "", "mm_selectAll"}) {
        if (!*k)
            m->addSeparator();
        else
            m->addAction(m_a[QLatin1String(k)]);
    }
    QMenu *sel = m->addMenu(tr("&Select"));
    for (const char *k : {"mm_selStacked", "mm_selOverlap", "mm_selConnected", "mm_selCluster"})
        sel->addAction(m_a[QLatin1String(k)]);
    for (const char *k : {"mm_invert", "mm_deselect", "", "mm_link", ""}) {
        if (!*k)
            m->addSeparator();
        else
            m->addAction(m_a[QLatin1String(k)]);
    }
    QMenu *find = m->addMenu(icons::action(QStringLiteral("edit-find")), tr("&Find"));
    for (const char *k : {"mm_find", "mm_findNext", "mm_findPrev", "mm_useSelection", "mm_jump"})
        find->addAction(m_a[QLatin1String(k)]);
}

void MindMapController::fillViewMenu(QMenu *m)
{
    for (const char *k : {"mm_inspector", "mm_footer", "mm_pageGuides", "", "mm_actualSize", "mm_zoomFit", "mm_zoomIn", "mm_zoomOut",
                          "mm_zoomSelection", "mm_zoomRandom"}) {
        if (!*k)
            m->addSeparator();
        else if (m_window || (QLatin1String(k) != QLatin1String("mm_inspector")))
            m->addAction(m_a[QLatin1String(k)]);
    }
    if (!m_window) {
        m->addSeparator();
        m->addAction(m_a[QStringLiteral("mm_openWindow")]);
    }
}

void MindMapController::fillNotesMenu(QMenu *m)
{
    m->addAction(m_a[QStringLiteral("mm_newNote")]);
    m->addAction(m_a[QStringLiteral("mm_newShape")]);
    QMenu *conn = m->addMenu(tr("New &Connected Note"));
    for (const char *k : {"mm_connLeft", "mm_connRight", "mm_connAbove", "mm_connBelow", "", "mm_connArrowLeft", "mm_connArrowRight",
                          "mm_connArrowAbove", "mm_connArrowBelow"}) {
        if (!*k)
            conn->addSeparator();
        else
            conn->addAction(m_a[QLatin1String(k)]);
    }
    m->addSeparator();
    for (const char *k : {"mm_connect", "mm_connectAll", "mm_connectArrow", "mm_removeArrows", "mm_disconnect", "mm_label", "",
                          "mm_magnetic", "mm_stack", "mm_autoFit"}) {
        if (!*k)
            m->addSeparator();
        else
            m->addAction(m_a[QLatin1String(k)]);
    }
    QMenu *al = m->addMenu(tr("&Align"));
    for (int i = 0; i < 6; ++i) {
        if (i == 4)
            al->addSeparator();
        al->addAction(m_a[QStringLiteral("mm_align%1").arg(i)]);
    }
    al->addSeparator();
    for (const char *k : {"mm_centerShape", "mm_centerShapeH", "mm_centerShapeV"})
        al->addAction(m_a[QLatin1String(k)]);
    QMenu *dist = m->addMenu(tr("&Distribute"));
    dist->addAction(m_a[QStringLiteral("mm_distH")]);
    dist->addAction(m_a[QStringLiteral("mm_distV")]);
    for (const char *k : {"mm_sameWidth", "mm_sameHeight", "", "mm_split", "mm_merge", "", "mm_front", "mm_back", "", "mm_toBinder"}) {
        if (!*k)
            m->addSeparator();
        else
            m->addAction(m_a[QLatin1String(k)]);
    }
}

void MindMapController::fillStyleMenus(QMenu *apply, QMenu *redefine, QMenu *remove)
{
    for (QMenu *m : {apply, redefine, remove})
        if (m)
            m->clear();
    if (apply)
        apply->addAction(m_a[QStringLiteral("mm_defaultStyle")]);
    const QList<NoteStyle> list = sortedStyles();
    for (int i = 0; i < list.size(); ++i) {
        const NoteStyle s = list[i];
        const QString id = s.id;
        if (apply) {
            QAction *a = i < 9 ? m_a[QStringLiteral("mm_style%1").arg(i + 1)] : nullptr;
            if (a) {
                a->setText(s.name);
                apply->addAction(a);
            } else {
                apply->addAction(s.name, this, [this, s] { applyStyle(s, targets()); });
            }
        }
        if (redefine)
            redefine->addAction(s.name, this, [this, id] { redefineStyle(id); })->setEnabled(targets().size() == 1);
        if (remove)
            remove->addAction(s.name, this, [this, id] { deleteStyle(id); });
    }
}

// The board's background (0.21.1): the shared palette, "Default" (the board follows the
// default of light or dark themes again), Other... The choice is the board's own color
// for the kind of theme in use; the other kind keeps its own.
void MindMapController::fillBackgroundMenu(QMenu *m)
{
    m->setObjectName(QStringLiteral("mm_menuBackground"));
    const bool dark = MindMapDoc::darkMode();
    m->setToolTipsVisible(true);
    palette::fillMenu(m, m_doc->project(), m_doc->ownBackground(), [this](const QColor &c) { m_doc->setOwnBackground(c); }, m_owner,
                      dark ? tr("Default for Dark Themes") : tr("Default for Light Themes"), tr("Background Color"));
    m->setEnabled(!m_doc->locked());
}

void MindMapController::fillFormatMenu(QMenu *m)
{
    QMenu *st = m->addMenu(tr("Note &Style"));
    st->addAction(m_a[QStringLiteral("mm_newStyle")]);
    QMenu *apply = st->addMenu(tr("&Apply Note Style"));
    QMenu *redef = st->addMenu(tr("&Redefine Note Style From Selection"));
    QMenu *del = st->addMenu(tr("&Delete Note Style"));
    connect(st, &QMenu::aboutToShow, this, [this, apply, redef, del] { fillStyleMenus(apply, redef, del); });
    fillStyleMenus(apply, redef, del);
    st->addSeparator();
    for (const char *k : {"mm_copyStyle", "mm_pasteStyle", "mm_importStyles"})
        st->addAction(m_a[QLatin1String(k)]);
    QMenu *font = m->addMenu(icons::action(QStringLiteral("font")), tr("&Font"));
    for (const char *k : {"mm_bold", "mm_italic", "mm_underline", "mm_strike", "", "mm_bigger", "mm_smaller"}) {
        if (!*k)
            font->addSeparator();
        else
            font->addAction(m_a[QLatin1String(k)]);
    }
    QMenu *al = m->addMenu(tr("&Alignment"));
    for (int i = 0; i < 4; ++i)
        al->addAction(m_a[QStringLiteral("mm_textAlign%1").arg(i)]);
    QMenu *ind = m->addMenu(tr("&Indents"));
    for (const char *k : {"mm_indent", "mm_outdent", "mm_noIndent"})
        ind->addAction(m_a[QLatin1String(k)]);
    fillBackgroundMenu(m->addMenu(palette::swatch(board().settings.background), tr("&Background Color")));
    QMenu *col = m->addMenu(tr("&Colors"));
    for (const char *k : {"mm_textColor", "mm_clearText", "", "mm_fillColor", "mm_clearFill", "", "mm_borderColor", "mm_clearBorder"}) {
        if (!*k)
            col->addSeparator();
        else
            col->addAction(m_a[QLatin1String(k)]);
    }
    QMenu *bor = m->addMenu(tr("&Border"));
    for (int w = 0; w <= 5; ++w)
        bor->addAction(m_a[QStringLiteral("mm_border%1").arg(w)]);
    bor->addSeparator();
    fillShapeMenu(bor);
    // the icons at the corners and sides of the selected notes (0.21); made anew when opened
    QMenu *ic = m->addMenu(tr("&Icons"));
    ic->setObjectName(QStringLiteral("mm_formatIcons"));
    fillIconsMenu(ic);
    connect(ic, &QMenu::aboutToShow, this, [this, ic] {
        ic->clear();
        fillIconsMenu(ic);
    });
    QMenu *dir = m->addMenu(tr("&Writing Direction"));
    dir->addAction(m_a[QStringLiteral("mm_ltr")]);
    dir->addAction(m_a[QStringLiteral("mm_rtl")]);
    m->addAction(m_a[QStringLiteral("mm_shadow")]);
    m->addAction(m_a[QStringLiteral("mm_fade")]);
}

void MindMapController::fillContextMenu(QMenu *m)
{
    updateActions();
    if (scene()->editingNote()) {
        for (const char *k : {"mm_cut", "mm_copy", "mm_paste", "", "mm_bold", "mm_italic", "mm_underline", "mm_strike", "", "mm_link", "mm_split"}) {
            if (!*k)
                m->addSeparator();
            else
                m->addAction(m_a[QLatin1String(k)]);
        }
        return;
    }
    if (scene()->selectedIds().isEmpty()) {
        QAction *pasteHere = m->addAction(icons::action(QStringLiteral("edit-paste")), tr("&Paste"), this,
                                          [this] { paste(m_view->lastContextPos(), true); });
        pasteHere->setEnabled(m_a[QStringLiteral("mm_paste")]->isEnabled());
        m->addAction(icons::action(QStringLiteral("list-add")), tr("&New Note Here"), this,
                     [this] { newNoteAt(m_view->lastContextPos()); })
            ->setEnabled(!m_doc->locked());
        m->addAction(m_a[QStringLiteral("mm_newShape")]);
        m->addSeparator();
        fillBackgroundMenu(m->addMenu(palette::swatch(board().settings.background), tr("&Background Color")));
        m->addSeparator();
        m->addAction(m_a[QStringLiteral("mm_selectAll")]);
        m->addAction(m_a[QStringLiteral("mm_zoomFit")]);
        if (!m_window)
            m->addAction(m_a[QStringLiteral("mm_openWindow")]);
        return;
    }
    for (const char *k : {"mm_cut", "mm_copy", "mm_paste", "mm_delete", ""})
        if (!*k)
            m->addSeparator();
        else
            m->addAction(m_a[QLatin1String(k)]);
    QMenu *apply = m->addMenu(tr("Apply &Style"));
    fillStyleMenus(apply, nullptr, nullptr);
    m->addSeparator();
    // colors from the shared palette, the shape, the icons (0.21)
    const bool locked = m_doc->locked();
    QMenu *fill = m->addMenu(palette::swatch(currentColor(1)), tr("&Fill Color"));
    fill->setObjectName(QStringLiteral("mm_menuFill"));
    fillColorMenu(fill, 1);
    QMenu *ink = m->addMenu(palette::swatch(currentColor(0)), tr("&Text Color"));
    ink->setObjectName(QStringLiteral("mm_menuText"));
    fillColorMenu(ink, 0);
    QMenu *edge = m->addMenu(palette::swatch(currentColor(2)), tr("Bor&der Color"));
    edge->setObjectName(QStringLiteral("mm_menuBorderColor"));
    fillColorMenu(edge, 2);
    QMenu *shape = m->addMenu(tr("S&hape"));
    shape->setObjectName(QStringLiteral("mm_menuShape"));
    fillShapeMenu(shape);
    QMenu *bor = m->addMenu(tr("&Border"));
    for (int w = 0; w <= 5; ++w)
        bor->addAction(m_a[QStringLiteral("mm_border%1").arg(w)]);
    QMenu *icons = m->addMenu(icons::get(QStringLiteral("qb-flag-red")), tr("&Icons"));
    icons->setObjectName(QStringLiteral("mm_menuIcons"));
    fillIconsMenu(icons, m_view->contextIconPlace());
    for (QMenu *sub : {fill, ink, edge, shape, bor, icons})
        sub->setEnabled(!locked);
    for (const char *k : {"", "mm_connect", "mm_connectArrow", "mm_disconnect", "mm_label", "", "mm_stack",
                          "mm_magnetic", "mm_fade", "", "mm_front", "mm_back"}) {
        if (!*k)
            m->addSeparator();
        else
            m->addAction(m_a[QLatin1String(k)]);
    }
}

QColor MindMapController::currentColor(int which) const
{
    const QList<int> ids = targets();
    if (ids.isEmpty())
        return QColor();
    if (const Note *n = board().note(ids.first()))
        return which == 0 ? n->look.textColor : which == 1 ? n->look.fill : n->look.borderColor;
    if (const Shape *s = board().shape(ids.first()))
        return which == 1 ? s->look.fill : which == 2 ? s->look.borderColor : QColor();
    return QColor();
}

void MindMapController::fillColorMenu(QMenu *m, int which)
{
    const QStringList none = {tr("Automatic"), tr("No Fill"), tr("Automatic")};
    const QStringList titles = {tr("Text Color"), tr("Fill Color"), tr("Border Color")};
    palette::fillMenu(m, m_doc->project(), currentColor(which), [this, which](const QColor &c) { setColor(which, c); }, m_owner,
                      none.value(which), titles.value(which));
}

void MindMapController::fillShapeMenu(QMenu *m)
{
    // in the order the writer asked for: rectangle, rounded rectangle, cloud (and Scapple's jagged)
    for (int i : {1, 0, 3, 2})
        m->addAction(m_a[QStringLiteral("mm_borderStyle%1").arg(i)]);
}

void MindMapController::fillIconsMenu(QMenu *m, int placeUnderMouse)
{
    const QList<int> ids = targets();
    const Project *project = m_doc->project();
    // the icons the first selected item has now
    const IconMap have = ids.isEmpty() ? IconMap() : board().iconsOf(ids.first());
    if (placeUnderMouse >= 0 && have.contains(placeUnderMouse)) {
        QAction *rm = m->addAction(icons::iconFor(project, have.value(placeUnderMouse)), tr("&Remove This Icon"), this,
                                   [this, placeUnderMouse] { setIconAt(placeUnderMouse, QString()); });
        rm->setObjectName(QStringLiteral("mm_removeIconHere"));
        m->addSeparator();
    }
    const QStringList places = {tr("Top Left Corner"), tr("Top"), tr("Top Right Corner"), tr("Right"),
                                tr("Bottom Right Corner"), tr("Bottom"), tr("Bottom Left Corner"), tr("Left")};
    for (int place = 0; place < kIconPositions; ++place) {
        const QString now = have.value(place);
        QMenu *sub = m->addMenu(now.isEmpty() ? QIcon() : icons::iconFor(project, now), places[place]);
        sub->setObjectName(QStringLiteral("mm_iconPlace%1").arg(place));
        fillIconChoiceMenu(sub, project, [this, place](const QString &id) { setIconAt(place, id); });
        if (!now.isEmpty()) {
            sub->addSeparator();
            sub->addAction(tr("&Remove"), this, [this, place] { setIconAt(place, QString()); });
        }
    }
    m->addSeparator();
    QAction *all = m->addAction(tr("Remove &All Icons"), this, &MindMapController::clearIcons);
    all->setEnabled(!have.isEmpty());
}

void MindMapController::fillLineMenu(QMenu *m, int a, int b)
{
    // a line between a and b: what is changed here belongs to this line only
    const QString na = itemName(a), nb = itemName(b);
    QAction *head = m->addAction(tr("Line: %1 – %2").arg(na, nb));
    head->setEnabled(false);
    m->addSeparator();
    const bool toB = board().pointsTo(a, b), toA = board().pointsTo(b, a);
    auto *arrows = new QActionGroup(m);
    auto arrow = [&](const QString &text, bool wantB, bool wantA, const char *name) {
        QAction *ac = m->addAction(text, this, [this, a, b, wantB, wantA] { setLineArrows(a, b, wantB, wantA); });
        ac->setCheckable(true);
        ac->setChecked(toB == wantB && toA == wantA);
        ac->setObjectName(QLatin1String(name));
        arrows->addAction(ac);
    };
    arrow(tr("&No Arrows"), false, false, "mm_lineNoArrow");
    arrow(tr("Arrow to %1").arg(nb), true, false, "mm_lineArrowToB");
    arrow(tr("Arrow to %1").arg(na), false, true, "mm_lineArrowToA");
    arrow(tr("Arrows at &Both Ends"), true, true, "mm_lineArrowBoth");
    m->addSeparator();
    const LineLook look = board().lineLook(a, b);
    QMenu *color = m->addMenu(palette::swatch(look.color), tr("Line &Color"));
    color->setObjectName(QStringLiteral("mm_lineColorMenu"));
    palette::fillMenu(color, m_doc->project(), look.color, [this, a, b](const QColor &c) { setLineColor(a, b, c); }, m_owner,
                      tr("Automatic"), tr("Line Color"));
    QMenu *icon = m->addMenu(look.icon.isEmpty() ? QIcon() : icons::iconFor(m_doc->project(), look.icon), tr("&Icon in the Middle"));
    icon->setObjectName(QStringLiteral("mm_lineIconMenu"));
    fillIconChoiceMenu(icon, m_doc->project(), [this, a, b](const QString &id) { setLineIcon(a, b, id); });
    if (!look.icon.isEmpty()) {
        icon->addSeparator();
        icon->addAction(tr("&Remove"), this, [this, a, b] { setLineIcon(a, b, QString()); });
    }
    m->addSeparator();
    m->addAction(board().lineLabel(a, b).isEmpty() ? tr("Add &Label...") : tr("Edit &Label..."), this, [this, a, b] { editLineLabel(a, b); });
    m->addAction(tr("Insert a &Note on the Line"), this, [this, a, b] { noteBetween(a, b, m_view->lastContextPos()); });
    m->addAction(icons::action(QStringLiteral("edit-delete")), tr("&Disconnect"), this, [this, a, b] {
        m_doc->change(tr("Disconnect"), [&] { board().disconnect(a, b); });
    });
}

QString MindMapController::itemName(int id) const
{
    if (const Note *n = board().note(id)) {
        if (n->isImage())
            return tr("Picture");
        QString t = n->text.simplified();
        if (t.isEmpty())
            return tr("Empty Note");
        if (t.size() > 24)
            t = t.left(22) + QStringLiteral("...");
        return QStringLiteral("“%1”").arg(t);
    }
    if (board().shape(id))
        return tr("Shape");
    return QString();
}

void MindMapController::setColor(int which, const QColor &c)
{
    if (!c.isValid()) {
        clearColor(which);
        return;
    }
    const QStringList names = {tr("Text Color"), tr("Fill Color"), tr("Border Color")};
    forItemsLook(names.value(which), [which, c](Appearance &a) {
        if (which == 0)
            a.textColor = c;
        else if (which == 1)
            a.fill = c;
        else {
            a.borderColor = c;
            if (a.borderWeight == 0)
                a.borderWeight = 1; // a color for a border that was not there: one pixel
        }
    });
}

void MindMapController::setIconAt(int place, const QString &iconId)
{
    const QList<int> ids = targets();
    if (ids.isEmpty() || m_doc->locked())
        return;
    m_doc->change(iconId.isEmpty() ? tr("Remove Icon") : tr("Icon"), [&] {
        for (int id : ids)
            board().setIcon(id, place, iconId);
    });
}

void MindMapController::clearIcons()
{
    const QList<int> ids = targets();
    if (ids.isEmpty() || m_doc->locked())
        return;
    m_doc->change(tr("Remove All Icons"), [&] {
        for (int id : ids)
            for (int place = 0; place < kIconPositions; ++place)
                board().setIcon(id, place, QString());
    });
}

void MindMapController::setLineArrows(int a, int b, bool toB, bool toA)
{
    if (!board().isConnected(a, b) || m_doc->locked())
        return;
    m_doc->change(tr("Arrows"), [&] { board().setArrows(a, b, toB, toA); });
}

void MindMapController::setLineColor(int a, int b, const QColor &c)
{
    if (!board().isConnected(a, b) || m_doc->locked())
        return;
    LineLook look = board().lineLook(a, b);
    look.color = c;
    m_doc->change(tr("Line Color"), [&] { board().setLineLook(a, b, look); });
}

void MindMapController::setLineIcon(int a, int b, const QString &iconId)
{
    if (!board().isConnected(a, b) || m_doc->locked())
        return;
    LineLook look = board().lineLook(a, b);
    look.icon = iconId;
    m_doc->change(iconId.isEmpty() ? tr("Remove Icon") : tr("Line Icon"), [&] { board().setLineLook(a, b, look); });
}

void MindMapController::updateActions()
{
    const bool locked = m_doc->locked();
    const QList<int> sel = scene()->selectedIds();
    const QList<int> notes = scene()->selectedNotes();
    const QList<int> shapes = scene()->selectedShapes();
    const bool editing = scene()->editingNote() != nullptr;
    const bool any = !sel.isEmpty();
    const bool hasItems = !board().isEmpty();
    auto en = [this](const char *k, bool on) {
        if (QAction *a = m_a.value(QLatin1String(k)))
            a->setEnabled(on);
    };
    m_a[QStringLiteral("mm_lock")]->setChecked(locked);
    en("mm_undo", !locked && (editing || m_doc->undoStack()->canUndo()));
    en("mm_redo", !locked && (editing || m_doc->undoStack()->canRedo()));
    m_a[QStringLiteral("mm_undo")]->setText(editing || !m_doc->undoStack()->canUndo() ? tr("&Undo")
                                                                                       : tr("&Undo %1").arg(m_doc->undoStack()->undoText()));
    m_a[QStringLiteral("mm_redo")]->setText(editing || !m_doc->undoStack()->canRedo() ? tr("&Redo")
                                                                                       : tr("&Redo %1").arg(m_doc->undoStack()->redoText()));
    en("mm_cut", !locked && (any || editing));
    en("mm_copy", any || editing);
    const QMimeData *md = QApplication::clipboard()->mimeData();
    en("mm_paste", !locked && md && (md->hasFormat(QLatin1String(kBoardMime)) || md->hasText() || md->hasImage()));
    en("mm_delete", !locked && any && !editing);
    for (const char *k : {"mm_newNote", "mm_newShape"})
        en(k, !locked);
    m_a[QStringLiteral("mm_newNote")]->setText(notes.size() == 1 ? tr("&New Stacked Note") : tr("&New Note"));
    m_a[QStringLiteral("mm_newShape")]->setText(any ? tr("New &Background Shape Around Selection") : tr("New &Background Shape"));
    for (const char *k : {"mm_connLeft", "mm_connRight", "mm_connAbove", "mm_connBelow", "mm_connArrowLeft", "mm_connArrowRight",
                          "mm_connArrowAbove", "mm_connArrowBelow"})
        en(k, !locked && !notes.isEmpty());
    for (const char *k : {"mm_connect", "mm_connectAll", "mm_connectArrow"})
        en(k, !locked && sel.size() >= 2);
    bool anyLine = false, anyArrow = false;
    for (int a : sel)
        for (int b : board().connectionsOf(a)) {
            if (sel.contains(b) || sel.size() == 1) {
                anyLine = true;
                if (board().pointsTo(a, b) || board().pointsTo(b, a))
                    anyArrow = true;
            }
        }
    en("mm_removeArrows", !locked && anyArrow);
    en("mm_disconnect", !locked && anyLine);
    en("mm_label", !locked && sel.size() == 2);
    en("mm_magnetic", !locked && !shapes.isEmpty());
    m_a[QStringLiteral("mm_magnetic")]->setChecked(!shapes.isEmpty() && board().shape(shapes.first()) && board().shape(shapes.first())->magnetic);
    en("mm_stack", !locked && notes.size() >= 2);
    en("mm_autoFit", !locked && any);
    for (int i = 0; i < 6; ++i)
        en(QStringLiteral("mm_align%1").arg(i).toLatin1().constData(), !locked && sel.size() >= 2);
    bool onShape = false;
    for (int id : sel)
        if (!board().shapesUnder(id).isEmpty())
            onShape = true;
    en("mm_centerShape", !locked && sel.size() == 1 && onShape);
    en("mm_centerShapeH", !locked && onShape);
    en("mm_centerShapeV", !locked && onShape);
    en("mm_distH", !locked && sel.size() >= 3);
    en("mm_distV", !locked && sel.size() >= 3);
    en("mm_sameWidth", !locked && sel.size() >= 2);
    bool tall = false;
    for (int id : sel)
        if (board().shape(id) || (board().note(id) && board().note(id)->isImage()))
            tall = true;
    en("mm_sameHeight", !locked && sel.size() >= 2 && tall);
    en("mm_split", !locked && editing);
    en("mm_merge", !locked && notes.size() >= 2);
    en("mm_front", !locked && any);
    en("mm_back", !locked && any);
    en("mm_toBinder", !locked && hasItems && m_doc->project());
    en("mm_selStacked", notes.size() >= 1);
    en("mm_selOverlap", !shapes.isEmpty());
    en("mm_selConnected", any);
    en("mm_selCluster", any);
    en("mm_invert", hasItems);
    en("mm_deselect", any || editing);
    en("mm_selectAll", hasItems || editing);
    en("mm_link", !locked && editing);
    en("mm_useSelection", any || editing);
    en("mm_jump", any);
    en("mm_findNext", hasItems);
    en("mm_findPrev", hasItems);
    en("mm_zoomSelection", any);
    en("mm_zoomRandom", !board().notes.isEmpty());
    en("mm_zoomFit", hasItems);
    en("mm_newStyle", sel.size() == 1);
    en("mm_copyStyle", sel.size() == 1);
    en("mm_pasteStyle", !locked && any && !m_styleClipboard.isEmpty());
    en("mm_importStyles", !locked);
    en("mm_importScap", !locked);
    const QList<NoteStyle> styles = sortedStyles();
    en("mm_defaultStyle", !locked && any);
    for (int i = 1; i <= 9; ++i) {
        QAction *a = m_a[QStringLiteral("mm_style%1").arg(i)];
        a->setEnabled(!locked && any && i <= styles.size());
        a->setText(i <= styles.size() ? styles[i - 1].name : tr("Style %1").arg(i));
    }
    const bool fmt = !locked && (any || editing);
    for (const char *k : {"mm_bold", "mm_italic", "mm_underline", "mm_strike"})
        en(k, !locked && (!notes.isEmpty() || editing));
    for (const char *k : {"mm_bigger", "mm_smaller", "mm_textColor", "mm_clearText", "mm_indent", "mm_outdent", "mm_noIndent"})
        en(k, !locked && !notes.isEmpty());
    for (int i = 0; i < 4; ++i)
        en(QStringLiteral("mm_textAlign%1").arg(i).toLatin1().constData(), !locked && !notes.isEmpty());
    for (const char *k : {"mm_fillColor", "mm_clearFill", "mm_borderColor", "mm_clearBorder", "mm_shadow", "mm_fade"})
        en(k, fmt && any);
    for (int w = 0; w <= 5; ++w)
        en(QStringLiteral("mm_border%1").arg(w).toLatin1().constData(), fmt && any);
    for (int i = 0; i < 4; ++i)
        en(QStringLiteral("mm_borderStyle%1").arg(i).toLatin1().constData(), fmt && any);
    en("mm_ltr", !locked);
    en("mm_rtl", !locked);
    m_a[board().settings.rightToLeft ? QStringLiteral("mm_rtl") : QStringLiteral("mm_ltr")]->setChecked(true);
    m_a[QStringLiteral("mm_footer")]->setChecked(board().settings.footer);
    m_a[QStringLiteral("mm_pageGuides")]->setChecked(board().settings.pageGuides);
    // the state of the first selected item
    const int first = sel.isEmpty() ? 0 : sel.first();
    Appearance look;
    if (const Note *n = board().note(first)) {
        look = n->look;
        m_a[QStringLiteral("mm_textAlign%1").arg(n->align == Align::Left ? 0 : n->align == Align::Center ? 1 : n->align == Align::Justify ? 2 : 3)]
            ->setChecked(true);
    } else if (const Shape *s = board().shape(first)) {
        look = s->look;
    }
    if (first) {
        m_a[QStringLiteral("mm_border%1").arg(qBound(0, look.borderWeight, 5))]->setChecked(true);
        m_a[QStringLiteral("mm_borderStyle%1").arg(int(look.borderStyle))]->setChecked(true);
    }
    m_a[QStringLiteral("mm_shadow")]->setChecked(first && look.shadow);
    m_a[QStringLiteral("mm_fade")]->setChecked(first && look.faded);
    for (const char *k : {"mm_exportPdf", "mm_exportPng", "mm_exportText", "mm_exportRtf", "mm_exportList", "mm_exportOpml", "mm_exportScap",
                          "mm_printPreview", "mm_print"})
        en(k, hasItems);
    bool images = false;
    for (const Note &n : board().notes)
        if (n.isImage())
            images = true;
    en("mm_exportImages", images);
}

// ------------------------------------------------------------------ helpers

QList<int> MindMapController::targets() const { return scene()->selectedIds(); }

QList<NoteStyle> MindMapController::sortedStyles() const
{
    QList<NoteStyle> l = board().styles;
    std::stable_sort(l.begin(), l.end(), [](const NoteStyle &a, const NoteStyle &b) { return a.name.localeAwareCompare(b.name) < 0; });
    return l;
}

void MindMapController::forNotes(const QString &undoText, const std::function<void(Note &)> &fn, bool)
{
    const QList<int> ids = scene()->selectedNotes();
    if (ids.isEmpty())
        return;
    m_doc->change(undoText, [&] {
        for (int id : ids) {
            Note *n = board().note(id);
            if (!n)
                continue;
            const double oldBottom = board().rectOf(id).bottom();
            // a note as narrow as its text stays so (bigger letters, bold ...)
            const bool snug = !n->isImage() && !n->text.contains(QLatin1Char('\n'))
                              && qAbs(board().fittingWidth(*n) - n->width) < 6;
            fn(*n);
            if (snug && MindMapPrefs::instance().autoSize)
                n->width = qMax(40.0, qMin(board().fittingWidth(*n), qMax(n->width, MindMapPrefs::instance().newNoteWidth)));
            board().restack(id, oldBottom);
        }
    });
}

void MindMapController::forItemsLook(const QString &undoText, const std::function<void(Appearance &)> &fn)
{
    const QList<int> ids = targets();
    if (ids.isEmpty())
        return;
    m_doc->change(undoText, [&] {
        for (int id : ids) {
            if (Note *n = board().note(id))
                fn(n->look);
            else if (Shape *s = board().shape(id))
                fn(s->look);
        }
    });
}

Note MindMapController::templateNote() const
{
    const MindMapPrefs &prefs = MindMapPrefs::instance();
    Note t;
    t.width = prefs.newNoteWidth;
    t.fontSize = prefs.newNoteSize;
    t.look.fill = prefs.newNoteFill;
    t.look.borderWeight = prefs.newNoteBorder;
    t.look.borderStyle = BorderStyle(qBound(0, prefs.newNoteShape, 3));
    t.text = prefs.placeholder();
    const QList<int> sel = scene()->selectedNotes();
    if (prefs.useSelectedStyle && !sel.isEmpty())
        if (const Note *n = board().note(sel.first()); n && !n->isImage()) {
            t.look = n->look;
            t.fontSize = n->fontSize;
            t.bold = n->bold;
            t.italic = n->italic;
            t.underline = n->underline;
            t.strike = n->strike;
            t.align = n->align;
            t.width = n->width;
            t.indent = 0;
        }
    return t;
}

QPointF MindMapController::freeSpotNear(const QPointF &p) const
{
    // a place where the new note does not cover another one
    QPointF at = p;
    for (int i = 0; i < 40; ++i) {
        bool hit = false;
        for (const Note &n : board().notes)
            if (QLineF(n.pos, at).length() < 6)
                hit = true;
        if (!hit)
            return at;
        at += QPointF(18, 18);
    }
    return at;
}

// ------------------------------------------------------------------ creating notes

int MindMapController::newNoteAt(const QPointF &sp, Qt::KeyboardModifiers mods, const QString &text)
{
    if (m_doc->locked())
        return 0;
    scene()->endEditing();
    Note n = templateNote();
    if (!text.isEmpty())
        n.text = text;
    n.id = board().nextId();
    n.pos = sp - QPointF(n.width / 2, 12);
    const QList<int> sel = scene()->selectedIds();
    const bool alt = mods & Qt::AltModifier, ctrl = mods & Qt::ControlModifier;
    m_doc->change(tr("New Note"), [&] {
        board().notes << n;
        if (!sel.isEmpty() && (alt || ctrl)) {
            for (int s : sel) {
                if (alt && ctrl)
                    board().connect(s, n.id, false, true);  // the arrow points back to the selected note
                else if (alt)
                    board().connect(s, n.id, true, false);  // arrow to the new note
                else
                    board().connect(s, n.id);               // dashed line
            }
        }
    });
    scene()->startEditing(n.id, true);
    return n.id;
}

void MindMapController::newNote()
{
    if (m_doc->locked())
        return;
    scene()->endEditing();
    const QList<int> sel = scene()->selectedNotes();
    if (sel.size() == 1) {
        // a stacked note below the selected one; the notes below move down
        const int above = sel.first();
        const QRectF r = board().rectOf(above);
        Note n = templateNote();
        n.id = board().nextId();
        n.pos = r.bottomLeft();
        n.width = r.width();
        if (const Note *a = board().note(above))
            n.indent = a->indent;
        m_doc->change(tr("New Stacked Note"), [&] {
            const QList<int> below = board().stackBelow(above);
            board().notes << n;
            const double h = board().rectOf(n.id).height();
            for (int b : below)
                board().note(b)->pos.ry() += h;
        });
        scene()->startEditing(n.id, true);
        m_view->ensureItemVisible(n.id);
        return;
    }
    newNoteAt(freeSpotNear(m_view->visibleCenter()));
}

void MindMapController::newConnectedNote(int dx, int dy, bool arrow)
{
    const QList<int> sel = scene()->selectedNotes();
    if (sel.isEmpty() || m_doc->locked())
        return;
    scene()->endEditing();
    const int hub = sel.first();
    const QRectF r = board().rectOf(hub);
    Note n = templateNote();
    if (const Note *h = board().note(hub); h && MindMapPrefs::instance().useSelectedStyle && !h->isImage()) {
        n.look = h->look;
        n.fontSize = h->fontSize;
    }
    n.id = board().nextId();
    const double gap = 50;
    n.pos = QPointF(dx < 0 ? r.left() - gap - n.width : dx > 0 ? r.right() + gap : r.left(), dy < 0 ? r.top() - gap - 30 : dy > 0 ? r.bottom() + gap : r.top());
    n.pos = freeSpotNear(n.pos);
    m_doc->change(tr("New Connected Note"), [&] {
        board().notes << n;
        board().connect(hub, n.id, arrow, false);
    });
    scene()->startEditing(n.id, true);
    m_view->ensureItemVisible(n.id);
}

void MindMapController::noteBetween(int a, int b, const QPointF &pos)
{
    if (m_doc->locked())
        return;
    Note n = templateNote();
    n.id = board().nextId();
    n.pos = pos - QPointF(n.width / 2, 12);
    const bool toB = board().pointsTo(a, b), toA = board().pointsTo(b, a);
    m_doc->change(tr("New Note"), [&] {
        board().disconnect(a, b);
        board().notes << n;
        board().connect(a, n.id, toB, toA);
        board().connect(n.id, b, toB, toA);
    });
    scene()->startEditing(n.id, true);
}

void MindMapController::editLineLabel(int a, int b)
{
    if (m_doc->locked())
        return;
    bool ok = false;
    const QString t = QInputDialog::getText(m_owner, tr("Connection Label"), tr("Label of the line (empty: no label):"), QLineEdit::Normal,
                                            board().lineLabel(a, b), &ok);
    if (!ok)
        return;
    m_doc->change(tr("Connection Label"), [&] {
        if (!board().isConnected(a, b))
            board().connect(a, b);
        board().setLineLabel(a, b, t);
    });
}

void MindMapController::deleteSelection()
{
    const QList<int> ids = targets();
    if (ids.isEmpty() || m_doc->locked() || scene()->editingNote())
        return;
    m_doc->change(tr("Delete"), [&] {
        // the notes below a deleted stacked note move up
        for (int id : ids) {
            const Note *n = board().note(id);
            if (!n || !board().stackAbove(id))
                continue;
            const QList<int> below = board().stackBelow(id);
            const double h = board().rectOf(id).height();
            for (int b : below)
                if (!ids.contains(b))
                    board().note(b)->pos.ry() -= h;
        }
        for (int id : ids)
            board().remove(id);
    });
}

void MindMapController::indent(int delta)
{
    forNotes(delta > 0 ? tr("Increase Indent") : tr("Decrease Indent"), [delta](Note &n) { n.indent = qBound(0, n.indent + delta, 10); });
}

void MindMapController::nudge(double dx, double dy)
{
    const QList<int> ids = targets();
    if (ids.isEmpty())
        return;
    m_doc->change(tr("Move"), [&] {
        for (int id : ids) {
            if (Note *n = board().note(id))
                n->pos += QPointF(dx, dy);
            else if (Shape *s = board().shape(id)) {
                s->rect.translate(dx, dy);
                if (s->magnetic)
                    for (int o : board().overlapping(id))
                        if (!ids.contains(o)) {
                            if (Note *on = board().note(o))
                                on->pos += QPointF(dx, dy);
                        }
            }
        }
    });
}

void MindMapController::duplicate(const QList<int> &ids, const QPointF &offset)
{
    if (ids.isEmpty() || m_doc->locked())
        return;
    QHash<int, int> map;
    int next = board().nextId();
    QList<int> created;
    m_doc->change(tr("Copy"), [&] {
        for (int id : ids) {
            if (const Note *n = board().note(id)) {
                Note c = *n;
                c.id = next++;
                c.pos += offset;
                c.connected.clear();
                c.pointsTo.clear();
                map.insert(id, c.id);
                board().notes << c;
                created << c.id;
            } else if (const Shape *s = board().shape(id)) {
                Shape c = *s;
                c.id = next++;
                c.rect.translate(offset);
                c.connected.clear();
                c.pointsTo.clear();
                map.insert(id, c.id);
                board().shapes << c;
                created << c.id;
            }
        }
        // the lines among the copied items come along
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            for (int o : board().connectionsOf(it.key()))
                if (map.contains(o)) {
                    board().connect(it.value(), map.value(o), board().pointsTo(it.key(), o), board().pointsTo(o, it.key()));
                    // with their labels, colors and icons (0.21)
                    board().setLineLabel(it.value(), map.value(o), board().lineLabel(it.key(), o));
                    board().setLineLook(it.value(), map.value(o), board().lineLook(it.key(), o));
                }
    });
    scene()->selectIds(created);
}

// ------------------------------------------------------------------ connections, shapes, stacks

void MindMapController::connectSelected(int how)
{
    const QList<int> sel = targets();
    if (sel.isEmpty())
        return;
    const QStringList texts = {tr("Connect"), tr("Connect All"), tr("Connect With Arrow"), tr("Remove Arrows"), tr("Disconnect")};
    m_doc->change(texts.value(how), [&] {
        const int hub = sel.first();
        switch (how) {
        case 0:
            for (int i = 1; i < sel.size(); ++i)
                board().connect(hub, sel[i]);
            break;
        case 1:
            for (int i = 0; i < sel.size(); ++i)
                for (int j = i + 1; j < sel.size(); ++j)
                    board().connect(sel[i], sel[j]);
            break;
        case 2:
            for (int i = 1; i < sel.size(); ++i)
                board().connect(hub, sel[i], true, false);
            break;
        case 3:
            for (int a : sel)
                for (int b : board().connectionsOf(a))
                    if (sel.contains(b) || sel.size() == 1)
                        board().removeArrows(a, b);
            break;
        case 4:
            if (sel.size() == 1) {
                for (int b : board().connectionsOf(hub))
                    board().disconnect(hub, b);
            } else {
                for (int i = 0; i < sel.size(); ++i)
                    for (int j = i + 1; j < sel.size(); ++j)
                        board().disconnect(sel[i], sel[j]);
            }
            break;
        }
    });
}

void MindMapController::newShape()
{
    if (m_doc->locked())
        return;
    scene()->endEditing();
    const MindMapPrefs &prefs = MindMapPrefs::instance();
    Shape s;
    s.id = board().nextId();
    s.look.fill = prefs.newShapeFill;
    s.look.borderWeight = prefs.newShapeBorder;
    s.magnetic = prefs.newShapeMagnetic;
    const QList<int> sel = targets();
    if (!sel.isEmpty()) {
        QRectF r;
        for (int id : sel)
            r = r.united(board().rectOf(id));
        s.rect = r.adjusted(-20, -20, 20, 20);
    } else {
        s.rect = QRectF(0, 0, 260, 180);
        s.rect.moveCenter(m_view->visibleCenter());
    }
    m_doc->change(sel.isEmpty() ? tr("New Background Shape") : tr("New Background Shape Around Selection"), [&] {
        board().shapes << s;
    });
    scene()->selectIds({s.id});
}

void MindMapController::toggleMagnetic()
{
    const QList<int> shapes = scene()->selectedShapes();
    if (shapes.isEmpty())
        return;
    const bool on = !board().shape(shapes.first())->magnetic;
    m_doc->change(tr("Magnetic"), [&] {
        for (int id : shapes)
            board().shape(id)->magnetic = on;
    });
}

void MindMapController::toggleFade()
{
    const QList<int> ids = targets();
    if (ids.isEmpty())
        return;
    bool on = true;
    if (const Note *n = board().note(ids.first()))
        on = !n->look.faded;
    else if (const Shape *s = board().shape(ids.first()))
        on = !s->look.faded;
    forItemsLook(tr("Fade"), [on](Appearance &a) { a.faded = on; });
}

void MindMapController::toggleShadow()
{
    const QList<int> ids = targets();
    if (ids.isEmpty())
        return;
    bool on = true;
    if (const Note *n = board().note(ids.first()))
        on = !n->look.shadow;
    else if (const Shape *s = board().shape(ids.first()))
        on = !s->look.shadow;
    forItemsLook(tr("Shadow"), [on](Appearance &a) { a.shadow = on; });
}

void MindMapController::stack()
{
    const QList<int> sel = scene()->selectedNotes();
    if (sel.size() < 2)
        return;
    const int first = sel.first();
    m_doc->change(tr("Stack"), [&] {
        const QPointF origin = board().rectOf(first).center();
        QList<int> rest = sel.mid(1);
        // by distance from the first note
        std::stable_sort(rest.begin(), rest.end(), [&](int a, int b) {
            const QPointF da = board().rectOf(a).center() - origin, db = board().rectOf(b).center() - origin;
            return QPointF::dotProduct(da, da) < QPointF::dotProduct(db, db);
        });
        // notes already below the first one in its stack stay below the new ones
        QList<int> below;
        for (int b : board().stackBelow(first))
            if (!rest.contains(b))
                below << b;
        const QRectF fr = board().rectOf(first);
        double y = fr.bottom();
        for (int id : rest) {
            Note *n = board().note(id);
            n->pos = QPointF(fr.left(), y);
            y += board().rectOf(id).height();
        }
        for (int id : below) {
            Note *n = board().note(id);
            n->pos = QPointF(fr.left(), y);
            y += board().rectOf(id).height();
        }
    });
}

void MindMapController::autoFit()
{
    const QList<int> ids = targets();
    m_doc->change(tr("Auto-Fit"), [&] {
        for (int id : ids) {
            if (Note *n = board().note(id)) {
                if (n->isImage())
                    continue;
                const double old = board().rectOf(id).bottom();
                n->width = qMax(30.0, board().fittingWidth(*n));
                board().restack(id, old);
            } else if (Shape *s = board().shape(id)) {
                // the shape around what lies on it
                QRectF r;
                for (int o : board().overlapping(id))
                    r = r.united(board().rectOf(o));
                if (!r.isNull())
                    s->rect = r.adjusted(-20, -20, 20, 20);
            }
        }
    });
}

void MindMapController::align(int how)
{
    const QList<int> sel = targets();
    if (sel.size() < 2)
        return;
    const QRectF ref = board().rectOf(sel.first());
    m_doc->change(tr("Align"), [&] {
        for (int i = 1; i < sel.size(); ++i) {
            const QRectF r = board().rectOf(sel[i]);
            QPointF d;
            switch (how) {
            case 0: d.setX(ref.left() - r.left()); break;
            case 1: d.setX(ref.right() - r.right()); break;
            case 2: d.setY(ref.top() - r.top()); break;
            case 3: d.setY(ref.bottom() - r.bottom()); break;
            case 4: d.setX(ref.center().x() - r.center().x()); break;
            case 5: d.setY(ref.center().y() - r.center().y()); break;
            }
            if (Note *n = board().note(sel[i]))
                n->pos += d;
            else if (Shape *s = board().shape(sel[i]))
                s->rect.translate(d);
        }
    });
}

void MindMapController::centerInShape(int how)
{
    const QList<int> sel = targets();
    m_doc->change(tr("Center in Background Shape"), [&] {
        for (int id : sel) {
            const QList<int> under = board().shapesUnder(id);
            if (under.isEmpty())
                continue;
            const QRectF sr = board().rectOf(under.last());
            const QRectF r = board().rectOf(id);
            QPointF d;
            if (how == 0 || how == 1)
                d.setX(sr.center().x() - r.center().x());
            if (how == 0 || how == 2)
                d.setY(sr.center().y() - r.center().y());
            if (Note *n = board().note(id))
                n->pos += d;
            else if (Shape *s = board().shape(id)) {
                s->rect.translate(d);
                if (s->magnetic)
                    for (int o : board().overlapping(id))
                        if (Note *on = board().note(o); on && !sel.contains(o))
                            on->pos += d;
            }
        }
    });
}

void MindMapController::distribute(bool horizontal)
{
    QList<int> sel = targets();
    if (sel.size() < 3)
        return;
    std::sort(sel.begin(), sel.end(), [&](int a, int b) {
        return horizontal ? board().rectOf(a).left() < board().rectOf(b).left() : board().rectOf(a).top() < board().rectOf(b).top();
    });
    const QRectF first = board().rectOf(sel.first()), last = board().rectOf(sel.last());
    double total = 0;
    for (int id : sel)
        total += horizontal ? board().rectOf(id).width() : board().rectOf(id).height();
    const double span = horizontal ? last.right() - first.left() : last.bottom() - first.top();
    const double gap = (span - total) / (sel.size() - 1);
    m_doc->change(tr("Distribute"), [&] {
        double at = horizontal ? first.left() : first.top();
        for (int id : sel) {
            const QRectF r = board().rectOf(id);
            QPointF d = horizontal ? QPointF(at - r.left(), 0) : QPointF(0, at - r.top());
            if (Note *n = board().note(id))
                n->pos += d;
            else if (Shape *s = board().shape(id))
                s->rect.translate(d);
            at += (horizontal ? r.width() : r.height()) + gap;
        }
    });
}

void MindMapController::makeSame(bool width)
{
    const QList<int> sel = targets();
    if (sel.size() < 2)
        return;
    const QRectF ref = board().rectOf(sel.first());
    m_doc->change(width ? tr("Make Same Width") : tr("Make Same Height"), [&] {
        for (int i = 1; i < sel.size(); ++i) {
            if (Note *n = board().note(sel[i])) {
                const double old = board().rectOf(n->id).bottom();
                if (width) {
                    n->width = ref.width();
                    if (n->isImage())
                        n->height = n->width * n->image.height() / qMax(1, n->image.width());
                } else if (n->isImage()) {
                    n->height = ref.height();
                    n->width = n->height * n->image.width() / qMax(1, n->image.height());
                }
                board().restack(n->id, old);
            } else if (Shape *s = board().shape(sel[i])) {
                if (width)
                    s->rect.setWidth(ref.width());
                else
                    s->rect.setHeight(ref.height());
            }
        }
    });
}

void MindMapController::splitNote()
{
    NoteItem *ni = scene()->editingNote();
    if (!ni)
        return;
    const int id = ni->id();
    QTextCursor c = ni->textItem()->textCursor();
    QTextDocument *d = ni->textItem()->document();
    QString moved;
    if (c.hasSelection()) {
        moved = c.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        c.removeSelectedText();
    } else {
        QTextCursor tail(d);
        tail.setPosition(c.position());
        tail.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        moved = tail.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        tail.removeSelectedText();
    }
    moved = moved.trimmed();
    scene()->endEditing(); // the shorter note goes into the board
    if (moved.isEmpty())
        return;
    const Note *orig = board().note(id);
    if (!orig)
        return;
    Note n = *orig;
    n.id = board().nextId();
    n.text = moved;
    n.ranges.clear();
    n.connected.clear();
    n.pointsTo.clear();
    m_doc->change(tr("Split"), [&] {
        Note *o = board().note(id);
        o->text = o->text.trimmed();
        const QRectF r = board().rectOf(id);
        n.pos = r.bottomLeft();
        const QList<int> below = board().stackBelow(id);
        board().notes << n;
        const double h = board().rectOf(n.id).height();
        for (int b : below)
            board().note(b)->pos.ry() += h;
    });
    scene()->selectIds({n.id});
}

void MindMapController::mergeNotes()
{
    const QList<int> sel = scene()->selectedNotes();
    if (sel.size() < 2)
        return;
    const int first = sel.first();
    m_doc->change(tr("Merge"), [&] {
        const QPointF origin = board().rectOf(first).center();
        QList<int> rest = sel.mid(1);
        std::stable_sort(rest.begin(), rest.end(), [&](int a, int b) {
            const QPointF da = board().rectOf(a).center() - origin, db = board().rectOf(b).center() - origin;
            return QPointF::dotProduct(da, da) < QPointF::dotProduct(db, db);
        });
        Note *f = board().note(first);
        const double old = board().rectOf(first).bottom();
        const double startWidth = f->width;
        for (int id : rest) {
            Note *o = board().note(id);
            if (!o || o->isImage())
                continue;
            const int base = f->text.size() + 2;
            f->text += QStringLiteral("\n\n") + o->text;
            for (TextRange r : o->ranges) {
                r.start += base;
                f->ranges << r;
            }
            // the lines of the merged notes go to the note that stays
            for (int c : board().connectionsOf(id))
                if (c != first)
                    board().connect(first, c, board().pointsTo(id, c), board().pointsTo(c, id));
            board().remove(id);
        }
        // the longer text: as wide as new notes at least (unless it was made wider)
        if (MindMapPrefs::instance().autoSize)
            f->width = qMax(startWidth, qMin(board().fittingWidth(*f), MindMapPrefs::instance().newNoteWidth));
        board().restack(first, old);
    });
    scene()->selectIds({first});
}

static std::function<void(const QString &)> &documentOpener()
{
    static std::function<void(const QString &)> fn;
    return fn;
}

void MindMapController::setDocumentOpener(std::function<void(const QString &)> fn) { documentOpener() = std::move(fn); }

void MindMapController::openLink(const QString &href)
{
    if (href.startsWith(QLatin1String("qb://")) || href.startsWith(QLatin1String("qbobj://"))) {
        // after the click is handled: opening may replace this map's view (inspector)
        QTimer::singleShot(0, qApp, [href] {
            if (documentOpener())
                documentOpener()(href);
        });
        return;
    }
    QDesktopServices::openUrl(QUrl::fromUserInput(href));
}

// ------------------------------------------------------------------ format

void MindMapController::toggleFont(int which)
{
    if (NoteItem *ni = scene()->editingNote()) {
        // a part of the text
        QTextCursor c = ni->textItem()->textCursor();
        QTextCharFormat cur = c.charFormat();
        QTextCharFormat f;
        switch (which) {
        case 0: f.setFontWeight(cur.fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold); break;
        case 1: f.setFontItalic(!cur.fontItalic()); break;
        case 2: f.setFontUnderline(!cur.fontUnderline()); break;
        case 3: f.setFontStrikeOut(!cur.fontStrikeOut()); break;
        }
        // with a selection: that text; without one: what is typed next
        c.mergeCharFormat(f);
        ni->textItem()->setTextCursor(c);
        return;
    }
    const QList<int> ids = scene()->selectedNotes();
    if (ids.isEmpty())
        return;
    const Note *first = board().note(ids.first());
    const bool on = !(which == 0 ? first->bold : which == 1 ? first->italic : which == 2 ? first->underline : first->strike);
    const QStringList names = {tr("Bold"), tr("Italic"), tr("Underline"), tr("Strikethrough")};
    forNotes(names.value(which), [which, on](Note &n) {
        (which == 0 ? n.bold : which == 1 ? n.italic : which == 2 ? n.underline : n.strike) = on;
        // the whole note: ranges of the same kind are no longer needed
        for (TextRange &r : n.ranges) {
            if (which == 0)
                r.bold = false;
            else if (which == 1)
                r.italic = false;
            else if (which == 2)
                r.underline = false;
            else
                r.strike = false;
        }
        n.ranges.erase(std::remove_if(n.ranges.begin(), n.ranges.end(),
                                      [](const TextRange &r) { return !r.bold && !r.italic && !r.underline && !r.strike && r.link.isEmpty(); }),
                       n.ranges.end());
    });
}

void MindMapController::setAlignment(Align a)
{
    forNotes(tr("Alignment"), [a](Note &n) { n.align = a; });
}

void MindMapController::chooseColor(int which)
{
    const QList<int> ids = targets();
    if (ids.isEmpty())
        return;
    QColor now;
    if (const Note *n = board().note(ids.first()))
        now = which == 0 ? n->look.textColor : which == 1 ? n->look.fill : n->look.borderColor;
    else if (const Shape *s = board().shape(ids.first()))
        now = which == 1 ? s->look.fill : s->look.borderColor;
    const QColor c = QColorDialog::getColor(now.isValid() ? now : Qt::white, m_owner, tr("Color"));
    if (!c.isValid())
        return;
    setColor(which, c);
}

void MindMapController::clearColor(int which)
{
    forItemsLook(tr("Clear Color"), [which](Appearance &a) {
        if (which == 0)
            a.textColor = QColor();
        else if (which == 1)
            a.fill = QColor();
        else
            a.borderColor = QColor();
    });
}

void MindMapController::setBorderWeight(int w)
{
    forItemsLook(tr("Border"), [w](Appearance &a) { a.borderWeight = w; });
}

void MindMapController::setBorderStyle(BorderStyle s)
{
    // a shape chosen for a note without a border gets a thin one, so that the shape shows (0.21)
    forItemsLook(tr("Border Style"), [s](Appearance &a) {
        a.borderStyle = s;
        if (a.borderWeight == 0)
            a.borderWeight = 1;
    });
}

void MindMapController::setWritingDirection(bool rtl)
{
    if (board().settings.rightToLeft == rtl)
        return;
    m_doc->change(tr("Writing Direction"), [&] { board().settings.rightToLeft = rtl; });
}

// ------------------------------------------------------------------ styles

void MindMapController::applyStyle(const NoteStyle &st, const QList<int> &ids)
{
    if (ids.isEmpty() || m_doc->locked())
        return;
    m_doc->change(tr("Apply Note Style"), [&] {
        for (int id : ids) {
            if (Note *n = board().note(id)) {
                const double old = board().rectOf(id).bottom();
                if (st.affectsFont) {
                    n->fontSize = st.fontSize;
                    n->bold = st.bold;
                    n->italic = st.italic;
                    n->underline = st.underline;
                    n->strike = st.strike;
                }
                if (st.affectsAlignment) {
                    n->align = st.align;
                    n->indent = st.indent;
                }
                if (st.affectsTextColor)
                    n->look.textColor = st.look.textColor;
                if (st.affectsBody) {
                    n->look.fill = st.look.fill;
                    n->look.borderColor = st.look.borderColor;
                    n->look.borderWeight = st.look.borderWeight;
                    n->look.borderStyle = st.look.borderStyle;
                    n->look.shadow = st.look.shadow;
                }
                if (st.affectsSize && st.width > 0)
                    n->width = st.width;
                if (st.affectsFade)
                    n->look.faded = st.look.faded;
                board().restack(id, old);
            } else if (Shape *s = board().shape(id)) {
                if (st.affectsBody) {
                    s->look.fill = st.look.fill;
                    s->look.borderColor = st.look.borderColor;
                    s->look.borderWeight = st.look.borderWeight;
                    s->look.borderStyle = st.look.borderStyle;
                    s->look.shadow = st.look.shadow;
                }
                if (st.affectsSize && st.width > 0)
                    s->rect.setSize(QSizeF(st.width, st.height > 0 ? st.height : s->rect.height()));
                if (st.affectsFade)
                    s->look.faded = st.look.faded;
                if (st.affectsMagnetic)
                    s->magnetic = st.magnetic;
            }
        }
    });
}

void MindMapController::applyStyleByIndex(int index)
{
    if (index == 0) {
        // Default: everything back to the look of new notes
        NoteStyle d;
        const Note t = [] {
            Note n;
            const MindMapPrefs &p = MindMapPrefs::instance();
            n.fontSize = p.newNoteSize;
            n.look.fill = p.newNoteFill;
            n.look.borderWeight = p.newNoteBorder;
            return n;
        }();
        d.affectsFont = d.affectsAlignment = d.affectsTextColor = d.affectsBody = d.affectsFade = true;
        d.fontSize = t.fontSize;
        d.look = t.look;
        applyStyle(d, targets());
        return;
    }
    const QList<NoteStyle> l = sortedStyles();
    if (index >= 1 && index <= l.size())
        applyStyle(l[index - 1], targets());
}

static bool styleDialog(QWidget *parent, const QString &title, NoteStyle &s, bool isShape)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    auto *lay = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(s.name, &dlg);
    name->setObjectName(QStringLiteral("mmStyleName"));
    form->addRow(QObject::tr("Name:"), name);
    lay->addLayout(form);
    auto box = [&](const QString &text, bool on, bool enabled) {
        auto *c = new QCheckBox(text, &dlg);
        c->setChecked(on);
        c->setEnabled(enabled);
        lay->addWidget(c);
        return c;
    };
    QCheckBox *font = box(QObject::tr("Include font size and style"), s.affectsFont, !isShape);
    QCheckBox *align = box(QObject::tr("Include text alignment and indent"), s.affectsAlignment, !isShape);
    QCheckBox *color = box(QObject::tr("Include text color"), s.affectsTextColor, !isShape);
    QCheckBox *body = box(QObject::tr("Include background and border style"), s.affectsBody, true);
    QCheckBox *size = box(QObject::tr("Include size"), s.affectsSize, true);
    QCheckBox *fade = box(QObject::tr("Include faded setting"), s.affectsFade, true);
    QCheckBox *mag = box(QObject::tr("Include magnetic setting"), s.affectsMagnetic, isShape);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted || name->text().trimmed().isEmpty())
        return false;
    s.name = name->text().trimmed();
    s.affectsFont = font->isChecked();
    s.affectsAlignment = align->isChecked();
    s.affectsTextColor = color->isChecked();
    s.affectsBody = body->isChecked();
    s.affectsSize = size->isChecked();
    s.affectsFade = fade->isChecked();
    s.affectsMagnetic = mag->isChecked();
    return true;
}

static NoteStyle styleFromItem(const Board &b, int id)
{
    NoteStyle s;
    if (const Note *n = b.note(id)) {
        s.fontSize = n->fontSize;
        s.bold = n->bold;
        s.italic = n->italic;
        s.underline = n->underline;
        s.strike = n->strike;
        s.align = n->align;
        s.indent = n->indent;
        s.look = n->look;
        s.width = n->width;
        s.affectsFont = s.affectsTextColor = s.affectsBody = true;
        s.affectsAlignment = n->align != Align::Left || n->indent;
        s.affectsFade = n->look.faded;
    } else if (const Shape *sh = b.shape(id)) {
        s.look = sh->look;
        s.width = sh->rect.width();
        s.height = sh->rect.height();
        s.magnetic = sh->magnetic;
        s.affectsBody = true;
        s.affectsMagnetic = true;
    }
    return s;
}

void MindMapController::newStyleFromSelection()
{
    const QList<int> sel = targets();
    if (sel.size() != 1)
        return;
    NoteStyle s = styleFromItem(board(), sel.first());
    s.name = tr("New Style");
    s.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
    if (!styleDialog(m_owner, tr("New Note Style"), s, board().shape(sel.first()) != nullptr))
        return;
    m_doc->change(tr("New Note Style"), [&] { board().styles << s; });
}

void MindMapController::redefineStyle(const QString &styleId)
{
    const QList<int> sel = targets();
    if (sel.size() != 1)
        return;
    for (int i = 0; i < board().styles.size(); ++i) {
        if (board().styles[i].id != styleId)
            continue;
        NoteStyle s = styleFromItem(board(), sel.first());
        const NoteStyle old = board().styles[i];
        s.id = old.id;
        s.name = old.name;
        s.affectsFont = old.affectsFont;
        s.affectsAlignment = old.affectsAlignment;
        s.affectsTextColor = old.affectsTextColor;
        s.affectsBody = old.affectsBody;
        s.affectsSize = old.affectsSize;
        s.affectsFade = old.affectsFade;
        s.affectsMagnetic = old.affectsMagnetic;
        if (!styleDialog(m_owner, tr("Redefine Note Style"), s, board().shape(sel.first()) != nullptr))
            return;
        m_doc->change(tr("Redefine Note Style"), [&] { board().styles[i] = s; });
        return;
    }
}

void MindMapController::deleteStyle(const QString &styleId)
{
    m_doc->change(tr("Delete Note Style"), [&] {
        for (int i = 0; i < board().styles.size(); ++i)
            if (board().styles[i].id == styleId) {
                board().styles.removeAt(i);
                break;
            }
    });
}

void MindMapController::copyStyle()
{
    const QList<int> sel = targets();
    if (sel.size() != 1)
        return;
    Board tmp;
    tmp.styles = {styleFromItem(board(), sel.first())};
    tmp.styles.first().name = QStringLiteral("clipboard");
    m_styleClipboard = tmp.toXml();
    updateActions();
}

void MindMapController::pasteStyle()
{
    Board tmp;
    if (m_styleClipboard.isEmpty() || !tmp.fromXml(m_styleClipboard) || tmp.styles.isEmpty())
        return;
    NoteStyle s = tmp.styles.first();
    s.affectsFont = s.affectsTextColor = s.affectsBody = s.affectsAlignment = s.affectsFade = true;
    applyStyle(s, targets());
}

void MindMapController::importStyles()
{
    const QString f = QFileDialog::getOpenFileName(m_owner, tr("Import Note Styles"), QDir::homePath(),
                                                   tr("Mind maps and Scapple files (*.scap)"));
    if (f.isEmpty())
        return;
    Board other;
    QString err;
    if (!other.load(f, &err)) {
        QMessageBox::warning(m_owner, tr("Import Note Styles"), err);
        return;
    }
    const auto answer = QMessageBox::question(m_owner, tr("Import Note Styles"),
                                              tr("%n note style(s) found. Overwrite the styles of this board with them?\n\n"
                                                 "No adds them to the styles of this board.",
                                                 nullptr, int(other.styles.size())),
                                              QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
    if (answer == QMessageBox::Cancel)
        return;
    m_doc->change(tr("Import Note Styles"), [&] {
        if (answer == QMessageBox::Yes)
            board().styles.clear();
        for (NoteStyle s : other.styles) {
            s.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
            board().styles << s;
        }
    });
}

// ------------------------------------------------------------------ selection

void MindMapController::selectStackedBelow()
{
    const QList<int> sel = scene()->selectedNotes();
    if (sel.isEmpty())
        return;
    int top = sel.first();
    if (sel.size() > 1)
        for (int a = board().stackAbove(top); a; a = board().stackAbove(a))
            top = a;
    QList<int> ids{top};
    ids << board().stackBelow(top);
    scene()->selectIds(ids);
}

void MindMapController::selectOverlapping()
{
    QList<int> ids = scene()->selectedIds();
    for (int s : scene()->selectedShapes())
        for (int o : board().overlapping(s))
            if (!ids.contains(o))
                ids << o;
    scene()->selectIds(ids);
}

void MindMapController::selectConnected(bool clusters)
{
    QList<int> ids = scene()->selectedIds();
    if (!clusters) {
        const QList<int> base = ids;
        for (int id : base)
            for (int c : board().connectionsOf(id))
                if (!ids.contains(c))
                    ids << c;
    } else {
        for (int i = 0; i < ids.size(); ++i)
            for (int c : board().connectionsOf(ids[i]))
                if (!ids.contains(c))
                    ids << c;
    }
    scene()->selectIds(ids);
}

void MindMapController::invertSelection()
{
    const QList<int> sel = scene()->selectedIds();
    QList<int> ids;
    for (const Note &n : board().notes)
        if (!sel.contains(n.id))
            ids << n.id;
    for (const Shape &s : board().shapes)
        if (!sel.contains(s.id))
            ids << s.id;
    scene()->selectIds(ids);
}

void MindMapController::jumpToSelection()
{
    if (NoteItem *ni = scene()->editingNote()) {
        m_view->centerOn(ni->mapToScene(ni->frame().center()));
        return;
    }
    QRectF r;
    for (int id : targets())
        r = r.united(scene()->liveRect(id));
    if (!r.isNull())
        m_view->centerOn(r.center());
}

void MindMapController::zoomToSelection()
{
    QRectF r;
    for (int id : targets())
        r = r.united(scene()->liveRect(id));
    m_view->zoomToRect(r);
}

void MindMapController::zoomToRandom()
{
    if (board().notes.isEmpty())
        return;
    const int i = int(QRandomGenerator::global()->bounded(board().notes.size()));
    const int id = board().notes[i].id;
    scene()->selectIds({id});
    m_view->zoomToRect(scene()->liveRect(id).adjusted(-60, -60, 60, 60));
}

void MindMapController::addLink()
{
    NoteItem *ni = scene()->editingNote();
    if (!ni)
        return;
    QTextCursor c = ni->textItem()->textCursor();
    const QString existing = c.charFormat().anchorHref();
    if (!c.hasSelection() && existing.isEmpty()) {
        QMessageBox::information(m_owner, tr("Add Link"), tr("Select the words that are to become the link first."));
        return;
    }
    bool ok = false;
    const QString url = QInputDialog::getText(m_owner, existing.isEmpty() ? tr("Add Link") : tr("Edit Link"),
                                              tr("Address (a web address, or a file: file:///home/...):"), QLineEdit::Normal, existing, &ok)
                            .trimmed();
    if (!ok)
        return;
    if (!c.hasSelection())
        c.select(QTextCursor::WordUnderCursor);
    QTextCharFormat f;
    if (url.isEmpty()) {
        f.setAnchor(false);
        f.setAnchorHref(QString());
        f.setFontUnderline(false);
        f.setForeground(QBrush());
        c.mergeCharFormat(f);
        return;
    }
    f.setAnchor(true);
    f.setAnchorHref(QUrl::fromUserInput(url).toString());
    f.setFontUnderline(true);
    f.setForeground(QColor(30, 90, 200));
    c.mergeCharFormat(f);
    ni->textItem()->setFocus();
}

// ------------------------------------------------------------------ clipboard, drops

void MindMapController::copy(bool cut)
{
    if (NoteItem *ni = scene()->editingNote()) {
        QTextCursor c = ni->textItem()->textCursor();
        if (!c.hasSelection())
            return;
        QApplication::clipboard()->setText(c.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n')));
        if (cut && !m_doc->locked())
            c.removeSelectedText();
        return;
    }
    const QList<int> ids = targets();
    if (ids.isEmpty())
        return;
    Board part;
    part.styles.clear();
    part.settings = board().settings;
    QSet<int> set(ids.begin(), ids.end());
    for (int id : ids) {
        if (const Note *n = board().note(id))
            part.notes << *n;
        else if (const Shape *s = board().shape(id))
            part.shapes << *s;
    }
    // only the lines among what is copied
    for (Note &n : part.notes) {
        n.connected &= set;
        n.pointsTo &= set;
    }
    for (Shape &s : part.shapes) {
        s.connected &= set;
        s.pointsTo &= set;
    }
    for (auto it = board().lineLabels.cbegin(); it != board().lineLabels.cend(); ++it) {
        const QStringList ab = it.key().split(QLatin1Char('-'));
        if (ab.size() == 2 && set.contains(ab[0].toInt()) && set.contains(ab[1].toInt()))
            part.lineLabels.insert(it.key(), it.value());
    }
    // the color and middle icon of those lines too (0.21)
    for (auto it = board().lineLooks.cbegin(); it != board().lineLooks.cend(); ++it) {
        const QStringList ab = it.key().split(QLatin1Char('-'));
        if (ab.size() == 2 && set.contains(ab[0].toInt()) && set.contains(ab[1].toInt()))
            part.lineLooks.insert(it.key(), it.value());
    }
    auto *md = new QMimeData;
    md->setData(QLatin1String(kBoardMime), part.toXml());
    md->setText(board().toPlainText(set, QString()).trimmed());
    if (ids.size() == 1)
        if (const Note *n = board().note(ids.first()); n && n->isImage())
            md->setImageData(n->image);
    QApplication::clipboard()->setMimeData(md);
    if (cut)
        deleteSelection();
    updateActions();
}

void MindMapController::paste(const QPointF &scenePos, bool atPoint)
{
    if (m_doc->locked())
        return;
    const QMimeData *md = QApplication::clipboard()->mimeData();
    if (!md)
        return;
    if (NoteItem *ni = scene()->editingNote()) {
        if (md->hasText())
            ni->textItem()->textCursor().insertText(md->text());
        return;
    }
    QPointF at = scenePos;
    if (!atPoint) {
        // Scapple: roughly in the middle of the view, a little higher
        const QRectF v = m_view->visibleRect();
        at = QPointF(v.center().x(), v.top() + v.height() * 0.35);
    }
    // under a single selected note: into its stack
    dropData(md, at, atPoint ? Qt::NoModifier : Qt::KeyboardModifiers(0x40000000));
}

bool MindMapController::canAccept(const QMimeData *md) const
{
    if (!md)
        return false;
    if (md->hasFormat(QLatin1String(kBoardMime)) || md->hasFormat(QLatin1String(kStyleMime)) || md->hasImage() || md->hasText())
        return true;
    if (md->hasFormat(QLatin1String(kItemsMime)))
        return true;
    if (md->hasUrls())
        for (const QUrl &u : md->urls())
            if (u.isLocalFile())
                return true;
    return false;
}

void MindMapController::dropData(const QMimeData *md, const QPointF &sp, Qt::KeyboardModifiers mods)
{
    if (m_doc->locked() || !md)
        return;
    const bool fromPaste = mods == Qt::KeyboardModifiers(0x40000000);
    if (fromPaste)
        mods = Qt::NoModifier;
    // a style dragged out of the style list: a note in that style
    if (md->hasFormat(QLatin1String(kStyleMime))) {
        const QString id = QString::fromUtf8(md->data(QLatin1String(kStyleMime)));
        int nid = 0;
        m_doc->begin(tr("New Note"));
        nid = newNoteAt(sp, mods);
        for (const NoteStyle &s : board().styles)
            if (s.id == id)
                applyStyle(s, {nid});
        m_doc->commit();
        if (nid)
            scene()->startEditing(nid, true);
        return;
    }
    // notes of a board (copied or dragged)
    if (md->hasFormat(QLatin1String(kBoardMime))) {
        Board part;
        if (!part.fromXml(md->data(QLatin1String(kBoardMime))))
            return;
        QRectF r = part.itemsRect();
        QPointF offset = sp - r.topLeft() - QPointF(r.width() / 2, 0);
        const QList<int> sel = scene()->selectedNotes();
        const int stackUnder = (fromPaste && sel.size() == 1) ? sel.first() : 0;
        if (stackUnder)
            offset = board().rectOf(stackUnder).bottomLeft() - r.topLeft();
        QHash<int, int> map;
        int next = board().nextId();
        QList<int> created;
        m_doc->change(tr("Paste"), [&] {
            for (const Shape &s : part.shapes) {
                Shape c = s;
                c.id = next++;
                c.rect.translate(offset);
                c.connected.clear();
                c.pointsTo.clear();
                map.insert(s.id, c.id);
                board().shapes << c;
                created << c.id;
            }
            QList<int> below = stackUnder ? board().stackBelow(stackUnder) : QList<int>();
            double y = stackUnder ? board().rectOf(stackUnder).bottom() : 0;
            for (const Note &n : part.notes) {
                Note c = n;
                c.id = next++;
                c.pos += offset;
                c.connected.clear();
                c.pointsTo.clear();
                if (stackUnder) {
                    c.pos = QPointF(board().rectOf(stackUnder).left(), y);
                    c.width = board().rectOf(stackUnder).width();
                }
                map.insert(n.id, c.id);
                board().notes << c;
                if (stackUnder)
                    y += board().rectOf(c.id).height();
                created << c.id;
            }
            if (stackUnder) {
                double extra = y - board().rectOf(stackUnder).bottom();
                for (int b : below)
                    board().note(b)->pos.ry() += extra;
            }
            for (const Note &n : part.notes)
                for (int o : n.connected)
                    if (map.contains(o))
                        board().connect(map.value(n.id), map.value(o), n.pointsTo.contains(o), part.pointsTo(o, n.id));
            for (const Shape &s : part.shapes)
                for (int o : s.connected)
                    if (map.contains(o))
                        board().connect(map.value(s.id), map.value(o), s.pointsTo.contains(o), part.pointsTo(o, s.id));
            for (auto it = part.lineLabels.cbegin(); it != part.lineLabels.cend(); ++it) {
                const QStringList ab = it.key().split(QLatin1Char('-'));
                if (ab.size() == 2)
                    board().setLineLabel(map.value(ab[0].toInt()), map.value(ab[1].toInt()), it.value());
            }
            for (auto it = part.lineLooks.cbegin(); it != part.lineLooks.cend(); ++it) {
                const QStringList ab = it.key().split(QLatin1Char('-'));
                if (ab.size() == 2)
                    board().setLineLook(map.value(ab[0].toInt()), map.value(ab[1].toInt()), it.value());
            }
        });
        scene()->selectIds(created);
        return;
    }
    // documents of the binder: a stack of notes with their titles (and synopses), linked to them
    if (md->hasFormat(QLatin1String(kItemsMime)) && m_doc->project()) {
        const QList<BinderItem *> items = BinderModel::itemsFromMime(md, m_doc->project());
        if (items.isEmpty())
            return;
        Note t = templateNote();
        QList<int> created;
        m_doc->change(tr("Add Documents"), [&] {
            QPointF at = sp - QPointF(t.width / 2, 10);
            for (BinderItem *it : items) {
                Note n = t;
                n.id = board().nextId();
                n.pos = at;
                const QString title = it->title.isEmpty() ? tr("Untitled") : it->title;
                n.text = it->synopsis.trimmed().isEmpty() ? title : title + QStringLiteral("\n") + it->synopsis.trimmed();
                TextRange link;
                link.start = 0;
                link.length = title.size();
                link.link = QStringLiteral("qb://") + it->id;
                link.bold = !it->synopsis.trimmed().isEmpty();
                n.ranges = {link};
                board().notes << n;
                at.ry() += board().rectOf(n.id).height();
                created << n.id;
            }
        });
        scene()->selectIds(created);
        return;
    }
    // files: pictures become picture notes, text files notes (one, or one per part)
    QList<QUrl> files;
    if (md->hasUrls())
        for (const QUrl &u : md->urls())
            if (u.isLocalFile())
                files << u;
    if (!files.isEmpty()) {
        QList<int> created;
        QStringList textFiles;
        QList<QImage> images;
        for (const QUrl &u : files) {
            const QString path = u.toLocalFile();
            QImageReader reader(path);
            if (reader.canRead()) {
                QImage img = reader.read();
                if (!img.isNull())
                    images << img;
            } else if (importer::classify(path) == importer::FileKind::Text) {
                textFiles << path;
            }
        }
        QStringList parts;
        for (const QString &path : textFiles) {
            QTextDocument doc;
            QString err;
            Formatting f;
            if (m_doc->project())
                f = m_doc->project()->formatting;
            if (!importer::readTextFile(path, &doc, f, false, &err))
                continue;
            parts << doc.toPlainText().trimmed();
        }
        if (textFiles.size() == 1 && !parts.isEmpty()) {
            // one file: split into notes?
            QDialog dlg(m_owner);
            dlg.setWindowTitle(tr("Import Text"));
            auto *lay = new QVBoxLayout(&dlg);
            auto *split = new QCheckBox(tr("Split into multiple notes separated by:"), &dlg);
            split->setObjectName(QStringLiteral("mmSplitImport"));
            auto *sep = new QLineEdit(QStringLiteral("\\n\\n"), &dlg);
            sep->setObjectName(QStringLiteral("mmSplitSeparator"));
            sep->setToolTip(tr("\\n stands for a line break; two line breaks: an empty line"));
            lay->addWidget(new QLabel(QFileInfo(textFiles.first()).fileName(), &dlg));
            lay->addWidget(split);
            lay->addWidget(sep);
            auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            lay->addWidget(bb);
            if (dlg.exec() != QDialog::Accepted)
                return;
            if (split->isChecked())
                parts = splitText(parts.first(), sep->text());
        }
        Note t = templateNote();
        m_doc->change(tr("Import"), [&] {
            QPointF at = sp - QPointF(t.width / 2, 10);
            for (const QImage &img : images) {
                Note n;
                n.id = board().nextId();
                n.image = img.width() > 1600 ? img.scaledToWidth(1600, Qt::SmoothTransformation) : img;
                n.width = qMin(240.0, double(img.width()));
                n.height = n.width * img.height() / qMax(1, img.width());
                n.pos = at;
                board().notes << n;
                created << n.id;
                at += QPointF(24, 24);
            }
            for (const QString &p : parts) {
                Note n = t;
                n.id = board().nextId();
                n.text = p;
                n.pos = at;
                board().notes << n;
                at.ry() += board().rectOf(n.id).height(); // a stack
                created << n.id;
            }
        });
        scene()->selectIds(created);
        return;
    }
    if (md->hasImage()) {
        const QImage img = qvariant_cast<QImage>(md->imageData());
        if (img.isNull())
            return;
        Note n;
        n.id = board().nextId();
        n.image = img;
        n.width = img.width(); // pasted pictures keep their own size
        n.height = img.height();
        n.pos = sp - QPointF(n.width / 2, n.height / 2);
        m_doc->change(tr("Paste"), [&] { board().notes << n; });
        scene()->selectIds({n.id});
        return;
    }
    if (md->hasText() && !md->text().trimmed().isEmpty()) {
        Note n = templateNote();
        n.id = board().nextId();
        n.text = md->text().trimmed();
        n.pos = sp - QPointF(n.width / 2, 10);
        const QList<int> sel = scene()->selectedNotes();
        m_doc->change(tr("Paste"), [&] {
            if (fromPaste && sel.size() == 1) {
                const QRectF r = board().rectOf(sel.first());
                const QList<int> below = board().stackBelow(sel.first());
                n.pos = r.bottomLeft();
                n.width = r.width();
                board().notes << n;
                const double h = board().rectOf(n.id).height();
                for (int b : below)
                    board().note(b)->pos.ry() += h;
            } else {
                board().notes << n;
            }
        });
        scene()->selectIds({n.id});
    }
}

// ------------------------------------------------------------------ binder

void MindMapController::addToBinder()
{
    Project *p = m_doc->project();
    BinderItem *owner = p ? p->item(m_doc->itemId()) : nullptr;
    if (!owner)
        return;
    QList<int> ids = scene()->selectedNotes();
    QSet<int> only(ids.begin(), ids.end());
    const QList<int> order = board().readingOrder(only);
    if (order.isEmpty())
        return;
    const auto answer = QMessageBox::question(m_owner, tr("Add Notes to the Binder"),
                                              tr("%n note(s) become documents inside “%1”, in the reading order of the board "
                                                 "(left to right, top to bottom; connected notes after the note they hang on).",
                                                 nullptr, int(order.size()))
                                                  .arg(owner->title),
                                              QMessageBox::Ok | QMessageBox::Cancel);
    if (answer != QMessageBox::Ok)
        return;
    for (int id : order) {
        const Note *n = board().note(id);
        if (!n)
            continue;
        QString title = n->isImage() ? tr("Picture") : n->text.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (title.size() > 60)
            title = title.left(60).trimmed() + QStringLiteral("...");
        BinderItem *it = p->model()->createItem(owner, -1, ItemType::Text, title);
        if (!it)
            continue;
        QTextDocument doc;
        if (n->isImage()) {
            doc.setPlainText(QString());
        } else {
            QTextCursor c(&doc);
            c.insertText(n->text);
        }
        p->docs()->setContent(it->id, &doc);
        p->notifyItemChanged(it);
    }
    p->markIndexDirty();
}

// ------------------------------------------------------------------ exports

bool MindMapController::exportPdf(const QString &path, bool background, bool fadedFull, QString *error)
{
    const QRectF src = scene()->contentRect().adjusted(-24, -24, 24, 24);
    if (src.isEmpty()) {
        if (error)
            *error = tr("The board is empty.");
        return false;
    }
    QPdfWriter pdf(path);
    pdf.setTitle(m_doc->project() && m_doc->project()->item(m_doc->itemId()) ? m_doc->project()->item(m_doc->itemId())->title : tr("Mind Map"));
    pdf.setCreator(QStringLiteral("Quillbinder"));
    pdf.setResolution(72);
    // one page as large as the board (in points)
    pdf.setPageSize(QPageSize(src.size(), QPageSize::Point, QString(), QPageSize::ExactMatch));
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter p;
    if (!p.begin(&pdf)) {
        if (error)
            *error = tr("Cannot write %1").arg(path);
        return false;
    }
    scene()->render(&p, QRectF(QPointF(0, 0), src.size()), src, background, fadedFull);
    p.end();
    return true;
}

bool MindMapController::exportPng(const QString &path, QString *error)
{
    const QRectF src = scene()->contentRect().adjusted(-24, -24, 24, 24);
    if (src.isEmpty()) {
        if (error)
            *error = tr("The board is empty.");
        return false;
    }
    const double scale = qMin(2.0, 8000.0 / qMax(src.width(), src.height()));
    QImage img((src.size() * scale).toSize(), QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    scene()->render(&p, QRectF(QPointF(0, 0), src.size() * scale), src, true, false);
    p.end();
    if (!img.save(path, "PNG")) {
        if (error)
            *error = tr("Cannot write %1").arg(path);
        return false;
    }
    return true;
}

bool MindMapController::exportText(const QString &path, int kind, bool selectedOnly, const QString &separator, bool notesField, QString *error)
{
    QSet<int> only;
    if (selectedOnly)
        for (int id : scene()->selectedNotes())
            only.insert(id);
    QByteArray bytes;
    const QString title = m_doc->project() && m_doc->project()->item(m_doc->itemId()) ? m_doc->project()->item(m_doc->itemId())->title : tr("Mind Map");
    switch (kind) {
    case 0: bytes = board().toPlainText(only, separator).toUtf8(); break;
    case 1: bytes = board().toPlainList(only).toUtf8(); break;
    case 2: {
        // the notes with their bold, italics ... one after the other
        QTextDocument doc;
        QTextCursor c(&doc);
        bool firstNote = true;
        for (int id : board().readingOrder(only)) {
            const Note *n = board().note(id);
            if (!n || n->isImage())
                continue;
            if (!firstNote) {
                c.insertBlock();
                if (!separator.isEmpty()) {
                    c.insertText(separator);
                    c.insertBlock();
                }
            }
            firstNote = false;
            QTextDocument one;
            board().fillDocument(&one, *n);
            c.insertFragment(QTextDocumentFragment(&one));
        }
        Formatting f;
        if (m_doc->project())
            f = m_doc->project()->formatting;
        bytes = rtf::write(&doc, f);
        break;
    }
    case 3: bytes = board().toOpml(only, notesField, title); break;
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        if (error)
            *error = tr("Cannot write %1").arg(path);
        return false;
    }
    return true;
}

int MindMapController::exportImages(const QString &dir, bool selectedOnly)
{
    int n = 0;
    QDir().mkpath(dir);
    const QList<int> sel = scene()->selectedNotes();
    for (const Note &note : board().notes) {
        if (!note.isImage() || (selectedOnly && !sel.contains(note.id)))
            continue;
        QString name = QStringLiteral("%1/picture-%2.png").arg(dir).arg(note.id);
        if (note.image.save(name, "PNG"))
            ++n;
    }
    return n;
}

void MindMapController::exportDialog(int kind)
{
    scene()->endEditing();
    const QString base = m_doc->project() && m_doc->project()->item(m_doc->itemId()) ? m_doc->project()->item(m_doc->itemId())->title : tr("Mind Map");
    const QString start = QDir::homePath() + QLatin1Char('/') + QString(base).replace(QLatin1Char('/'), QLatin1Char('-'));
    QString err;
    bool ok = true;
    if (kind == 0) {
        QDialog dlg(m_owner);
        dlg.setWindowTitle(tr("Export as PDF"));
        auto *lay = new QVBoxLayout(&dlg);
        auto *bg = new QCheckBox(tr("Draw background and use default text color"), &dlg);
        bg->setChecked(true);
        auto *full = new QCheckBox(tr("Draw faded notes at full opacity"), &dlg);
        lay->addWidget(bg);
        lay->addWidget(full);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        lay->addWidget(bb);
        if (dlg.exec() != QDialog::Accepted)
            return;
        const QString f = QFileDialog::getSaveFileName(m_owner, tr("Export as PDF"), start + QStringLiteral(".pdf"), tr("PDF (*.pdf)"));
        if (f.isEmpty())
            return;
        ok = exportPdf(f, bg->isChecked(), full->isChecked(), &err);
    } else if (kind == 1) {
        const QString f = QFileDialog::getSaveFileName(m_owner, tr("Export as PNG"), start + QStringLiteral(".png"), tr("PNG picture (*.png)"));
        if (f.isEmpty())
            return;
        ok = exportPng(f, &err);
    } else if (kind >= 2 && kind <= 5) {
        const int textKind = kind == 2 ? 0 : kind == 3 ? 2 : kind == 4 ? 1 : 3;
        QDialog dlg(m_owner);
        dlg.setWindowTitle(tr("Export"));
        auto *lay = new QVBoxLayout(&dlg);
        auto *selOnly = new QCheckBox(tr("Export selected notes only"), &dlg);
        selOnly->setEnabled(!scene()->selectedNotes().isEmpty());
        lay->addWidget(selOnly);
        QLineEdit *sep = nullptr;
        QCheckBox *notesField = nullptr;
        if (textKind == 0 || textKind == 2) {
            lay->addWidget(new QLabel(tr("Separator (empty: an empty line between the notes):"), &dlg));
            sep = new QLineEdit(&dlg);
            lay->addWidget(sep);
        }
        if (textKind == 3) {
            notesField = new QCheckBox(tr("Create “notes” field (the full text as an OPML note, the beginning as the heading)"), &dlg);
            lay->addWidget(notesField);
        }
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        lay->addWidget(bb);
        if (dlg.exec() != QDialog::Accepted)
            return;
        const QString suffix = textKind == 2 ? QStringLiteral(".rtf") : textKind == 3 ? QStringLiteral(".opml") : QStringLiteral(".txt");
        const QString filter = textKind == 2 ? tr("Rich Text (*.rtf)") : textKind == 3 ? tr("OPML (*.opml)") : tr("Text (*.txt)");
        const QString f = QFileDialog::getSaveFileName(m_owner, tr("Export"), start + suffix, filter);
        if (f.isEmpty())
            return;
        ok = exportText(f, textKind, selOnly->isChecked(), sep ? sep->text() : QString(), notesField && notesField->isChecked(), &err);
    } else if (kind == 6) {
        const QString d = QFileDialog::getExistingDirectory(m_owner, tr("Export Images"), QDir::homePath());
        if (d.isEmpty())
            return;
        const int n = exportImages(d, false);
        QMessageBox::information(m_owner, tr("Export Images"), tr("%n picture(s) saved in %1.", nullptr, n).arg(d));
        return;
    } else if (kind == 7) {
        const QString f = QFileDialog::getSaveFileName(m_owner, tr("Export as Scapple File"), start + QStringLiteral(".scap"),
                                                       tr("Scapple file (*.scap)"));
        if (f.isEmpty())
            return;
        ok = board().save(f, &err);
    }
    if (!ok)
        QMessageBox::warning(m_owner, tr("Export"), err);
}

void MindMapController::importBoard()
{
    const QString f = QFileDialog::getOpenFileName(m_owner, tr("Import Mind Map"), QDir::homePath(),
                                                   tr("Mind maps and Scapple files (*.scap)"));
    if (f.isEmpty())
        return;
    Board other;
    QString err;
    if (!other.load(f, &err)) {
        QMessageBox::warning(m_owner, tr("Import Mind Map"), err);
        return;
    }
    // added to this board, to the right of what is there
    auto *md = new QMimeData;
    Board part = other;
    md->setData(QLatin1String(kBoardMime), part.toXml());
    const QRectF here = board().itemsRect();
    const QRectF there = other.itemsRect();
    QPointF at = here.isNull() ? m_view->visibleCenter() : QPointF(here.right() + 80 + there.width() / 2, here.top());
    dropData(md, at, Qt::NoModifier);
    delete md;
    if (board().settings.font.isEmpty() && !other.settings.font.isEmpty())
        board().settings.font = other.settings.font;
}

// ------------------------------------------------------------------ printing

void MindMapController::pageSetup()
{
    QPageSetupDialog dlg(&m_printer, m_owner);
    if (dlg.exec() == QDialog::Accepted) {
        m_printerSet = true;
        m_view->viewport()->update();
    }
}

void MindMapController::printBoard(QPrinter *printer)
{
    const QRectF src = scene()->contentRect().adjusted(-12, -12, 12, 12);
    if (src.isEmpty())
        return;
    QPainter p(printer);
    const QRectF page = printer->pageLayout().paintRectPixels(printer->resolution());
    const QRectF target(QPointF(0, 0), page.size());
    // scaled to fit one page (proportions kept)
    const double s = qMin(target.width() / src.width(), target.height() / src.height());
    const QRectF dest(0, 0, src.width() * s, src.height() * s);
    scene()->render(&p, dest, src, false, false);
}

void MindMapController::printPreview()
{
    scene()->endEditing();
    QPrintPreviewDialog dlg(&m_printer, m_owner);
    dlg.setWindowTitle(tr("Print Preview"));
    connect(&dlg, &QPrintPreviewDialog::paintRequested, this, [this](QPrinter *pr) { printBoard(pr); });
    dlg.exec();
}

void MindMapController::print()
{
    scene()->endEditing();
    QPrintDialog dlg(&m_printer, m_owner);
    if (dlg.exec() == QDialog::Accepted)
        printBoard(&m_printer);
}

void MindMapController::drawPageGuides(QPainter *p, const QRectF &rect)
{
    if (!board().settings.pageGuides)
        return;
    // the printable area of a sheet at 100 % (points on the board)
    const QRectF page = m_printer.pageLayout().paintRect(QPageLayout::Point);
    if (page.width() < 50 || page.height() < 50)
        return;
    p->save();
    QPen pen(QColor(120, 120, 120, 150), 0, Qt::DashLine);
    pen.setCosmetic(true);
    p->setPen(pen);
    const double w = page.width(), h = page.height();
    for (double x = std::floor(rect.left() / w) * w; x <= rect.right(); x += w)
        p->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    for (double y = std::floor(rect.top() / h) * h; y <= rect.bottom(); y += h)
        p->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    p->restore();
}

// ------------------------------------------------------------------ find

static bool matchAt(const QString &text, int pos, const QString &what, int mode, Qt::CaseSensitivity cs)
{
    if (pos < 0 || pos + what.size() > text.size())
        return false;
    if (QStringView(text).mid(pos, what.size()).compare(what, cs) != 0)
        return false;
    auto word = [&](int i) { return i >= 0 && i < text.size() && (text[i].isLetterOrNumber() || text[i] == QLatin1Char('_')); };
    const bool startOk = !word(pos - 1);
    const bool endOk = !word(pos + what.size());
    switch (mode) {
    case 1: return startOk;            // starts with
    case 2: return startOk && endOk;   // whole word
    case 3: return endOk;              // ends with
    default: return true;              // contains
    }
}

bool MindMapController::find(const QString &text, bool backward, int mode, bool ignoreCase, bool wrap)
{
    if (text.isEmpty())
        return false;
    m_lastFind = text;
    const Qt::CaseSensitivity cs = ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive;
    const QList<int> order = board().readingOrder();
    if (order.isEmpty())
        return false;
    // where to begin: the note being edited (after its selection) or the selected one
    int startIdx = 0, startPos = 0;
    if (NoteItem *ni = scene()->editingNote()) {
        startIdx = qMax(0, int(order.indexOf(ni->id())));
        const QTextCursor c = ni->textItem()->textCursor();
        startPos = backward ? c.selectionStart() - 1 : c.selectionEnd();
    } else if (!scene()->selectedNotes().isEmpty()) {
        startIdx = qMax(0, int(order.indexOf(scene()->selectedNotes().first())));
        startPos = backward ? -1 : 0;
        if (backward)
            startIdx = (startIdx - 1 + order.size()) % order.size(), startPos = INT_MAX;
    } else {
        startIdx = backward ? order.size() - 1 : 0;
        startPos = backward ? INT_MAX : 0;
    }
    for (int step = 0; step <= order.size(); ++step) {
        int idx = backward ? startIdx - step : startIdx + step;
        if (!wrap && (idx < 0 || idx >= order.size()))
            return false;
        idx = ((idx % order.size()) + order.size()) % order.size();
        const int id = order[idx];
        const Note *n = board().note(id);
        if (!n || n->isImage())
            continue;
        QString t = n->text;
        if (NoteItem *ni = scene()->editingNote(); ni && ni->id() == id)
            t = ni->textItem()->document()->toPlainText();
        int found = -1;
        if (!backward) {
            for (int i = step == 0 ? qMax(0, startPos) : 0; i + text.size() <= t.size(); ++i)
                if (matchAt(t, i, text, mode, cs)) {
                    found = i;
                    break;
                }
        } else {
            for (int i = qMin(t.size() - int(text.size()), step == 0 ? startPos : INT_MAX - 1); i >= 0; --i)
                if (matchAt(t, i, text, mode, cs)) {
                    found = i;
                    break;
                }
        }
        if (found >= 0) {
            scene()->startEditing(id, false);
            NoteItem *ni = scene()->noteItem(id);
            QTextCursor c = ni->textItem()->textCursor();
            c.setPosition(found);
            c.setPosition(found + text.size(), QTextCursor::KeepAnchor);
            ni->textItem()->setTextCursor(c);
            m_view->ensureItemVisible(id);
            return true;
        }
    }
    return false;
}

bool MindMapController::replaceCurrent(const QString &with)
{
    NoteItem *ni = scene()->editingNote();
    if (!ni || m_doc->locked())
        return false;
    QTextCursor c = ni->textItem()->textCursor();
    if (!c.hasSelection())
        return false;
    c.insertText(with);
    return true;
}

int MindMapController::replaceAll(const QString &text, const QString &with, int mode, bool ignoreCase, bool selectedOnly)
{
    if (text.isEmpty() || m_doc->locked())
        return 0;
    scene()->endEditing();
    const Qt::CaseSensitivity cs = ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive;
    const QList<int> sel = scene()->selectedNotes();
    int count = 0;
    auto replaceIn = [&](QString &t) {
        QString out;
        int i = 0;
        int n = 0;
        while (i < t.size()) {
            if (matchAt(t, i, text, mode, cs)) {
                out += with;
                i += text.size();
                ++n;
            } else {
                out += t[i++];
            }
        }
        if (n)
            t = out;
        return n;
    };
    m_doc->change(tr("Replace All"), [&] {
        for (Note &n : board().notes) {
            if (n.isImage() || (selectedOnly && !sel.contains(n.id)))
                continue;
            const double old = board().rectOf(n.id).bottom();
            const int k = replaceIn(n.text);
            if (k) {
                n.ranges.clear(); // the places changed
                count += k;
                board().restack(n.id, old);
            }
        }
        // labels of the lines of those notes as well
        for (auto it = board().lineLabels.begin(); it != board().lineLabels.end(); ++it) {
            const QStringList ab = it.key().split(QLatin1Char('-'));
            if (selectedOnly && (ab.size() != 2 || !(sel.contains(ab[0].toInt()) || sel.contains(ab[1].toInt()))))
                continue;
            count += replaceIn(it.value());
        }
    });
    return count;
}

void MindMapController::useSelectionForFind()
{
    QString t;
    if (NoteItem *ni = scene()->editingNote())
        t = ni->textItem()->textCursor().selectedText();
    else if (!scene()->selectedNotes().isEmpty())
        t = board().note(scene()->selectedNotes().first())->text;
    t = t.replace(QChar::ParagraphSeparator, QLatin1Char(' ')).trimmed();
    if (t.isEmpty())
        return;
    m_lastFind = t;
    if (m_find)
        m_find->setFindText(t);
}

// ------------------------------------------------------------------ options

void MindMapController::showOptions()
{
    MindMapOptionsDialog dlg(m_owner);
    if (dlg.exec() == QDialog::Accepted) {
        scene()->syncAll();
        m_view->viewport()->update();
    }
}

} // namespace qb
