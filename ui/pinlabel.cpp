#include "pinlabel.h"

#include <QFontMetrics>
#include <QPainter>

PinLabel::PinLabel(const QString &text, QWidget *parent, Qt::WindowFlags f)
    : QLabel(text, parent, f)
{
}

void PinLabel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    QFont f = font();
    f.setPointSize(40);
    f.setBold(true);
    p.setFont(f);
    const QFontMetrics fm(f);
    const QString t = text();
    const int textWidth = fm.horizontalAdvance(t);
    const int lineHeight = fm.ascent() + fm.descent();
    const QRect r = rect();
    const int x = r.left() + (r.width() - textWidth) / 2;
    const int baselineY = r.top() + (r.height() - lineHeight) / 2 + fm.ascent();

    // 口令本体：#39c5bb
    p.setPen(QColor(QStringLiteral("#39c5bb")));
    p.drawText(x, baselineY, t);

    // 数字下方红色下划线
    const int underlineY = baselineY + fm.underlinePos() + 2;
    p.setPen(QPen(QColor(QStringLiteral("#e74c3c")), 3));
    p.drawLine(x, underlineY, x + textWidth, underlineY);
}
