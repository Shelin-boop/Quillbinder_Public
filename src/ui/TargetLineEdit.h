#pragma once
#include "ui/Theme.h"

#include <QLineEdit>
#include <QPainter>

namespace qb {

// Quick search field of the toolbar with the draft and session target bars.
class TargetLineEdit : public QLineEdit
{
public:
    using QLineEdit::QLineEdit;
    double draft = -1, session = -1;

protected:
    void paintEvent(QPaintEvent *e) override
    {
        QLineEdit::paintEvent(e);
        QPainter p(this);
        const int w = width() - 8;
        int y = height() - 4;
        auto bar = [&](double v, const QColor &c) {
            if (v < 0)
                return;
            p.fillRect(QRect(4, y, w, 2), QColor(0, 0, 0, 30));
            p.fillRect(QRect(4, y, int(w * qMin(1.0, v)), 2), v >= 1.0 ? theme::color(theme::Role::TargetReached) : c);
            y -= 3;
        };
        bar(session, theme::color(theme::Role::TargetSession));
        bar(draft, theme::color(theme::Role::TargetDraft));
    }
};

} // namespace qb
