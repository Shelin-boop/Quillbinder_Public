#pragma once
#include <QDialog>
#include <QIcon>
#include <QList>
#include <QString>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QListWidget;

namespace qb {

// What the main toolbar and the format bar hold. A bar is a list of item
// ids: keys of commands (see Commands.h), ids of the special controls below,
// "|" for a separator and "<>" for a space that takes up what is left over.
namespace toolbars {

enum class Bar { Main = 0, Format = 1 };

extern const QString Separator;   // "|"
extern const QString Spacer;      // "<>"

struct Special {
    QString id;
    QString label;
    QString icon;   // icon name
};
// Controls that are more than one command: the View button, the search
// field, the style list ...
QList<Special> specials(Bar bar);
bool isSpecial(const QString &id);

QStringList standard(Bar bar);
// The list in use (settings, or the standard set).
QStringList configured(Bar bar);
QString label(const QString &id);   // for lists; empty for unknown ids
QIcon icon(const QString &id);

} // namespace toolbars

// View > Customize Toolbars
class ToolbarDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ToolbarDialog(QWidget *parent = nullptr, toolbars::Bar bar = toolbars::Bar::Main);
    void accept() override;
    void apply();

    // what the buttons do (also used by the tests)
    void setBar(toolbars::Bar bar);
    QStringList items() const;
    bool addItem(const QString &id, int row = -1);   // false if it is in the bar already
    bool removeItem(const QString &id);
    bool moveItem(int row, int delta);
    void resetToStandard();

private:
    void storeCurrent();
    void fillCurrent(const QStringList &ids);
    void fillAvailable();

    toolbars::Bar m_bar = toolbars::Bar::Main;
    QStringList m_lists[2];
    QComboBox *m_barCombo, *m_iconSize, *m_style;
    QLineEdit *m_filter;
    QListWidget *m_available, *m_current;
};

} // namespace qb
