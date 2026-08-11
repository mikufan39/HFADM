#include "ui/remarkdelegate.h"

#include <QApplication>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

RemarkDelegate::RemarkDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void RemarkDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                           const QModelIndex &index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // 按当前列宽省略号截断文本；其余绘制（背景/选中态/焦点）交给标准样式
    if (!opt.text.isEmpty()) {
        opt.text = opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, opt.rect.width());
    }

    const QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
}
