#ifndef REVERSEWHEELSPINBOX_H
#define REVERSEWHEELSPINBOX_H

#include <QSpinBox>
#include <QWheelEvent>

// 数量微调框：滚轮方向与系统默认相反——向下滚动=增加、向上滚动=减少（步长 1）。
// 新建零件/新建部件/零件编辑等数量输入共用；最小数量由 setRange 下限保证。
class ReverseWheelSpinBox : public QSpinBox
{
public:
    using QSpinBox::QSpinBox;

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        const int delta = event->angleDelta().y();
        if (delta > 0) {
            setValue(value() - singleStep());
        } else if (delta < 0) {
            setValue(value() + singleStep());
        }
        event->accept();
    }
};

#endif // REVERSEWHEELSPINBOX_H
