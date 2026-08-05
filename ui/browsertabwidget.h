#ifndef BROWSERTABWIDGET_H
#define BROWSERTABWIDGET_H

#include <QTabWidget>

class BrowserTabBar;

// 浏览器式标签容器：构造时以 BrowserTabBar 替换默认 QTabBar。
// QTabWidget::setTabBar 为 protected，只能在子类中调用。
class BrowserTabWidget : public QTabWidget
{
    Q_OBJECT

public:
    explicit BrowserTabWidget(QWidget *parent = nullptr);

    BrowserTabBar *browserTabBar() const;
};

#endif // BROWSERTABWIDGET_H
