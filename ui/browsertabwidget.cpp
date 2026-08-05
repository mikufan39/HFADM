#include "browsertabwidget.h"
#include "browsertabbar.h"

BrowserTabWidget::BrowserTabWidget(QWidget *parent)
    : QTabWidget(parent)
{
    setTabBar(new BrowserTabBar(this));
}

BrowserTabBar *BrowserTabWidget::browserTabBar() const
{
    return static_cast<BrowserTabBar *>(tabBar());
}
