#pragma once
// Drag and drop shared by the views: dragging documents from any widget
// (header icons), dropping them on plain widgets (toolbar buttons, tabs),
// dragging keywords, and the files a drag hands to other programs.
#include "core/Project.h"

#include <QObject>
#include <QPoint>
#include <QStringList>
#include <QUrl>
#include <functional>

class QAbstractButton;
class QDropEvent;
class QMimeData;
class QPixmap;
class QWidget;

namespace qb {

inline constexpr const char *kKeywordsMime = "application/x-quillbinder-keywords";

namespace dnd {

// ---- documents ------------------------------------------------------------
// Start a drag of documents from any widget; returns when the drag is over.
Qt::DropAction startItemDrag(QWidget *source, Project *project, const QList<BinderItem *> &items);
QPixmap dragPixmap(Project *project, const QList<BinderItem *> &items);

// Makes a button a handle for its document: pressing and moving the mouse
// drags the documents `items` returns (a plain click still clicks).
void makeDragHandle(QAbstractButton *button, std::function<Project *()> project,
                    std::function<QList<BinderItem *>()> items);
// The same for a label or any other widget without a click of its own.
void makeDragHandle(QWidget *widget, std::function<Project *()> project, std::function<QList<BinderItem *>()> items);

// Lets a plain widget (toolbar button, label) take drops. `accepts` decides
// while the mouse is over it, `dropped` gets the drop.
class DropTarget : public QObject
{
public:
    DropTarget(QWidget *widget, std::function<bool(const QMimeData *)> accepts,
               std::function<void(const QMimeData *, const QPoint &pos, Qt::KeyboardModifiers)> dropped);
    static bool installedOn(QWidget *widget);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    QWidget *m_widget;
    std::function<bool(const QMimeData *)> m_accepts;
    std::function<void(const QMimeData *, const QPoint &, Qt::KeyboardModifiers)> m_dropped;
};

// ---- files for other programs ----------------------------------------------
// Writes one document (text in the given format: rtf | docx | md | txt; other
// documents as the file they are) into dir; with children, a group also gets
// a folder of its name holding its subdocuments. Names already taken in dir
// get a number. Returns the path of the file (or of the folder if the
// document has no file of its own).
QString exportItem(Project *project, BinderItem *item, const QString &dir, const QString &format, bool notes,
                   bool children);
// File > Export > Files.
struct ExportOptions {
    QString format = QStringLiteral("rtf");  // text: md | rtf | docx | txt | fountain | fdx
    bool children = true;    // subdocuments: groups become folders
    bool notes = false;      // document notes as "<name> (notes).<format>" (scripts: .txt)
    bool synopsis = false;   // in the metadata file, else as "<name> (synopsis).txt"
    bool metadata = false;   // "<name> (metadata).txt": label, status, keywords, dates, custom metadata …
    bool snapshots = false;  // "<name> (snapshots)/<date> <title>.<format>"
    bool comments = true;    // comments and inline annotations stay in the text
};
QString exportItem(Project *project, BinderItem *item, const QString &dir, const ExportOptions &options);
// "Key: value" lines of a document's metadata (the metadata file of an export).
QString metadataText(Project *project, BinderItem *item, bool withSynopsis);
// The files of a drag: a temporary folder that lives until the program ends.
QList<QUrl> exportForDrag(Project *project, const QStringList &ids);
// Connects the document drags with exportForDrag (once, at start-up).
void installFileProvider();

// ---- keywords --------------------------------------------------------------
QMimeData *mimeForKeywords(Project *project, const QStringList &keywordIds);
QStringList keywordsFromMime(const QMimeData *data, Project *project);
// Gives the documents the keywords; withGroups also gives the groups the
// keywords belong to. Returns how many assignments were new.
int assignKeywords(Project *project, const QList<BinderItem *> &items, const QStringList &keywordIds, bool withGroups);
// Keywords dropped on a document in a list: the documents selected there get
// them if the target is one of them, otherwise the target alone.
int dropKeywords(Project *project, BinderItem *target, const QList<BinderItem *> &selected, const QMimeData *data,
                 Qt::KeyboardModifiers modifiers);

// ---- images ----------------------------------------------------------------
// The image a drop carries: an image document of the project, an image file
// or image data. Empty if there is none.
QString imageFileFromMime(const QMimeData *data, Project *project);
bool isImageFile(const QString &path);

} // namespace dnd
} // namespace qb
