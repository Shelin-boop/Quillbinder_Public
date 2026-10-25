#pragma once
// The Objects sheet (0.20): characters, locations, environments, points of
// interest, weather and categories of the writer's own, with templates, colors
// and links (core/Objects.h). Shown as a tab of the inspector (narrow) and in a
// window of its own (wide); both work on the same objects of the project.
#include "core/Objects.h"

#include <QDialog>
#include <QMainWindow>
#include <QPointer>
#include <QTextEdit>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QComboBox;
class QSplitter;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QHBoxLayout;
class QFormLayout;
class QStackedWidget;
class QPushButton;

namespace qb {

class Project;
class BinderItem;
class FlowLayout;

// Text color that can be read on `background` (black or white).
QColor readableOn(const QColor &background);

// Picks an object (or makes a new one) or a document of the binder: a small window
// with a search field and a list.
class ObjectChooser : public QDialog
{
    Q_OBJECT
public:
    enum Mode { Objects, Documents };
    // targets: categories that can be chosen (empty: all); exclude: ids not offered
    ObjectChooser(Project *project, Mode mode, const QStringList &targets, const QStringList &exclude, QWidget *parent = nullptr);
    QString chosenId() const { return m_chosen; }
    QLineEdit *searchField() const { return m_search; }
    QListWidget *list() const { return m_list; }
    // convenience: shows the window, returns the chosen id ("" when canceled)
    static QString choose(Project *project, Mode mode, const QStringList &targets, const QStringList &exclude, QWidget *parent,
                          const QString &title);

private:
    void fill();
    void accept() override;
    QPointer<Project> m_project;
    Mode m_mode;
    QStringList m_targets, m_exclude;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QString m_chosen;
};

// A long text field: formatted text with links to objects, documents and web pages.
// A click on a link follows it; the right-click menu links the selection.
class ObjectTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit ObjectTextEdit(QWidget *parent = nullptr);
    void setProject(Project *p) { m_project = p; }
    void setMarkdownText(const QString &md);
    QString markdownText() const;
    // links the selected text (or inserts `text` as a link when nothing is selected)
    void linkSelection(const QString &href, const QString &text);
    // Formatting from the format bar, the Format menu and its shortcuts (0.21.1):
    // the selection, or what is typed next. Stored in the object's file as Markdown
    // (bold, italic, strikethrough) and HTML spans where Markdown has nothing (text
    // color, highlight, underline); versions 0.20 and 0.21 show these as well.
    enum class Toggle { Bold, Italic, Underline, Strike };
    void toggle(Toggle what);
    bool isOn(Toggle what) const;
    void applyTextColor(const QColor &c);   // invalid: automatic (the field's text color)
    void applyHighlight(const QColor &c);   // invalid: none
    void clearFormatting();
    QColor textColorAtCursor() const;
    QColor highlightAtCursor() const;
    void removeLinkAtCursor();
    // used by the menu; tests call them directly
    void linkToObject();
    void linkToDocument();
signals:
    void linkActivated(const QString &href);
    void edited();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;

private:
    void changeFormat(const std::function<void(QTextCharFormat &)> &fn);
    QPointer<Project> m_project;
    QPoint m_pressPos;
    bool m_loading = false;
};

// A row of colored chips (linked objects or documents) with a "+" button.
class ChipList : public QWidget
{
    Q_OBJECT
public:
    struct Chip {
        QString id;
        QString text;
        QColor color; // invalid: plain
        QString tip;
    };
    explicit ChipList(bool addButton, QWidget *parent = nullptr);
    void setChips(const QList<Chip> &chips);
    QList<Chip> chips() const { return m_chips; }
    QToolButton *addButton() const { return m_add; }
    QToolButton *chipButton(const QString &id) const;
signals:
    void activated(const QString &id);
    void removeRequested(const QString &id);
    void addRequested();

private:
    FlowLayout *m_flow;
    QToolButton *m_add = nullptr;
    QList<Chip> m_chips;
    QList<QToolButton *> m_buttons;
};

// The fields of one object, made from its category's template.
class ObjectForm : public QWidget
{
    Q_OBJECT
public:
    explicit ObjectForm(bool wide, QWidget *parent = nullptr);
    void setProject(Project *p);
    void showObject(const QString &id); // "" = nothing
    QString objectId() const { return m_id; }
    QLineEdit *nameField() const { return m_name; }
    // the editor of a field (QLineEdit, ObjectTextEdit, QComboBox, QCheckBox, ChipList, QLabel for pictures)
    QWidget *fieldWidget(const QString &fieldId) const { return m_fields.value(fieldId); }
    ChipList *backlinks() const { return m_backlinks; }
    // a link field of the object: add an id (object or document)
    void addLink(const QString &fieldId, const QString &id);
    void removeLink(const QString &fieldId, const QString &id);
    void setPicture(const QString &fieldId, const QString &file);
    QToolButton *colorButton() const { return m_colorButton; }
signals:
    void navigate(const QString &objectId);
    void openDocument(qb::BinderItem *item);
    void linkActivated(const QString &href);

private:
    void rebuild();
    void reloadValues();
    void refreshChips();
    void refreshBacklinks();
    void commit();
    void setValue(const QString &fieldId, const QVariant &v);
    QWidget *makeEditor(const objects::FieldDef &f);
    objects::Store *store() const;
    QPointer<Project> m_project;
    bool m_wide;
    QString m_id;
    QString m_builtFor;  // category the editors were made for
    int m_schemaGen = -1;
    objects::Object m_obj;
    bool m_loading = false;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    QWidget *m_header = nullptr;
    QLabel *m_category = nullptr;
    QLineEdit *m_name = nullptr;
    QToolButton *m_colorButton = nullptr;
    QWidget *m_fieldBox = nullptr;
    QHash<QString, QWidget *> m_fields;
    ChipList *m_backlinks = nullptr;
    QLabel *m_backlinkTitle = nullptr;
    QLabel *m_empty = nullptr;
    QStackedWidget *m_stack = nullptr;
};

class ObjectsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ObjectsPanel(bool wide, QWidget *parent = nullptr);
    void setProject(Project *p);
    Project *project() const { return m_project; }
    // the binder document shown in the editor ("In this document": its objects)
    void setDocument(BinderItem *item);
    void setCategory(const QString &id);
    QString category() const { return m_category; }
    // shows an object: its category, selected in the list, in the form (history)
    void showObject(const QString &id, bool record = true);
    QString currentObject() const;
    QString newObject(const QString &name = QString());
    QString newFolder(const QString &name = QString());
    void deleteSelected(bool ask = true);
    QString duplicateCurrent();
    void back();
    void forward();
    bool canGoBack() const { return m_pos > 0; }
    bool canGoForward() const { return m_pos + 1 < m_history.size(); }
    void editTemplates();
    ObjectForm *form() const { return m_form; }
    QTreeWidget *tree() const { return m_tree; }
    QLineEdit *filterField() const { return m_filter; }
    ChipList *documentChips() const { return m_docChips; }
    QToolButton *categoryButton(const QString &id) const;
    QToolButton *backButton() const { return m_back; }
    QToolButton *forwardButton() const { return m_forward; }
    // Ctrl + mouse wheel (0.21.1): one zoom for the upper part and the list, one for the sheet
    void setZoomKeys(const QString &listKey, const QString &sheetKey);
    QSplitter *splitter() const { return m_split; }
    // the category, the object shown and the divider (the inspector keeps them with the project)
    QVariantMap saveState() const;
    void restoreState(const QVariantMap &state);
signals:
    void openWindowRequested(const QString &objectId);
    void openDocumentRequested(qb::BinderItem *item);
    void linkActivated(const QString &href);

protected:
    void showEvent(QShowEvent *e) override;

private:
    void rebuildCategories();
    void rebuildTree();
    void updateNav();
    void refreshDocumentChips();
    void onTreeSelection();
    void treeContextMenu(const QPoint &pos);
    void moveByDrop(QTreeWidgetItem *dragged, QTreeWidgetItem *target);
    objects::Store *store() const;
    void connectStore();
    QPointer<Project> m_project;
    QPointer<objects::Store> m_store;
    bool m_wide;
    QString m_category;
    QString m_documentId;
    QStringList m_history;
    int m_pos = -1;
    bool m_updating = false;
    QWidget *m_categoryBar = nullptr;
    FlowLayout *m_categoryFlow = nullptr;
    QList<QToolButton *> m_categoryButtons;
    QToolButton *m_back = nullptr, *m_forward = nullptr, *m_add = nullptr, *m_addFolder = nullptr, *m_menu = nullptr,
                *m_window = nullptr;
    QLineEdit *m_filter = nullptr;
    QWidget *m_docRow = nullptr;
    ChipList *m_docChips = nullptr;
    QTreeWidget *m_tree = nullptr;
    ObjectForm *m_form = nullptr;
    QSplitter *m_split = nullptr;
    bool m_sized = false; // the list and the form got their first sizes
    QList<int> m_pendingSizes; // sizes kept with the project, set when the panel is first shown
};

// The large window of the objects (one per project).
class ObjectsWindow : public QMainWindow
{
    Q_OBJECT
public:
    static ObjectsWindow *open(Project *project, const QString &objectId, QWidget *parent);
    static ObjectsWindow *existing(Project *project);
    static void closeFor(Project *project);
    ObjectsPanel *panel() const { return m_panel; }

private:
    ObjectsWindow(Project *project, QWidget *parent);
    ObjectsPanel *m_panel;
    QPointer<Project> m_project;
};

// Project > Object Templates and Colors: categories (name, color, order) and the
// fields of each template.
class ObjectTemplatesDialog : public QDialog
{
    Q_OBJECT
public:
    ObjectTemplatesDialog(Project *project, const QString &category, QWidget *parent = nullptr);
    objects::Schema schema() const { return m_schema; }
    QStringList removedCategories() const { return m_removed; }
    // parts for tests
    QListWidget *categoryList() const { return m_cats; }
    QListWidget *fieldList() const { return m_fieldList; }
    QLineEdit *categoryName() const { return m_catName; }
    QLineEdit *fieldLabel() const { return m_label; }
    QComboBox *fieldType() const { return m_type; }
    QLineEdit *fieldUnit() const { return m_unit; }
    QPlainTextEdit *fieldChoices() const { return m_choices; }
    QListWidget *fieldTargets() const { return m_targets; }
    void setCategoryColor(const QColor &c);
    void addCategory(const QString &name);
    void addField(const QString &label, objects::FieldType type);
    void removeCurrentField();
    void moveField(int delta);
    void removeCurrentCategory();
    // applies the result to the project (asks before objects of removed categories go)
    static bool run(Project *project, const QString &category, QWidget *parent);

private:
    void loadCategory();
    void loadField();
    void storeField();
    void storeCategory();
    objects::Category *current();
    QPointer<Project> m_project;
    objects::Schema m_schema;
    QStringList m_removed;
    bool m_loading = false;
    QListWidget *m_cats = nullptr;
    QLineEdit *m_catName = nullptr;
    QToolButton *m_catColor = nullptr;
    QListWidget *m_fieldList = nullptr;
    QWidget *m_fieldBox = nullptr;
    QLineEdit *m_label = nullptr;
    QComboBox *m_type = nullptr;
    QLineEdit *m_unit = nullptr;
    QPlainTextEdit *m_choices = nullptr;
    QListWidget *m_targets = nullptr;
    QLabel *m_unitLabel = nullptr, *m_choicesLabel = nullptr, *m_targetsLabel = nullptr;
};

} // namespace qb
