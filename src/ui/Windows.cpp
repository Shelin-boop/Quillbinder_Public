#include "ui/Windows.h"
#include "ui/Zoom.h"
#include "ui/Commands.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "ui/AppSettings.h"
#include "ui/MediaView.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScrollBar>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSlider>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace qb {

// ------------------------------------------------------------------ composition

CompositionWindow::CompositionWindow(Project *p, const QList<BinderItem *> &items, bool combined,
                                     BinderItem *focusItem, int cursorPos)
    : QWidget(nullptr, Qt::Window), m_project(p)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Composition"));
    const AppSettings &s = AppSettings::instance();
    if (!p->compositionBackground.isEmpty())
        m_background = QPixmap(p->compositionBackground);

    // composition mode has its own paper; the pages of the page view rest meanwhile
    p->docs()->suspendPageView(true);
    m_pagesSuspended = true;
    m_view = new TextColumnView(this);
    m_view->setFollowsPageView(false);
    m_view->setProject(p);
    m_view->setCustomColors(theme::color(theme::Role::CompositionText), theme::color(theme::Role::CompositionPaper),
                            QColor(0, 0, 0, 0));
    m_view->viewport()->setAutoFillBackground(false);
    m_view->setAttribute(Qt::WA_TranslucentBackground);
    // A style sheet on the view itself would freeze the palettes of the
    // editors inside it (colors could not follow the theme while the window
    // is open), so only the scroll bar gets one.
    m_view->verticalScrollBar()->setStyleSheet(QStringLiteral("QScrollBar:vertical { width: 8px; }"));
    m_view->setTypewriter(true);

    // side panel (inspector-like)
    m_side = new QWidget(this);
    m_side->setAutoFillBackground(true);
    auto themed = [this] {
        const QColor paper = theme::color(theme::Role::CompositionPaper), text = theme::color(theme::Role::CompositionText);
        m_view->setCustomColors(text, paper, QColor(0, 0, 0, 0));
        QPalette sp = QApplication::palette();
        sp.setColor(QPalette::Window, paper.lightness() < 128 ? paper.darker(115) : paper.darker(106));
        sp.setColor(QPalette::WindowText, text);
        sp.setColor(QPalette::Base, paper);
        sp.setColor(QPalette::Text, text);
        QColor faded = text;
        faded.setAlpha(120);
        sp.setColor(QPalette::PlaceholderText, faded);
        m_side->setPalette(sp);
        update();
    };
    themed();
    connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, themed);
    auto *sl = new QVBoxLayout(m_side);
    m_sideTitle = new QLineEdit(m_side);
    m_sideSynopsis = new QPlainTextEdit(m_side);
    m_sideSynopsis->setPlaceholderText(tr("Synopsis"));
    m_sideSynopsis->setMaximumHeight(140);
    m_sideNotes = new TextEditor(m_side);
    m_sideNotes->setLineWrapMode(QTextEdit::WidgetWidth);
    sl->addWidget(m_sideTitle);
    sl->addWidget(m_sideSynopsis);
    sl->addWidget(new QLabel(tr("Notes"), m_side));
    sl->addWidget(m_sideNotes, 1);
    m_side->hide();
    connect(m_sideSynopsis, &QPlainTextEdit::textChanged, this, [this] {
        BinderItem *it = m_view->activeItem();
        if (it && m_project && it->synopsis != m_sideSynopsis->toPlainText()) {
            it->synopsis = m_sideSynopsis->toPlainText();
            m_project->notifyItemChanged(it, false);
        }
    });
    connect(m_sideTitle, &QLineEdit::editingFinished, this, [this] {
        BinderItem *it = m_view->activeItem();
        if (it && m_project && it->title != m_sideTitle->text())
            m_project->model()->setData(m_project->model()->indexFor(it), m_sideTitle->text(), Qt::EditRole);
    });
    connect(m_view, &TextColumnView::activeItemChanged, this, &CompositionWindow::updateInspector);

    // control bar
    m_bar = new QWidget(this);
    m_bar->setAutoFillBackground(true);
    QPalette bp = m_bar->palette();
    bp.setColor(QPalette::Window, QColor(20, 22, 26, 235));
    bp.setColor(QPalette::WindowText, QColor(220, 220, 220));
    bp.setColor(QPalette::ButtonText, QColor(220, 220, 220));
    m_bar->setPalette(bp);
    auto *bl = new QHBoxLayout(m_bar);
    bl->setContentsMargins(12, 6, 12, 6);
    m_zoom = new QComboBox(m_bar);
    for (int z : {100, 110, 125, 150, 175, 200, 250})
        m_zoom->addItem(QStringLiteral("%1%").arg(z), z);
    bl->addWidget(new QLabel(tr("Zoom"), m_bar));
    bl->addWidget(m_zoom);
    bl->addSpacing(12);
    bl->addWidget(new QLabel(tr("Paper width"), m_bar));
    m_width = new QSlider(Qt::Horizontal, m_bar);
    m_width->setRange(25, 95);
    m_width->setValue(int(s.compositionPaperWidth * 100));
    m_width->setMaximumWidth(160);
    bl->addWidget(m_width);
    bl->addSpacing(12);
    bl->addWidget(new QLabel(tr("Background fade"), m_bar));
    m_fade = new QSlider(Qt::Horizontal, m_bar);
    m_fade->setRange(0, 100);
    m_fade->setValue(int(s.compositionFade * 100));
    m_fade->setMaximumWidth(140);
    bl->addWidget(m_fade);
    bl->addStretch(1);
    m_count = new QLabel(m_bar);
    bl->addWidget(m_count);
    bl->addStretch(1);
    auto *typewriter = new QToolButton(m_bar);
    typewriter->setText(tr("Typewriter"));
    typewriter->setCheckable(true);
    typewriter->setChecked(true);
    bl->addWidget(typewriter);
    auto *insp = new QToolButton(m_bar);
    insp->setText(tr("Inspector"));
    insp->setCheckable(true);
    bl->addWidget(insp);
    auto *exit = new QToolButton(m_bar);
    exit->setText(tr("Exit (Esc)"));
    bl->addWidget(exit);
    m_bar->hide();

    m_oldZoom = p->docs()->zoom();
    const int wantZoom = s.compositionZoomPercent;
    m_zoom->setCurrentIndex(qMax(0, m_zoom->findData(wantZoom)));
    p->docs()->setZoom(wantZoom / 100.0);

    // Ctrl + mouse wheel (0.21): the zoom list of the bar, one entry per notch
    zoom::setHandler(this, [this](int steps) {
        const int i = qBound(0, m_zoom->currentIndex() + steps, m_zoom->count() - 1);
        if (i == m_zoom->currentIndex())
            return;
        m_zoom->setCurrentIndex(i);
        emit m_zoom->activated(i);
    });
    connect(m_zoom, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        const int z = m_zoom->itemData(i).toInt();
        AppSettings::instance().compositionZoomPercent = z;
        AppSettings::instance().save();
        if (m_project)
            m_project->docs()->setZoom(z / 100.0);
        updateLayout();
    });
    connect(m_width, &QSlider::valueChanged, this, [this](int v) {
        AppSettings::instance().compositionPaperWidth = v / 100.0;
        updateLayout();
    });
    connect(m_width, &QSlider::sliderReleased, this, [] { AppSettings::instance().save(); });
    connect(m_fade, &QSlider::valueChanged, this, [this](int v) {
        AppSettings::instance().compositionFade = v / 100.0;
        update();
    });
    connect(m_fade, &QSlider::sliderReleased, this, [] { AppSettings::instance().save(); });
    connect(typewriter, &QToolButton::toggled, m_view, &TextColumnView::setTypewriter);
    connect(insp, &QToolButton::toggled, this, [this](bool on) {
        m_side->setVisible(on);
        updateInspector();
        updateLayout();
    });
    connect(exit, &QToolButton::clicked, this, &QWidget::close);
    connect(p->docs(), &DocumentStore::countsUpdated, this, [this] {
        int words = 0;
        for (BinderItem *it : m_view->items())
            words += it->wordCount;
        m_count->setText(tr("%1 words").arg(QLocale().toString(words)));
    });

    m_hover.setInterval(150);
    connect(&m_hover, &QTimer::timeout, this, [this] {
        const QPoint pos = mapFromGlobal(QCursor::pos());
        const bool near = rect().contains(pos) && pos.y() > height() - 70;
        if (near != m_bar->isVisible()) {
            m_bar->setVisible(near);
            m_bar->raise();
        }
    });
    m_hover.start();

    showItems(items, combined);
    // File > Options > Behaviors > Composition Mode: the screen it opens on (0.21)
    {
        const QString want = AppSettings::instance().compositionScreen;
        QScreen *target = nullptr;
        if (want == QLatin1String("primary"))
            target = QGuiApplication::primaryScreen();
        else if (want != QLatin1String("current"))
            for (QScreen *sc : QGuiApplication::screens())
                if (sc->name() == want)
                    target = sc;
        if (!target) // the screen of the main window (or the one under the mouse)
            target = QApplication::activeWindow() ? QApplication::activeWindow()->screen() : QGuiApplication::screenAt(QCursor::pos());
        if (target)
            setGeometry(target->geometry());
    }
    showFullScreen();
    if (focusItem) {
        m_view->focusItem(focusItem);
        if (TextEditor *e = m_view->activeEditor()) {
            QTextCursor c = e->textCursor();
            c.setPosition(qBound(0, cursorPos, e->document()->characterCount() - 1));
            e->setTextCursor(c);
        }
    } else if (TextEditor *e = m_view->activeEditor()) {
        e->setFocus();
    }
}

CompositionWindow::~CompositionWindow()
{
    if (m_project && m_pagesSuspended)
        m_project->docs()->suspendPageView(false);
}

void CompositionWindow::showItems(const QList<BinderItem *> &items, bool combined)
{
    m_view->setItems(items, combined);
    m_view->setCustomColors(theme::color(theme::Role::CompositionText), theme::color(theme::Role::CompositionPaper),
                            QColor(0, 0, 0, 0));
    int words = 0;
    for (BinderItem *it : m_view->items())
        words += it->wordCount;
    m_count->setText(tr("%1 words").arg(QLocale().toString(words)));
    updateLayout();
    if (TextEditor *e = m_view->activeEditor())
        e->setFocus();
    updateInspector();
}

void CompositionWindow::updateInspector()
{
    if (!m_side->isVisible())
        return;
    BinderItem *it = m_view->activeItem();
    if (!it || !m_project)
        return;
    m_sideTitle->setText(it->title);
    if (m_sideSynopsis->toPlainText() != it->synopsis)
        m_sideSynopsis->setPlainText(it->synopsis);
    m_sideNotes->setProjectDocument(m_project, it, m_project->docs()->notes(it->id), true);
}

void CompositionWindow::updateLayout()
{
    const int sideW = m_side->isVisible() ? qMin(360, width() / 4) : 0;
    const int w = int((width() - sideW) * AppSettings::instance().compositionPaperWidth);
    m_view->setGeometry((width() - sideW - w) / 2, 0, w, height());
    m_view->setColumnWidthOverride(w - 40);
    m_view->setTopMargin(qMax(24, height() / 9));
    m_side->setGeometry(width() - sideW, 0, sideW, height());
    m_bar->setGeometry(0, height() - 48, width(), 48);
}

void CompositionWindow::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    updateLayout();
}

void CompositionWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const AppSettings &s = AppSettings::instance();
    if (!m_background.isNull())
        p.drawPixmap(rect(), m_background.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    else
        p.fillRect(rect(), theme::color(theme::Role::CompositionBackground));
    if (s.compositionFade > 0) {
        QColor c = theme::color(theme::Role::CompositionBackground);
        c.setAlphaF(float(s.compositionFade));
        p.fillRect(rect(), c);
    }
    // paper
    const QRect paper = m_view->geometry();
    p.fillRect(paper, theme::color(theme::Role::CompositionPaper));
}

void CompositionWindow::keyPressEvent(QKeyEvent *e)
{
    // F11 always closes; Escape unless File > Options > Behaviors > Composition Mode says no (0.21)
    if ((e->key() == Qt::Key_Escape && AppSettings::instance().compositionEscCloses) || e->key() == Qt::Key_F11) {
        close();
        return;
    }
    // the shortcuts of the commands of the main window (they may have been changed there)
    const QKeySequence pressed(e->keyCombination());
    auto is = [&pressed](const char *command) {
        const QKeySequence ks = commands::shortcutOf(QLatin1String(command));
        return !ks.isEmpty() && ks == pressed;
    };
    if (is("prevDoc") || is("nextDoc")) {
        emit navigate(is("nextDoc"));
        return;
    }
    if (is("back") || is("forward")) {
        emit historyRequested(is("back"));
        return;
    }
    QWidget::keyPressEvent(e);
}

void CompositionWindow::closeEvent(QCloseEvent *e)
{
    BinderItem *it = m_view->activeItem();
    int pos = 0;
    if (TextEditor *ed = m_view->activeEditor())
        pos = ed->textCursor().position();
    if (m_project)
        m_project->docs()->setZoom(m_oldZoom);
    if (m_project && m_pagesSuspended)
        m_project->docs()->suspendPageView(false);
    m_pagesSuspended = false;
    emit closed(it, pos);
    QWidget::closeEvent(e);
}

} // namespace qb
