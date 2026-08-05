#ifndef DIRECTORYNAVIGATOR_H
#define DIRECTORYNAVIGATOR_H

#include "ui/tabmanager.h"

#include <QObject>

class NodeService;

// 目录浏览导航：后退/前进/上一级/跳转，并维护各 Tab 的导航历史栈与标题
class DirectoryNavigator : public QObject
{
    Q_OBJECT

public:
    explicit DirectoryNavigator(NodeService *nodeService, TabManager *tabManager,
                                QObject *parent = nullptr);

    // 进入 nodeId 目录（记录后退历史、清空前栈、更新 Tab 标题）
    void navigateTo(TabManager::TabData *tab, qint64 nodeId);
    void navigateBack(TabManager::TabData *tab);
    void navigateForward(TabManager::TabData *tab);
    // 上一级；无父级时返回 false
    bool navigateUp(TabManager::TabData *tab);
    bool canGoUp(TabManager::TabData *tab) const;
    qint64 parentIdOf(qint64 nodeId, bool *ok) const;

private:
    void updateTabTitle(TabManager::TabData *tab, qint64 nodeId);

    NodeService *m_nodeService;
    TabManager *m_tabManager;
};

#endif // DIRECTORYNAVIGATOR_H
