#include <QCoreApplication>
#include "contextmenus.h"

#include <QAction>
#include <QIcon>
#include <QMenu>

// 菜单图标统一取 :/assets/Icons/ 下的 lucide SVG（currentColor 描边，与工具栏一致）
namespace {
QIcon menuIcon(const char *name)
{
    return QIcon(QStringLiteral(":/assets/Icons/%1.svg").arg(QLatin1String(name)));
}
} // namespace

ContextMenus buildContextMenus(QWidget *parent)
{
    ContextMenus m;

    m.newMenu = new QMenu(parent);
    m.newComponent = m.newMenu->addAction(QCoreApplication::translate("ContextMenus", "部件"));
    m.newComponent->setIcon(menuIcon("box"));
    m.newPart = m.newMenu->addAction(QCoreApplication::translate("ContextMenus", "零件"));
    m.newPart->setIcon(menuIcon("cog"));

    m.nodeMenu = new QMenu(parent);
    QMenu *newSubMenu = m.nodeMenu->addMenu(QCoreApplication::translate("ContextMenus", "新建"));
    newSubMenu->setIcon(menuIcon("plus"));
    newSubMenu->addAction(m.newComponent);
    newSubMenu->addAction(m.newPart);
    m.openInNewTab = m.nodeMenu->addAction(QCoreApplication::translate("ContextMenus", "在新标签页打开"));
    m.openInNewTab->setIcon(menuIcon("external-link"));
    m.nodeMenu->addSeparator();
    m.cut = m.nodeMenu->addAction(QCoreApplication::translate("ContextMenus", "剪切"));
    m.cut->setIcon(menuIcon("scissors"));
    m.copy = m.nodeMenu->addAction(QCoreApplication::translate("ContextMenus", "复制"));
    m.copy->setIcon(menuIcon("copy"));
    m.paste = m.nodeMenu->addAction(QCoreApplication::translate("ContextMenus", "粘贴"));
    m.paste->setIcon(menuIcon("clipboard"));
    m.nodeMenu->addSeparator();
    m.rename = m.nodeMenu->addAction(QCoreApplication::translate("ContextMenus", "重命名"));
    m.rename->setIcon(menuIcon("pencil"));
    m.remove = m.nodeMenu->addAction(QCoreApplication::translate("ContextMenus", "删除"));
    m.remove->setIcon(menuIcon("trash-2"));
    m.properties = m.nodeMenu->addAction(QCoreApplication::translate("ContextMenus", "属性"));
    m.properties->setIcon(menuIcon("settings"));

    m.drawingMenu = new QMenu(parent);
    m.importPdf = m.drawingMenu->addAction(QCoreApplication::translate("ContextMenus", "导入 PDF 图纸"));
    m.importPdf->setIcon(menuIcon("upload"));
    m.drawingMenu->addSeparator();
    m.viewDrawing = m.drawingMenu->addAction(QCoreApplication::translate("ContextMenus", "查看"));
    m.viewDrawing->setIcon(menuIcon("eye"));
    m.setCurrent = m.drawingMenu->addAction(QCoreApplication::translate("ContextMenus", "设为当前版本"));
    m.setCurrent->setIcon(menuIcon("check-circle"));
    m.drawingMenu->addSeparator();
    m.deleteDrawing = m.drawingMenu->addAction(QCoreApplication::translate("ContextMenus", "删除"));
    m.deleteDrawing->setIcon(menuIcon("trash-2"));

    return m;
}
