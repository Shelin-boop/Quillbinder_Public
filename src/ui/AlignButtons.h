#pragma once
// Four buttons for the alignment of body text (left, centered, right, justified), as in
// the format bar; used where the main text style is set (File > Options > Editing >
// Formatting, Project > Project Settings > Formatting). 0.40.3, before: "Justify text".

#include <QWidget>

class QButtonGroup;

namespace qb {

class AlignButtons : public QWidget
{
    Q_OBJECT
public:
    explicit AlignButtons(QWidget *parent = nullptr);
    Qt::Alignment alignment() const;            // AlignLeft, AlignHCenter, AlignRight or AlignJustify
    void setAlignment(Qt::Alignment a);

signals:
    void alignmentChanged(Qt::Alignment a);

private:
    QButtonGroup *m_group;
};

} // namespace qb
