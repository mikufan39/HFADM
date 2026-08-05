#ifndef BROWSERTABBAR_H
#define BROWSERTABBAR_H

#include <QColor>
#include <QHash>
#include <QTabBar>

// Firefox 风格标签栏：
//  - 顶部项目色带（多项目标签共存时区分归属）
//  - 滚轮切换标签
//  - 中键点击关闭标签
//  - 拖拽排序
class BrowserTabBar : public QTabBar
{
    Q_OBJECT

public:
    explicit BrowserTabBar(QWidget *parent = nullptr);

    void setProjectColor(int index, const QColor &color);
    QColor projectColor(int index) const;
    void clearProjectColors();

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void tabInserted(int index) override;
    void tabRemoved(int index) override;

private:
    QHash<int, QColor> m_projectColors;
};

#endif // BROWSERTABBAR_H
