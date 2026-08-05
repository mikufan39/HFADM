#include "contextmenus.h"

#include <QAction>
#include <QMenu>

ContextMenus buildContextMenus(QWidget *parent)
{
    ContextMenus m;

    m.newMenu = new QMenu(parent);
    m.newComponent = m.newMenu->addAction(QStringLiteral("部件"));
    m.newPart = m.newMenu->addAction(QStringLiteral("零件"));

    m.nodeMenu = new QMenu(parent);
    QMenu *newSubMenu = m.nodeMenu->addMenu(QStringLiteral("新建"));
    newSubMenu->addAction(m.newComponent);
    newSubMenu->addAction(m.newPart);
    m.openInNewTab = m.nodeMenu->addAction(QStringLiteral("在新标签页打开"));
    m.nodeMenu->addSeparator();
    m.cut = m.nodeMenu->addAction(QStringLiteral("剪切"));
    m.copy = m.nodeMenu->addAction(QStringLiteral("复制"));
    m.paste = m.nodeMenu->addAction(QStringLiteral("粘贴"));
    m.nodeMenu->addSeparator();
    m.rename = m.nodeMenu->addAction(QStringLiteral("重命名"));
    m.remove = m.nodeMenu->addAction(QStringLiteral("删除"));
    m.properties = m.nodeMenu->addAction(QStringLiteral("属性"));

    m.drawingMenu = new QMenu(parent);
    m.importPdf = m.drawingMenu->addAction(QStringLiteral("导入 PDF 图纸"));
    m.drawingMenu->addSeparator();
    m.viewDrawing = m.drawingMenu->addAction(QStringLiteral("查看"));
    m.setCurrent = m.drawingMenu->addAction(QStringLiteral("设为当前版本"));
    m.drawingMenu->addSeparator();
    m.deleteDrawing = m.drawingMenu->addAction(QStringLiteral("删除"));

    return m;
}
