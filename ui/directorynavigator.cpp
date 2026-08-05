#include "directorynavigator.h"
#include "tabmanager.h"
#include "service/nodeservice.h"
#include "model/hfdadnode.h"

DirectoryNavigator::DirectoryNavigator(NodeService *nodeService, TabManager *tabManager,
                                       QObject *parent)
    : QObject(parent)
    , m_nodeService(nodeService)
    , m_tabManager(tabManager)
{
}

void DirectoryNavigator::navigateTo(TabManager::TabData *tab, qint64 nodeId)
{
    if (!tab) {
        return;
    }
    if (tab->currentNodeId != 0) {
        tab->backStack.push(tab->currentNodeId);
    }
    tab->forwardStack.clear();
    tab->currentNodeId = nodeId;
    updateTabTitle(tab, nodeId);
}

void DirectoryNavigator::navigateBack(TabManager::TabData *tab)
{
    if (!tab || tab->backStack.isEmpty()) {
        return;
    }
    tab->forwardStack.push(tab->currentNodeId);
    tab->currentNodeId = tab->backStack.pop();
    updateTabTitle(tab, tab->currentNodeId);
}

void DirectoryNavigator::navigateForward(TabManager::TabData *tab)
{
    if (!tab || tab->forwardStack.isEmpty()) {
        return;
    }
    tab->backStack.push(tab->currentNodeId);
    tab->currentNodeId = tab->forwardStack.pop();
    updateTabTitle(tab, tab->currentNodeId);
}

bool DirectoryNavigator::navigateUp(TabManager::TabData *tab)
{
    bool ok = false;
    const qint64 parent = parentIdOf(tab ? tab->currentNodeId : 0, &ok);
    if (!ok || parent == 0) {
        return false;
    }
    navigateTo(tab, parent);
    return true;
}

bool DirectoryNavigator::canGoUp(TabManager::TabData *tab) const
{
    bool ok = false;
    const qint64 parent = parentIdOf(tab ? tab->currentNodeId : 0, &ok);
    return ok && parent != 0;
}

qint64 DirectoryNavigator::parentIdOf(qint64 nodeId, bool *ok) const
{
    HFADMNode node;
    if (!m_nodeService || !m_nodeService->getNode(nodeId, node)) {
        if (ok) {
            *ok = false;
        }
        return 0;
    }
    if (ok) {
        *ok = true;
    }
    return node.parentId;
}

void DirectoryNavigator::updateTabTitle(TabManager::TabData *tab, qint64 nodeId)
{
    HFADMNode node;
    if (m_nodeService && m_nodeService->getNode(nodeId, node)) {
        tab->title = node.name;
        m_tabManager->setTabTitle(m_tabManager->currentIndex(), node.name);
    }
}
