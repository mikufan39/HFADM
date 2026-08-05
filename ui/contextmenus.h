#ifndef CONTEXTMENUS_H
#define CONTEXTMENUS_H

class QMenu;
class QAction;
class QWidget;

// 主界面右键菜单集合：节点菜单 / 图纸菜单 / 新建菜单
// 菜单与动作的生命周期由 parent 管理
struct ContextMenus {
    QMenu *newMenu = nullptr;      // 工具栏"新建"弹出菜单（部件/零件）
    QMenu *nodeMenu = nullptr;     // 节点右键菜单
    QMenu *drawingMenu = nullptr;  // 图纸右键菜单

    QAction *newComponent = nullptr;
    QAction *newPart = nullptr;
    QAction *openInNewTab = nullptr;   // 节点菜单：在新标签页打开（仅部件/机型）
    QAction *importPdf = nullptr;      // 图纸菜单：导入 PDF 图纸
    QAction *cut = nullptr;
    QAction *copy = nullptr;
    QAction *paste = nullptr;
    QAction *rename = nullptr;
    QAction *remove = nullptr;
    QAction *properties = nullptr;
    QAction *viewDrawing = nullptr;
    QAction *setCurrent = nullptr;
    QAction *deleteDrawing = nullptr;
};

// 构建全部右键菜单
ContextMenus buildContextMenus(QWidget *parent);

#endif // CONTEXTMENUS_H
