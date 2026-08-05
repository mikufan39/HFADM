#include "browsertabbar.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

BrowserTabBar::BrowserTabBar(QWidget *parent)
    : QTabBar(parent)
{
    setMovable(true);
    setUsesScrollButtons(true);
    setElideMode(Qt::ElideRight);
}

void BrowserTabBar::setProjectColor(int index, const QColor &color)
{
    if (index < 0 || index >= count()) {
        return;
    }
    m_projectColors.insert(index, color);
    update();
}

QColor BrowserTabBar::projectColor(int index) const
{
    return m_projectColors.value(index);
}

void BrowserTabBar::clearProjectColors()
{
    m_projectColors.clear();
    update();
}

void BrowserTabBar::paintEvent(QPaintEvent *event)
{
    QTabBar::paintEvent(event);

    // 在每个标签顶部绘制项目色带，避开圆角与右侧关闭按钮
    QPainter painter(this);
    for (auto it = m_projectColors.cbegin(); it != m_projectColors.cend(); ++it) {
        const int index = it.key();
        if (index < 0 || index >= count()) {
            continue;
        }
        const QRect rect = tabRect(index);
        if (rect.isEmpty()) {
            continue;
        }
        painter.fillRect(QRect(rect.left() + 4, rect.top() + 2,
                               rect.width() - 8, 3), it.value());
    }
}

void BrowserTabBar::wheelEvent(QWheelEvent *event)
{
    if (count() < 2) {
        QTabBar::wheelEvent(event);
        return;
    }

    const int delta = event->angleDelta().y();
    int next = currentIndex();
    if (delta > 0) {
        next = qMax(0, next - 1);
    } else if (delta < 0) {
        next = qMin(count() - 1, next + 1);
    }
    if (next != currentIndex()) {
        setCurrentIndex(next);
        event->accept();
        return;
    }
    QTabBar::wheelEvent(event);
}

void BrowserTabBar::mouseReleaseEvent(QMouseEvent *event)
{
    // 中键点击标签 = 关闭（Firefox 习惯）
    if (event->button() == Qt::MiddleButton) {
        const int index = tabAt(event->pos());
        if (index >= 0) {
            emit tabCloseRequested(index);
            event->accept();
            return;
        }
    }
    QTabBar::mouseReleaseEvent(event);
}

void BrowserTabBar::tabInserted(int index)
{
    QTabBar::tabInserted(index);

    // 新标签插入后，原 index 及之后的颜色同步后移一位
    QHash<int, QColor> shifted;
    for (auto it = m_projectColors.cbegin(); it != m_projectColors.cend(); ++it) {
        shifted.insert(it.key() >= index ? it.key() + 1 : it.key(), it.value());
    }
    m_projectColors = shifted;
}

void BrowserTabBar::tabRemoved(int index)
{
    QTabBar::tabRemoved(index);

    // 标签移除后，index 之后的颜色同步前移一位
    QHash<int, QColor> shifted;
    for (auto it = m_projectColors.cbegin(); it != m_projectColors.cend(); ++it) {
        if (it.key() == index) {
            continue;
        }
        shifted.insert(it.key() > index ? it.key() - 1 : it.key(), it.value());
    }
    m_projectColors = shifted;
}
