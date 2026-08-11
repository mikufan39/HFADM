#ifndef REMARKDELEGATE_H
#define REMARKDELEGATE_H

#include <QStyledItemDelegate>

// 备注列专用 delegate：内容超长时按列宽省略号截断（ElideRight），
// 完整内容由模型 ToolTipRole 提供（悬停查看）
class RemarkDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit RemarkDelegate(QObject *parent = nullptr);

protected:
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};

#endif // REMARKDELEGATE_H
