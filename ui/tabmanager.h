#ifndef TABMANAGER_H
#define TABMANAGER_H

#include "model/drawing.h"
#include "ui/nodetablemodel.h"

#include <QList>
#include <QObject>
#include <QStack>

class QTabWidget;
class PdfTabViewer;
class RemoteClient;

// 标签页生命周期管理：负责 Tab 页的创建、销毁、切换与 TabData 持有
// 目录/PDF/远程 三种页面类型统一管理
class TabManager : public QObject
{
    Q_OBJECT

public:
    enum class TabType {
        Directory,   // 目录浏览页
        Pdf,         // PDF 查看页
        Remote       // 远程访问页（目录浏览，数据走协议）
    };

    struct TabData {
        TabType type = TabType::Directory;
        QString title;
        // 所属项目（多项目标签共存，切换标签时据此切换数据库上下文）
        QString projectPath;
        QString projectName;
        // 远程会话标签：非空表示该标签数据来自远程连接（目录/PDF 页均可能）
        RemoteClient *remoteClient = nullptr;
        // 目录模式
        qint64 currentNodeId = 0;
        QStack<qint64> backStack;
        QStack<qint64> forwardStack;
        QVector<DirectoryItem> items;
        NodeTableModel *model = nullptr;
        // PDF 模式
        QString pdfFilePath;
        PdfTabViewer *pdfViewer = nullptr;
    };

    explicit TabManager(QTabWidget *tabWidget, QObject *parent = nullptr);
    ~TabManager() override;

    // 打开目录标签页
    void openDirectoryTab(const QString &title, qint64 rootNodeId,
                          const QString &projectPath, const QString &projectName);
    // 打开 PDF 页；ok 输出是否成功（加载失败时不会创建页）
    void openPdfTab(const Drawing &drawing, const QString &fullPath,
                    const QString &projectPath, const QString &projectName, bool *ok = nullptr,
                    RemoteClient *remoteClient = nullptr);
    // 打开远程目录标签页（数据由 RemoteClient 协议提供）；rootNodeId 为起始浏览节点
    void openRemoteTab(const QString &title, const QString &projectName,
                       RemoteClient *client, qint64 rootNodeId = 0);
    void closeTab(int index);
    void closeAll();
    bool isEmpty() const;
    bool hasTabOfType(TabType type) const;
    int indexOfType(TabType type) const;
    // 项目相关
    bool hasTabOfProject(const QString &projectPath) const;
    QList<int> projectTabIndices(const QString &projectPath) const;

    TabData *currentTab() const;
    TabData *tabAt(int index) const;
    int currentIndex() const;
    int count() const;
    TabType currentTabType() const;
    void setCurrentIndex(int index);
    void setTabTitle(int index, const QString &title);

signals:
    void currentChanged(int index);

private:
    int insertTabPage(TabData *data, QWidget *page, const QString &title);

    QTabWidget *m_tabWidget;
    QList<TabData *> m_tabs;
};

#endif // TABMANAGER_H
