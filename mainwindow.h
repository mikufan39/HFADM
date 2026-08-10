#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "model/hfdadnode.h"
#include "ui/nodetablemodel.h"
#include "ui/tabmanager.h"
#include "ui/contextmenus.h"
#include "ui/clipboard.h"
#include "ui/sessionmanager.h"

#include <QColor>
#include <QHash>
#include <QMainWindow>
#include <QString>
#include <QTimer>

class ProjectService;
class NodeService;
class DrawingService;
class RecentProjectsMenu;
class TabManager;
class DirectoryNavigator;
class WelcomePage;
class RemoteServer;
class RemoteClient;
class QMenu;
class QLabel;
class QTemporaryDir;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    // 文件菜单
    void onNewProject();
    void onOpenProject();
    void onBackupProject();
    void onCloseProject();
    void onExit();
    void openRecentProjectByPath(const QString &projectPath);
    // 网络
    void onOpenRemoteAccess();
    void onCloseRemoteAccess();
    void onConnectRemote();
    void onManageDevices();
    // 编辑 / 右键（节点）
    void onRenameAction();
    void onDeleteAction();
    void onPropertiesAction();
    void onCutAction();
    void onCopyAction();
    void onPasteAction();
    // 图纸
    void onImportPdfAction();
    void onOpenInNewTabAction();
    void onViewDrawingAction();
    void onSetCurrentDrawingAction();
    void onDeleteDrawingAction();
    // 工具栏
    void onBackClicked();
    void onForwardClicked();
    void onUpClicked();
    void onRefreshClicked();
    void onHomeClicked();
    void onNewClicked();
    // 新建子项
    void onNewComponent();
    void onNewPart();
    // 浏览交互
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void onTableDoubleClicked(const QModelIndex &index);
    void onSearchTextChanged(const QString &text);
    void onTableContextMenuRequested(const QPoint &pos);
    void onSelectionChanged();
    // 图号列点击：零件行打开最新版图纸（系统默认 PDF 程序，临时缓存退出清理）
    void onPartNoClicked(const QModelIndex &index);
    // 其他
    void onAbout();

private:
    // 标签页数据（由 TabManager 管理）
    using TabData = TabManager::TabData;

    void initServices();
    void createMenus();
    void setupShortcuts();
    void setupUiConnections();
    void setupToolbarIcons();
    void connectContextMenuActions();
    void updateNavigationState();
    void updateActionState();
    // 会话保存/恢复
    void saveSession();
    void restoreSession();
    void restoreOneTab(const SessionManager::SessionTab &tab);
    // 详情列表列宽记忆（hfadm.session，与标签会话同文件）
    void saveColumnWidths();
    void restoreColumnWidths();

    bool openProjectPath(const QString &projectPath, bool isNewProject);
    void openPdfTab(const Drawing &drawing);
    bool isCurrentTabPdf() const;
    // 远程：当前标签是否为远程会话（Remote 目录标签 / 远程 PDF 标签）
    bool isRemoteTab() const;
    RemoteClient *currentRemoteClient() const;
    // 远程状态栏常驻提示
    void updateRemoteStatusBar();
    void closeProject();
    // 欢迎页（启动覆盖层）：无会话可恢复/会话全部失效时全屏显示
    void showWelcomePage();
    void hideWelcomePage();
    QStringList loadRecentProjects() const;
    // 多项目标签：切换当前激活的项目数据库上下文；返回是否成功
    bool activateProjectContext(const QString &projectPath);
    void registerProjectColor(const QString &projectPath);
    void refreshTabColors();

    TabManager::TabData *currentTabData() const;
    int currentTabIndex() const;
    void loadCurrentDirectory();
    void refreshDetailView();
    void navigateTo(qint64 nodeId);
    void navigateBack();
    void navigateForward();
    void navigateUp();

    void createNewComponentDialog();
    void createNewPartDialog();
    void renameSelectedNode();
    void deleteSelectedNode();
    void showPropertiesDialog();
    void importPdfToSelectedPart();
    void viewSelectedDrawing();
    void setCurrentSelectedDrawing();
    void deleteSelectedDrawing();
    DirectoryItem selectedItem() const;
    HFADMNode selectedNode() const;
    // 全部选中行中的节点（多选支持：Ctrl/Shift 多选后复制/剪切批量操作）
    QVector<HFADMNode> selectedNodes() const;
    // 全部选中行（多选支持：删除/回收站操作批量处理，含图纸行）
    QVector<DirectoryItem> selectedItems() const;
    Drawing selectedDrawing() const;
    // 图号点击打开零件最新版图纸（本地直接读 / 远程 listDir+fetch 异步）
    void openLatestDrawingForPart(const HFADMNode &node);
    // 复制图纸到缓存目录并用系统默认程序打开（程序退出时清理缓存）
    void openCachedPdf(const QString &srcPath, const QString &fileName);
    void setProjectOpenState(bool open);

    void addRecentProject(const QString &projectPath);
    void rebuildRecentProjectsMenu();
    void showStatus(const QString &message, int timeout = 3000);
    void showError(const QString &title, const QString &message);

    Ui::MainWindow *ui;
    ProjectService *m_projectService = nullptr;
    NodeService *m_nodeService = nullptr;
    DrawingService *m_drawingService = nullptr;
    TabManager *m_tabManager = nullptr;
    DirectoryNavigator *m_navigator = nullptr;
    NodeClipboard m_clipboard;
    ContextMenus m_ctxMenus;
    SessionManager *m_sessionManager = nullptr;

    QMenuBar *m_menuBar = nullptr;
    RecentProjectsMenu *m_recentMenu = nullptr;
    QStatusBar *m_statusBar = nullptr;
    QAction *m_actionNewProject = nullptr;
    QAction *m_actionOpenProject = nullptr;
    QAction *m_actionBackupProject = nullptr;
    QAction *m_actionCloseProject = nullptr;
    QAction *m_actionExit = nullptr;
    QAction *m_actionRename = nullptr;
    QAction *m_actionDelete = nullptr;
    QAction *m_actionProperties = nullptr;
    QAction *m_actionCut = nullptr;
    QAction *m_actionCopy = nullptr;
    QAction *m_actionPaste = nullptr;
    QAction *m_actionAbout = nullptr;
    QAction *m_actionOpenRemote = nullptr;
    QAction *m_actionCloseRemote = nullptr;
    QAction *m_actionManageDevices = nullptr;
    bool m_projectOpen = false;
    // 当前激活项目（标签切换时据此切换数据库上下文）
    QString m_activeProjectPath;
    QHash<QString, QColor> m_projectColors;
    // 会话恢复期间抑制 onTabChanged 的重复加载
    bool m_restoringSession = false;
    // 欢迎页覆盖层（parent 为 centralwidget，显示时铺满并置顶）
    WelcomePage *m_welcomePage = nullptr;
    // 远程访问服务端（一次开放一个机型）
    RemoteServer *m_remoteServer = nullptr;
    // 状态栏常驻远程访问提示
    QLabel *m_remoteStatusLabel = nullptr;
    // 剪贴板来源：本地=nullptr；远程=来源连接的客户端（跨域粘贴被拒绝）
    RemoteClient *m_clipboardClient = nullptr;
    // 列宽防抖保存定时器（拖动停止 400ms 后写盘）
    QTimer m_columnWidthSaveTimer;
    // 图号点击打开图纸的临时缓存目录（程序退出时自动清理）
    QTemporaryDir *m_pdfCacheDir = nullptr;
};

#endif // MAINWINDOW_H
