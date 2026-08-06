#ifndef PINLABEL_H
#define PINLABEL_H

#include <QLabel>

// 大号配对口令展示标签：40pt 粗体 #39c5bb 文本，数字下方红色下划线。
// 自绘实现（QPainter）以保证下划线独立着色且内容垂直居中。
// 用于服务端配对确认弹窗与客户端口令展示弹窗。
class PinLabel : public QLabel
{
    Q_OBJECT

public:
    explicit PinLabel(const QString &text, QWidget *parent = nullptr,
                      Qt::WindowFlags f = Qt::WindowFlags());

protected:
    void paintEvent(QPaintEvent *event) override;
};

#endif // PINLABEL_H
