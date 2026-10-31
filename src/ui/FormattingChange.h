#pragma once
// Changes of the project formatting and of styles that reach every document
// of the project: Project Settings ▸ Formatting, and the offer of the format
// bar after a font or size was picked ("Change the Style", "Use for All Text").

#include "text/DirectFormat.h"

#include <QFrame>
#include <QString>

class QLabel;
class QPushButton;

namespace qb {

class Project;

namespace formatchange {

// p->formatting changed from oldF: every text document of the project (open
// or not; script documents keep their own format) follows the new values, see
// direct::followProject. A document that changes gets a snapshot first.
// Returns the number of changed documents, -1 if some could not be written.
int followProject(Project *p, const Formatting &oldF, bool ownToo, QString *error = nullptr);
// The style got a new font or size: text of that style with the old or the new
// value set by hand shows the style (direct::followStyle) in every document.
int followStyle(Project *p, const QString &style, bool character, int charMembers, const direct::CharSpec &oldValues,
                const direct::CharSpec &newValues, QString *error = nullptr);
// A style was changed (0.40.2): all text of the style in every document takes over the
// changed attributes, also where it had its own values (direct::takeStyle). A document
// that changes gets a snapshot first. Returns the number of changed documents, -1 if some
// could not be written.
int takeStyle(Project *p, const QString &style, bool character, int charMembers, int paraBits, QString *error = nullptr);
// The font family and size a style gives its text (the project's where the
// style does not set them).
direct::CharSpec styleFont(const Formatting &f, const QString &style);

} // namespace formatchange

// The bar above the editors with the offer after a font or size was picked in
// the format bar: a sentence, one button and a close button.
class FontScopeBar : public QFrame
{
    Q_OBJECT
public:
    explicit FontScopeBar(QWidget *parent = nullptr);
    void offer(const QString &text, const QString &button);
    QString text() const;
    QPushButton *button() const { return m_button; }

signals:
    void accepted();

protected:
    void changeEvent(QEvent *e) override;

private:
    void updateColors();
    QLabel *m_text;
    QPushButton *m_button;
    bool m_coloring = false;
};

} // namespace qb
