#pragma once
// The windows of tables and pictures (0.62): Insert > Table, Format > Table > Table Properties,
// and the Picture window of a picture in the text (size, position, caption, description).
#include "text/Tables.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;

namespace qb {

class TableDialog : public QDialog
{
    Q_OBJECT
public:
    // newTable: Insert > Table (rows and columns asked); else the properties of a table
    TableDialog(const tables::Spec &spec, int columns, bool newTable, QWidget *parent = nullptr);
    tables::Spec spec() const;
    int rows() const;
    int columns() const;

private:
    void fill(const tables::Spec &s);
    void own(); // a value changed by hand: the style is the writer's own now
    tables::Spec m_base;
    bool m_filling = false;
    QSpinBox *m_rows = nullptr, *m_cols = nullptr;
    QComboBox *m_style, *m_align, *m_flow;
    QSpinBox *m_width, *m_header;
    QDoubleSpinBox *m_border, *m_padding, *m_spacing;
    QCheckBox *m_linesOnly;
    QPushButton *m_borderColor, *m_headerColor, *m_stripeColor;
    QColor m_border_c, m_header_c, m_stripe_c;
    QLineEdit *m_columns;
};

class PictureDialog : public QDialog
{
    Q_OBJECT
public:
    PictureDialog(const QSize &natural, int width, figures::Position pos, const QString &caption, const QString &description,
                  QWidget *parent = nullptr);
    int pictureWidth() const;
    figures::Position position() const;
    QString caption() const;
    QString description() const;

private:
    QSpinBox *m_width;
    QComboBox *m_pos;
    QLineEdit *m_caption;
    QPlainTextEdit *m_desc;
};

} // namespace qb
