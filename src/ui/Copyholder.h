#pragma once
// A copyholder: a second, plain view inside an editor that shows one other
// document for reference (text or a picture / PDF). It has a narrow header
// bar and no footer; it never shows a corkboard, an outliner or combined text.

#include "core/Project.h"

#include <QPointer>
#include <QWidget>

class QLabel;
class QStackedWidget;
class QToolButton;

namespace qb {

class TextColumnView;
class TextEditor;
class MediaView;

class Copyholder : public QWidget
{
    Q_OBJECT
public:
    explicit Copyholder(QWidget *parent = nullptr);
    void setProject(Project *p);
    void showItem(BinderItem *item);
    BinderItem *item() const { return m_item; }
    void clear();
    TextColumnView *textView() const { return m_text; }
    TextEditor *editor() const;
    QWidget *header() const { return m_header; }
    void refreshHeader();
    void setActiveLook(bool on);

signals:
    void closeRequested();
    void quickReferenceRequested(qb::BinderItem *item);
    void placementRequested(const QString &placement); // left | right | top | bottom
    void itemsDropped(const QList<qb::BinderItem *> &items);
    void activated();
    void cursorMoved(qb::TextEditor *editor);
    void linkActivated(const QString &href);
    void noteClicked(const QString &noteId);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void headerMenu(const QPoint &globalPos);

    QPointer<Project> m_project;
    BinderItem *m_item = nullptr;
    QWidget *m_header;
    QLabel *m_icon, *m_title;
    QToolButton *m_quickRef, *m_close;
    QStackedWidget *m_stack;
    TextColumnView *m_text;
    MediaView *m_media;
    QLabel *m_empty;
    bool m_activeLook = false;
};

} // namespace qb
