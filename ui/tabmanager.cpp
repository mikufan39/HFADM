#include "tabmanager.h"
#include "pdftabviewer.h"

#include <QTabWidget>

TabManager::TabManager(QTabWidget *tabWidget, QObject *parent)
    : QObject(parent)
    , m_tabWidget(tabWidget)
{
    connect(m_tabWidget, &QTabWidget::currentChanged,
            this, &TabManager::currentChanged);
}

TabManager::~TabManager()
{
    closeAll();
}

void TabManager::openDirectoryTab(const QString &title, qint64 rootNodeId,
                                  const QString &projectPath, const QString &projectName)
{
    auto *tabData = new TabData;
    tabData->type = TabType::Directory;
    tabData->currentNodeId = rootNodeId;
    tabData->rootNodeId = rootNodeId;
    tabData->title = title;
    tabData->projectPath = projectPath;
    tabData->projectName = projectName;
    tabData->model = new NodeTableModel(this);

    insertTabPage(tabData, new QWidget(m_tabWidget), title);
}

void TabManager::openPdfTab(const Drawing &drawing, const QString &fullPath,
                            const QString &projectPath, const QString &projectName, bool *ok,
                            RemoteClient *remoteClient)
{
    auto *viewer = new PdfTabViewer(fullPath);
    if (!viewer->isLoaded()) {
        if (ok) {
            *ok = false;
        }
        delete viewer;
        return;
    }

    auto *tabData = new TabData;
    tabData->type = TabType::Pdf;
    tabData->title = projectName + QStringLiteral(": ") + drawing.fileName;
    tabData->projectPath = projectPath;
    tabData->projectName = projectName;
    tabData->pdfFilePath = fullPath;
    tabData->pdfViewer = viewer;
    tabData->remoteClient = remoteClient;

    insertTabPage(tabData, viewer, tabData->title);
    if (ok) {
        *ok = true;
    }
}

void TabManager::openRemoteTab(const QString &title, const QString &projectName,
                               RemoteClient *client, qint64 rootNodeId)
{
    auto *tabData = new TabData;
    tabData->type = TabType::Remote;
    tabData->title = title;
    tabData->projectName = projectName;
    tabData->currentNodeId = rootNodeId;
    tabData->rootNodeId = rootNodeId;
    tabData->remoteClient = client;
    tabData->currentNodeType = NodeType::Aircraft; // 远程标签初始在服务端机型根
    tabData->model = new NodeTableModel(this);

    insertTabPage(tabData, new QWidget(m_tabWidget), title);
}

void TabManager::closeTab(int index)
{
    if (index < 0 || index >= m_tabs.size()) {
        return;
    }
    TabData *tab = m_tabs.takeAt(index);
    if (tab->pdfViewer) {
        delete tab->pdfViewer; // QTabWidget::removeTab 不会自动删除页面
    }
    delete tab;
    m_tabWidget->removeTab(index);
}

void TabManager::closeAll()
{
    while (!m_tabs.isEmpty()) {
        closeTab(0);
    }
    while (m_tabWidget->count() > 0) {
        m_tabWidget->removeTab(0);
    }
}

bool TabManager::isEmpty() const
{
    return m_tabs.isEmpty();
}

bool TabManager::hasTabOfType(TabType type) const
{
    return indexOfType(type) >= 0;
}

bool TabManager::hasTabOfProject(const QString &projectPath) const
{
    for (const TabData *tab : m_tabs) {
        if (tab->projectPath == projectPath) {
            return true;
        }
    }
    return false;
}

QList<int> TabManager::projectTabIndices(const QString &projectPath) const
{
    QList<int> indices;
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs.at(i)->projectPath == projectPath) {
            indices.append(i);
        }
    }
    return indices;
}

int TabManager::indexOfType(TabType type) const
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs.at(i)->type == type) {
            return i;
        }
    }
    return -1;
}

TabManager::TabData *TabManager::currentTab() const
{
    const int index = currentIndex();
    if (index < 0 || index >= m_tabs.size()) {
        return nullptr;
    }
    return m_tabs.at(index);
}

TabManager::TabData *TabManager::tabAt(int index) const
{
    if (index < 0 || index >= m_tabs.size()) {
        return nullptr;
    }
    return m_tabs.at(index);
}

int TabManager::currentIndex() const
{
    return m_tabWidget ? m_tabWidget->currentIndex() : -1;
}

int TabManager::count() const
{
    return m_tabs.size();
}

TabManager::TabType TabManager::currentTabType() const
{
    const TabData *tab = currentTab();
    return tab ? tab->type : TabType::Directory;
}

void TabManager::setCurrentIndex(int index)
{
    if (m_tabWidget) {
        m_tabWidget->setCurrentIndex(index);
    }
}

void TabManager::setTabTitle(int index, const QString &title)
{
    if (m_tabWidget && index >= 0 && index < m_tabs.size()) {
        m_tabs[index]->title = title;
        m_tabWidget->setTabText(index, title);
    }
}

int TabManager::insertTabPage(TabData *data, QWidget *page, const QString &title)
{
    const int index = m_tabWidget->addTab(page, title);
    m_tabs.insert(index, data);
    m_tabWidget->setCurrentIndex(index);
    return index;
}
