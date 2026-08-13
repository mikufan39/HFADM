#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "service/projectservice.h"
#include "service/nodeservice.h"
#include "service/drawingservice.h"
#include "service/remoteserver.h"
#include "service/remoteclient.h"
#include "service/remoteprotocol.h"
#include "service/languagemanager.h"
#include "model/deletionplan.h"
#include "ui/pdftabviewer.h"
#include "ui/dialogs.h"
#include "ui/recentprojectsmenu.h"
#include "ui/tabmanager.h"
#include "ui/contextmenus.h"
#include "ui/directorynavigator.h"
#include "ui/directoryassembler.h"
#include "ui/clipboard.h"
#include "ui/drawingoperations.h"
#include "ui/nodeoperations.h"
#include "ui/deleteprogressdialog.h"
#include "ui/parteditordialog.h"
#include "ui/remarkdelegate.h"
#include "ui/browsertabbar.h"
#include "ui/welcomepage.h"
#include "ui/browsertabwidget.h"
#include "ui/locationbar.h"
#include "ui/remotedialog.h"
#include "ui/remoteoperations.h"
#include "ui/remoteparteditordialog.h"
#include "ui/pairingdialog.h"
#include "ui/devicemanagerdialog.h"
#include "ui/dropimportdialog.h"

#include <QApplication>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStack>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QDesktopServices>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <memory>
#include <QKeySequence>

#include <algorithm>

namespace {
constexpr int kMaxRecentProjects = 5;
const QString kRecentProjectsKey = QStringLiteral("recentProjects");

// 多项目标签的项目色板（按打开顺序循环分配）
const QList<QColor> kProjectPalette = {
    QColor(0x37, 0x8A, 0xDD), // 蓝
    QColor(0x63, 0x99, 0x22), // 绿
    QColor(0xD8, 0x5A, 0x30), // 橙
    QColor(0xD4, 0x53, 0x7E), // 粉
    QColor(0x7F, 0x77, 0xDD), // 紫
    QColor(0xBA, 0x75, 0x17), // 琥珀
};

// Firefox photon 风格标签栏样式（浅色）
const char *kTabBarStyleSheet = R"(
QTabWidget::pane { border: none; background: transparent; }
QTabBar { background: #ececec; }
QTabBar::tab {
    background: #e3e3e3;
    color: #5f5f5f;
    border: none;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    padding: 5px 12px;
    margin-right: 3px;
    margin-top: 2px;
}
QTabBar::tab:hover:!selected { background: #d8d8d8; }
QTabBar::tab:selected { background: #ffffff; color: #111111; }
QTabBar::tab:selected:hover { background: #ffffff; }
QTabBar::close-button { margin: 2px; }
QTabBar::close-button:hover { background: #d0d0d0; border-radius: 3px; }
QMenuBar { background: transparent; border: none; }
QMenuBar::item { background: transparent; padding: 3px 6px; border-radius: 4px; }
QMenuBar::item:selected { background: #d8d8d8; }
QMenuBar::item:pressed { background: #d0d0d0; }
)";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setAcceptDrops(true); // 拖拽导入 PDF 图纸
    // LocationBar 内部已对输入框关闭文本拖放（PDF 拖拽统一交给主窗口导入）

    // 内容区（detailView）独占剩余空间，标签栏/菜单行/搜索行只占固定高度
    ui->verticalLayout->setStretch(3, 1);

    // 标签栏已由 BrowserTabWidget 在构造时换成浏览器式实现
    setStyleSheet(QString::fromLatin1(kTabBarStyleSheet));

    initServices();
    setupSingleInstance();
    m_pdfCacheDir = new QTemporaryDir;
    createMenus();
    setupShortcuts();
    setupToolbarIcons();
    setupUiConnections();
    m_ctxMenus = buildContextMenus(this);
    connectContextMenuActions();
    rebuildRecentProjectsMenu();
    setProjectOpenState(false);

    // 欢迎页覆盖层：parent 为中央内容区，默认隐藏，启动恢复会话失败时显示
    m_welcomePage = new WelcomePage(ui->centralwidget);
    m_welcomePage->hide();
    connect(m_welcomePage, &WelcomePage::createProjectRequested,
            this, &MainWindow::onNewProject);
    connect(m_welcomePage, &WelcomePage::openProjectRequested,
            this, &MainWindow::onOpenProject);
    // 连接到远程：输入目标地址（仅局域网 IPv4），连接成功后新建远程标签页
    connect(m_welcomePage, &WelcomePage::connectRemoteRequested,
            this, &MainWindow::onConnectRemote);

    restoreSession();
    // 列宽恢复放最后：读取 hfadm.session 中上次保存的宽度，无记录时用内置默认值
    restoreColumnWidths();
    // 备注列省略号截断 delegate（parent 指向 detailView 随其释放，一次性挂接）
    ui->detailView->setItemDelegateForColumn(NodeTableModel::ColRemark,
                                             new RemarkDelegate(ui->detailView));
}

MainWindow::~MainWindow()
{
    // 先释放 TabManager（其 closeAll 需要 tabWidget 仍有效）
    delete m_tabManager;
    m_tabManager = nullptr;
    delete m_pdfCacheDir;
    m_pdfCacheDir = nullptr;
    delete ui;
}

void MainWindow::initServices()
{
    m_projectService = new ProjectService(this);
    m_nodeService = new NodeService(m_projectService->databaseManager(), this);
    m_drawingService = new DrawingService(m_projectService->databaseManager(), this);
    m_drawingService->setNodeService(m_nodeService);
    m_tabManager = new TabManager(ui->tabWidget, this);
    m_navigator = new DirectoryNavigator(m_nodeService, m_tabManager, this);
    m_sessionManager = new SessionManager(this);
    m_remoteServer = new RemoteServer(m_projectService, m_nodeService, m_drawingService, this);
    connect(m_remoteServer, &RemoteServer::connectionCountChanged,
            this, [this](int) { updateRemoteStatusBar(); });
    // 配对确认解析器：收到 ConnectRequest 时弹出确认对话框，由用户选择权限；
    // 弹窗注册到服务端，客户端取消配对时可被外部关闭
    m_remoteServer->setPairingResolver([this](const RemoteProtocol::PairingRequest &req) {
        return showPairingDialog(this, req, m_remoteServer);
    });
    // 客户端取消配对：状态栏（左下角）提示
    connect(m_remoteServer, &RemoteServer::pairingCancelled, this,
            [this](const QString &address) {
        showStatus(tr("客户端（%1）取消了连接").arg(address), 8000);
    });
}

void MainWindow::createMenus()
{
    // 菜单栏位于第二层级（标签栏下方、与导航按钮同行），不再挂到 QMainWindow 顶部
    m_menuBar = ui->menuBar;

    // 锁定菜单栏尺寸策略：靠左、不随窗口宽度拉伸（与 .ui 中 sizePolicy 保持一致，双保险）
    m_menuBar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    // 菜单结构在此一次性创建；所有标题/动作文本统一在 applyMenuTexts() 中用 tr() 设置，
    // 语言切换时只需重调 applyMenuTexts() 即可全量刷新（Qt 原生多语言：源串只写一处）
    auto addAction = [this](QMenu *menu, void (MainWindow::*slot)()) {
        auto *action = new QAction(this);
        menu->addAction(action);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    m_fileMenu = new QMenu(this);
    m_menuBar->addMenu(m_fileMenu);
    m_actionNewProject = addAction(m_fileMenu, &MainWindow::onNewProject);
    m_actionOpenProject = addAction(m_fileMenu, &MainWindow::onOpenProject);
    m_recentMenu = new RecentProjectsMenu(this);
    connect(m_recentMenu, &RecentProjectsMenu::openRequested,
            this, &MainWindow::openRecentProjectByPath);
    connect(m_recentMenu, &RecentProjectsMenu::removeRequested, this, [this](const QString &path) {
        QSettings s;
        QStringList list = s.value(kRecentProjectsKey).toStringList();
        list.removeAll(path);
        s.setValue(kRecentProjectsKey, list);
        rebuildRecentProjectsMenu();
        showStatus(tr("已移除最近记录"));
    });
    m_fileMenu->addSeparator();
    m_actionBackupProject = addAction(m_fileMenu, &MainWindow::onBackupProject);
    m_actionCloseProject = addAction(m_fileMenu, &MainWindow::onCloseProject);
    m_fileMenu->addSeparator();
    m_actionExit = addAction(m_fileMenu, &MainWindow::onExit);

    m_editMenu = new QMenu(this);
    m_menuBar->addMenu(m_editMenu);
    m_actionCut = addAction(m_editMenu, &MainWindow::onCutAction);
    m_actionCut->setIcon(QIcon(QStringLiteral(":/assets/Icons/scissors.svg")));
    m_actionCopy = addAction(m_editMenu, &MainWindow::onCopyAction);
    m_actionCopy->setIcon(QIcon(QStringLiteral(":/assets/Icons/copy.svg")));
    m_actionPaste = addAction(m_editMenu, &MainWindow::onPasteAction);
    m_actionPaste->setIcon(QIcon(QStringLiteral(":/assets/Icons/clipboard.svg")));
    m_editMenu->addSeparator();
    m_actionRename = addAction(m_editMenu, &MainWindow::onRenameAction);
    m_actionRename->setIcon(QIcon(QStringLiteral(":/assets/Icons/pencil.svg")));
    m_actionDelete = addAction(m_editMenu, &MainWindow::onDeleteAction);
    m_actionDelete->setIcon(QIcon(QStringLiteral(":/assets/Icons/trash-2.svg")));
    m_actionProperties = addAction(m_editMenu, &MainWindow::onPropertiesAction);
    m_actionProperties->setIcon(QIcon(QStringLiteral(":/assets/Icons/settings.svg")));

    // 网络菜单：开放远程访问（服务端，绑定当前标签页机型） / 关闭所有连接（停止开放）
    m_networkMenu = new QMenu(this);
    m_menuBar->addMenu(m_networkMenu);
    m_actionOpenRemote = addAction(m_networkMenu, &MainWindow::onOpenRemoteAccess);
    m_actionCloseRemote = addAction(m_networkMenu, &MainWindow::onCloseRemoteAccess);
    m_networkMenu->addSeparator();
    m_actionManageDevices = addAction(m_networkMenu, &MainWindow::onManageDevices);

    // 语言菜单（网络与帮助之间）：互斥勾选 简体中文 / English；
    // 标题按元规则显示"语言"（中文）或"Language"（其他语言），见 LanguageManager::menuTitle()
    m_languageMenu = new QMenu(this);
    m_menuBar->addMenu(m_languageMenu);
    m_actionLangChinese = new QAction(this);
    m_actionLangChinese->setCheckable(true);
    m_languageMenu->addAction(m_actionLangChinese);
    connect(m_actionLangChinese, &QAction::triggered, this, [this] {
        LanguageManager::instance()->switchTo(LanguageManager::kZhCN);
    });
    m_actionLangEnglish = new QAction(this);
    m_actionLangEnglish->setCheckable(true);
    m_languageMenu->addAction(m_actionLangEnglish);
    connect(m_actionLangEnglish, &QAction::triggered, this, [this] {
        LanguageManager::instance()->switchTo(LanguageManager::kEn);
    });
    auto *langGroup = new QActionGroup(this);
    langGroup->addAction(m_actionLangChinese);
    langGroup->addAction(m_actionLangEnglish);
    // 初始勾选当前语言（switchTo 成功与否不影响勾选状态本身）
    m_actionLangChinese->setChecked(
        LanguageManager::instance()->currentLanguage() == LanguageManager::kZhCN);
    m_actionLangEnglish->setChecked(
        LanguageManager::instance()->currentLanguage() == LanguageManager::kEn);

    m_helpMenu = new QMenu(this);
    m_menuBar->addMenu(m_helpMenu);
    m_actionAbout = addAction(m_helpMenu, &MainWindow::onAbout);

    // 菜单文本统一设置（含语言菜单标题），此后语言切换时重调本函数即可
    applyMenuTexts();

    m_statusBar = new QStatusBar(this);
    setStatusBar(m_statusBar);
    // 状态栏常驻：远程访问开启时显示监听地址与连接数（默认隐藏）
    m_remoteStatusLabel = new QLabel(this);
    m_remoteStatusLabel->hide();
    m_statusBar->addPermanentWidget(m_remoteStatusLabel);
}

void MainWindow::applyMenuTexts()
{
    // 文件
    m_fileMenu->setTitle(tr("文件(&F)"));
    m_actionNewProject->setText(tr("新建项目(&N)..."));
    m_actionOpenProject->setText(tr("打开项目(&O)..."));
    m_actionBackupProject->setText(tr("备份项目(&B)..."));
    m_actionCloseProject->setText(tr("关闭项目(&C)"));
    m_actionExit->setText(tr("退出(&X)"));
    // 编辑
    m_editMenu->setTitle(tr("编辑(&E)"));
    m_actionCut->setText(tr("剪切(&T)"));
    m_actionCopy->setText(tr("复制(&C)"));
    m_actionPaste->setText(tr("粘贴(&P)"));
    m_actionRename->setText(tr("重命名(&R)"));
    m_actionDelete->setText(tr("删除(&D)"));
    m_actionProperties->setText(tr("属性(&I)"));
    // 网络
    m_networkMenu->setTitle(tr("网络(&N)"));
    m_actionOpenRemote->setText(tr("开放远程访问(&O)"));
    m_actionCloseRemote->setText(tr("关闭所有连接(&C)"));
    m_actionManageDevices->setText(tr("授权管理(&M)..."));
    // 语言：菜单标题走元规则（中文"语言"、其他"Language"），子项为各语言本地写法（不翻译）
    m_languageMenu->setTitle(LanguageManager::instance()->menuTitle());
    m_actionLangChinese->setText(LanguageManager::displayName(LanguageManager::kZhCN));
    m_actionLangEnglish->setText(LanguageManager::displayName(LanguageManager::kEn));
    // 帮助
    m_helpMenu->setTitle(tr("帮助(&H)"));
    m_actionAbout->setText(tr("关于艾锐奥智能图纸管理系统(&A)"));
}

void MainWindow::updateWindowTitle()
{
    if (m_projectOpen && !m_activeProjectPath.isEmpty()) {
        setWindowTitle(tr("艾锐奥智能图纸管理系统 - %1")
                           .arg(m_projectService->currentProjectInfo().name));
    } else {
        setWindowTitle(tr("艾锐奥智能图纸管理系统"));
    }
}

void MainWindow::setupShortcuts()
{
    m_actionNewProject->setShortcut(QKeySequence::New);
    m_actionOpenProject->setShortcut(QKeySequence::Open);
    m_actionRename->setShortcut(QKeySequence(Qt::Key_F2));
    m_actionDelete->setShortcut(QKeySequence::Delete);
    m_actionCut->setShortcut(QKeySequence::Cut);
    m_actionCopy->setShortcut(QKeySequence::Copy);
    m_actionPaste->setShortcut(QKeySequence::Paste);

    // Ctrl+F / Ctrl+L 聚焦地址栏并进入编辑态（浏览器式：全选当前路径）
    auto *searchShortcut = new QShortcut(QKeySequence::Find, this);
    connect(searchShortcut, &QShortcut::activated, this, [this] {
        ui->locationBar->focusForEditing();
    });
    auto *locationShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this);
    connect(locationShortcut, &QShortcut::activated, this, [this] {
        ui->locationBar->focusForEditing();
    });

    // Ctrl+W 关闭当前标签（Firefox 习惯）
    auto *closeTabShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+W")), this);
    connect(closeTabShortcut, &QShortcut::activated, this, [this] {
        if (m_tabManager->count() > 0) {
            onTabCloseRequested(m_tabManager->currentIndex());
        }
    });
}

void MainWindow::setupUiConnections()
{
    // 工具栏
    connect(ui->backButton, &QToolButton::clicked, this, &MainWindow::onBackClicked);
    connect(ui->forwardButton, &QToolButton::clicked, this, &MainWindow::onForwardClicked);
    connect(ui->upButton, &QToolButton::clicked, this, &MainWindow::onUpClicked);
    connect(ui->refreshButton, &QToolButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(ui->homeButton, &QToolButton::clicked, this, &MainWindow::onHomeClicked);
    connect(ui->newButton, &QToolButton::clicked, this, &MainWindow::onNewClicked);

    // 浏览交互
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(ui->tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    // 拖拽排序后项目色带需跟随新索引
    connect(ui->tabWidget->tabBar(), &QTabBar::tabMoved, this, [this](int, int) {
        refreshTabColors();
    });
    // Windows 上双击或回车均触发 activated：单连接覆盖两种打开方式，避免双击被处理两次
    connect(ui->detailView, &QTableView::activated, this, &MainWindow::onTableDoubleClicked);
    connect(ui->detailView, &QTableView::customContextMenuRequested,
            this, &MainWindow::onTableContextMenuRequested);
    // 地址栏：输入即搜索 / 面包屑段跳转 / 输入路径回车跳转 / 清除搜索
    connect(ui->locationBar, &LocationBar::searchTextChanged,
            this, &MainWindow::onSearchTextChanged);
    connect(ui->locationBar, &LocationBar::segmentClicked, this, [this](qint64 nodeId) {
        navigateTo(nodeId);
    });
    connect(ui->locationBar, &LocationBar::pathSubmitRequested,
            this, &MainWindow::onLocationPathSubmit);
    connect(ui->locationBar, &LocationBar::clearSearchRequested, this, [this] {
        loadCurrentDirectory();
        updateActionState();
    });
    connect(ui->detailView, &QTableView::clicked, this, &MainWindow::onSelectionChanged);
    connect(ui->detailView, &QTableView::clicked, this, &MainWindow::onPartNoClicked);
    connect(ui->detailView, &QTableView::pressed, this, &MainWindow::onSelectionChanged);
    connect(ui->detailView, &QTableView::activated, this, &MainWindow::onSelectionChanged);

    // 列宽记忆：拖动停止 400ms 后写盘（防抖，避免拖动过程频繁刷文件）
    m_columnWidthSaveTimer.setSingleShot(true);
    m_columnWidthSaveTimer.setInterval(400);
    connect(&m_columnWidthSaveTimer, &QTimer::timeout,
            this, &MainWindow::saveColumnWidths);
    connect(ui->detailView->horizontalHeader(), &QHeaderView::sectionResized,
            this, [this] { m_columnWidthSaveTimer.start(); });
}

void MainWindow::setupToolbarIcons()
{
    // 从资源系统加载 SVG 图标（assets/Icons/，经 CMake qt_add_resources 打包为 :/assets/Icons/）
    ui->backButton->setIcon(QIcon(QStringLiteral(":/assets/Icons/arrow-left.svg")));
    ui->forwardButton->setIcon(QIcon(QStringLiteral(":/assets/Icons/arrow-right.svg")));
    ui->upButton->setIcon(QIcon(QStringLiteral(":/assets/Icons/arrow-up.svg")));
    ui->refreshButton->setIcon(QIcon(QStringLiteral(":/assets/Icons/refresh.svg")));
    ui->homeButton->setIcon(QIcon(QStringLiteral(":/assets/Icons/home.svg")));
    ui->newButton->setIcon(QIcon(QStringLiteral(":/assets/Icons/new.svg")));

    // 工具栏按钮组：固定大小、只显示图标、不参与 toolbarLayout 水平方向拉伸
    // （与 .ui 中 sizePolicy=Maximum/Fixed 形成双保险，杜绝按钮组按窗口宽度等距分布）
    const QSize iconSize(20, 20);
    const QList<QToolButton *> toolbarButtons = {
        ui->backButton, ui->forwardButton, ui->upButton,
        ui->refreshButton, ui->homeButton, ui->newButton,
    };
    for (QToolButton *btn : toolbarButtons) {
        btn->setIconSize(iconSize);
        btn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
}

void MainWindow::setupSingleInstance()
{
    m_singleInstanceServer = new QLocalServer(this);
    if (!m_singleInstanceServer->listen(QLatin1String(kSingleInstanceKey))) {
        // 监听失败：可能是启动竞态（另一实例已就绪但 main 检测阶段未连上）或残留命名。
        // 能连上说明已有实例：通知其置前后本实例退出（不经 closeEvent，避免覆盖会话文件）
        QLocalSocket probe;
        probe.connectToServer(QLatin1String(kSingleInstanceKey));
        if (probe.waitForConnected(300)) {
            probe.write("show");
            probe.flush();
            probe.waitForBytesWritten(300);
            probe.disconnectFromServer();
            QTimer::singleShot(0, qApp, [] { qApp->quit(); });
        } else {
            QLocalServer::removeServer(QLatin1String(kSingleInstanceKey));
            m_singleInstanceServer->listen(QLatin1String(kSingleInstanceKey));
        }
    }
    // 收到重复启动通知（第二实例连接成功并写入 show）：置前窗口并提示
    connect(m_singleInstanceServer, &QLocalServer::newConnection, this, [this] {
        while (m_singleInstanceServer->hasPendingConnections()) {
            QLocalSocket *sock = m_singleInstanceServer->nextPendingConnection();
            sock->deleteLater(); // 仅作为唤醒信号，无需读取内容
        }
        activateFromSecondInstance();
    });
}

void MainWindow::activateFromSecondInstance()
{
    showNormal();
    raise();
    activateWindow();
    QMessageBox::information(this, tr("程序已在运行"),
                             tr("检测到重复启动，已切换到已打开的唯一实例。"));
}

void MainWindow::connectContextMenuActions()
{
    connect(m_ctxMenus.newComponent, &QAction::triggered, this, &MainWindow::onNewComponent);
    connect(m_ctxMenus.newPart, &QAction::triggered, this, &MainWindow::onNewPart);
    connect(m_ctxMenus.importPdf, &QAction::triggered, this, &MainWindow::onImportPdfAction);
    connect(m_ctxMenus.openInNewTab, &QAction::triggered, this, &MainWindow::onOpenInNewTabAction);
    connect(m_ctxMenus.cut, &QAction::triggered, this, &MainWindow::onCutAction);
    connect(m_ctxMenus.copy, &QAction::triggered, this, &MainWindow::onCopyAction);
    connect(m_ctxMenus.paste, &QAction::triggered, this, &MainWindow::onPasteAction);
    connect(m_ctxMenus.rename, &QAction::triggered, this, &MainWindow::onRenameAction);
    connect(m_ctxMenus.remove, &QAction::triggered, this, &MainWindow::onDeleteAction);
    connect(m_ctxMenus.properties, &QAction::triggered, this, &MainWindow::onPropertiesAction);
    connect(m_ctxMenus.viewDrawing, &QAction::triggered, this, &MainWindow::onViewDrawingAction);
    connect(m_ctxMenus.setCurrent, &QAction::triggered, this, &MainWindow::onSetCurrentDrawingAction);
    connect(m_ctxMenus.deleteDrawing, &QAction::triggered, this, &MainWindow::onDeleteDrawingAction);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 退出前停止远程访问（关闭监听 + 断开全部连接）
    if (m_remoteServer) {
        m_remoteServer->stop();
    }
    // 关闭时记住当前标签页状态，下次启动恢复
    saveSession();
    // 关闭时兜底保存列宽（拖动期间的防抖保存可能在退出前未触发）
    saveColumnWidths();
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // 欢迎页覆盖层跟随窗口尺寸同步铺满内容区
    if (m_welcomePage && m_welcomePage->isVisible()) {
        m_welcomePage->setGeometry(ui->centralwidget->rect());
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    // 构造时调用的 showWelcomePage 看不到 centralwidget 的真实几何（窗口未显示），
    // 首次显示后强制同步一次覆盖层位置与尺寸
    if (m_welcomePage && m_welcomePage->isVisible()) {
        m_welcomePage->setGeometry(ui->centralwidget->rect());
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    // 语言切换统一刷新入口：QApplication::installTranslator 后向所有顶层窗口发送
    // LanguageChange，此处重翻译 .ui 文本、代码创建的菜单文本与窗口标题
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        applyMenuTexts();
        updateWindowTitle();
        updateRemoteStatusBar(); // 状态栏常驻文本（远程访问提示）
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl &url : urls) {
            if (url.isLocalFile()
                && QFileInfo(url.toLocalFile()).suffix().compare(QStringLiteral("pdf"),
                                                                 Qt::CaseInsensitive) == 0) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }

    QStringList pdfPaths;
    int ignoredCount = 0;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        if (QFileInfo(path).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0) {
            pdfPaths.append(path);
        } else {
            ++ignoredCount;
        }
    }
    event->acceptProposedAction();
    if (pdfPaths.isEmpty()) {
        showStatus(tr("拖入的文件中没有 PDF 图纸"));
        return;
    }

    // 仅本地项目标签支持拖拽导入；远程标签走原有对话框导入流程
    if (isRemoteTab()) {
        showError(tr("拖拽导入"),
                  tr("远程标签不支持拖拽导入，请在零件编辑窗口中使用导入功能。"));
        return;
    }
    if (!m_projectOpen || m_activeProjectPath.isEmpty()) {
        showError(tr("拖拽导入"),
                  tr("请先打开项目，再拖入 PDF 图纸。"));
        return;
    }

    // 收集所有已打开的本地项目标签（多项目标签共存：拖入的图纸可导入任意已打开机型，
    // 而不只限于当前标签；同一项目多个标签（目录/PDF）按项目路径去重）
    QVector<DropTargetProject> openProjects;
    QString currentMachineName;
    const TabData *curTab = m_tabManager->currentTab();
    if (curTab) {
        currentMachineName = curTab->projectName;
    }
    for (int i = 0; i < m_tabManager->count(); ++i) {
        const TabData *tab = m_tabManager->tabAt(i);
        if (!tab || tab->type == TabManager::TabType::Remote || tab->projectPath.isEmpty()) {
            continue; // 远程标签不参与拖拽导入；无项目归属的标签跳过
        }
        bool exists = false;
        for (const DropTargetProject &p : openProjects) {
            if (p.projectPath == tab->projectPath) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            openProjects.append({tab->projectPath, tab->projectName});
        }
    }
    if (currentMachineName.isEmpty()) {
        currentMachineName = m_projectService->currentProjectInfo().name; // 兜底：与激活项目一致
    }

    // 解析/导入需要按文件机型切换数据库上下文（跨标签页导入的关键）
    auto switchContext = [this](const QString &projectPath) {
        return activateProjectContext(projectPath);
    };
    if (!resolveDropImport(this, m_nodeService, m_drawingService, openProjects,
                           currentMachineName, pdfPaths, switchContext,
                           m_activeProjectPath)) {
        return; // 无法继续（对话框内部已恢复项目上下文）
    }
    if (ignoredCount > 0) {
        showStatus(tr("已忽略非 PDF 文件 %1 个").arg(ignoredCount));
    }
    // 导入详情在面板内实时展示，完成后面板保持打开供观察，不再弹结果框
    loadCurrentDirectory();
    updateActionState();
}

// ---------- 文件菜单 ----------

void MainWindow::onNewProject()
{
    QString parentDir;
    QString projectName;
    if (!showNewProjectDialog(this, parentDir, projectName)) {
        return;
    }

    const QString projectPath = QDir(parentDir).filePath(projectName.trimmed());
    if (!m_projectService->createProject(projectPath, projectName.trimmed())) {
        showError(tr("新建项目失败"), m_projectService->lastError());
        return;
    }

    addRecentProject(projectPath);
    openProjectPath(projectPath, true);
}

void MainWindow::onOpenProject()
{
    QString projectPath;
    if (!showOpenProjectDialog(this, projectPath)) {
        return;
    }

    if (!m_projectService->openProject(projectPath)) {
        showError(tr("打开项目失败"), m_projectService->lastError());
        return;
    }

    addRecentProject(projectPath);
    openProjectPath(projectPath, false);
}

void MainWindow::openRecentProjectByPath(const QString &projectPath)
{
    if (!m_projectService->openProject(projectPath)) {
        showError(tr("打开项目失败"), m_projectService->lastError());
        rebuildRecentProjectsMenu();
        return;
    }
    addRecentProject(projectPath);
    openProjectPath(projectPath, false);
}

void MainWindow::onBackupProject()
{
    if (!m_projectOpen) {
        showStatus(tr("请先打开项目"));
        return;
    }

    QString targetDir;
    if (!showBackupTargetDialog(this, targetDir)) {
        return;
    }

    QString backupPath;
    if (!m_projectService->backupProject(targetDir, backupPath)) {
        showError(tr("备份失败"), m_projectService->lastError());
        return;
    }

    showStatus(tr("备份完成：%1").arg(QDir::toNativeSeparators(backupPath)));
    QMessageBox::information(this, tr("备份完成"),
                             tr("项目已备份到：\n%1").arg(QDir::toNativeSeparators(backupPath)));
}

void MainWindow::onExit()
{
    close();
}

// ---------- 项目浏览 ----------

bool MainWindow::openProjectPath(const QString &projectPath, bool isNewProject)
{
    Q_UNUSED(isNewProject)

    QVector<HFADMNode> roots;
    if (!m_nodeService->loadDirectory(0, roots)) {
        showError(tr("加载项目失败"), m_nodeService->lastError());
        return false;
    }

    HFADMNode root;
    for (const HFADMNode &candidate : roots) {
        if (candidate.type == NodeType::Aircraft) {
            root = candidate;
            break;
        }
    }
    if (root.id == 0) {
        showError(tr("加载项目失败"), tr("未找到机型根节点"));
        return false;
    }

    // 多项目标签共存：不再关闭已有标签，仅在标签栏追加本项目标签
    m_drawingService->setProjectPath(projectPath);
    m_nodeService->setProjectPath(projectPath);
    m_activeProjectPath = projectPath;
    registerProjectColor(projectPath);

    const ProjectInfo info = m_projectService->currentProjectInfo();
    m_tabManager->openDirectoryTab(root.name, root.id, projectPath, info.name);
    refreshTabColors();
    loadCurrentDirectory();
    setProjectOpenState(true);

    updateWindowTitle(); // 标题随语言/项目名：艾锐奥智能图纸管理系统 - 项目名
    showStatus(tr("项目已打开：%1").arg(info.name));
    hideWelcomePage(); // 打开项目成功：退出欢迎页覆盖层
    return true;
}

bool MainWindow::isCurrentTabPdf() const
{
    return m_tabManager->currentTabType() == TabManager::TabType::Pdf;
}

void MainWindow::openPdfTab(const Drawing &drawing)
{
    const QString fullPath = DrawingService::resolveDrawingPath(
        m_drawingService->projectPath(), drawing.filePath);
    if (!QFile::exists(fullPath)) {
        showError(tr("打开图纸失败"),
                  tr("文件不存在：%1").arg(fullPath));
        return;
    }

    bool ok = false;
    m_tabManager->openPdfTab(drawing, fullPath, m_activeProjectPath,
                             m_projectService->currentProjectInfo().name, &ok);
    if (!ok) {
        showError(tr("打开图纸失败"),
                  tr("PDF 加载失败：%1").arg(fullPath));
        return;
    }
    refreshTabColors();
    updateNavigationState();
    showStatus(tr("已打开图纸：%1").arg(drawing.fileName));
}

void MainWindow::closeProject()
{
    // 停止远程开放（关闭监听 + 断开全部连接），释放全部远程客户端
    if (m_remoteServer) {
        m_remoteServer->stop();
        updateRemoteStatusBar();
    }
    for (int i = 0; i < m_tabManager->count(); ++i) {
        RemoteClient *client = m_tabManager->tabAt(i)->remoteClient;
        if (client) {
            client->deleteLater();
        }
    }
    m_clipboardClient = nullptr;
    m_tabManager->closeAll();
    ui->locationBar->clearSearch();
    ui->locationBar->setPath({});
    m_locationBarKey.clear();
    m_clipboard = NodeClipboard();
    m_activeProjectPath.clear();
    m_projectColors.clear();
    setProjectOpenState(false);
    updateWindowTitle(); // 无项目：仅产品名
    showWelcomePage(); // 无任何标签可看：回到欢迎页
}

// ---------- 会话保存 / 恢复 ----------

void MainWindow::saveSession()
{
    QList<SessionManager::SessionTab> tabs;
    for (int i = 0; i < m_tabManager->count(); ++i) {
        const TabData *tab = m_tabManager->tabAt(i);
        if (!tab) {
            continue;
        }
        if (tab->remoteClient) {
            continue; // 远程会话标签不持久化（重启后不恢复）
        }
        SessionManager::SessionTab st;
        st.type = tab->type;
        st.projectPath = tab->projectPath;
        st.currentNodeId = tab->currentNodeId;
        st.pdfFilePath = tab->pdfFilePath;
        tabs.append(st);
    }
    if (!m_sessionManager->save(tabs, m_tabManager->currentIndex())) {
        qWarning() << "MainWindow: 保存会话失败" << SessionManager::sessionFilePath();
    }
}

void MainWindow::restoreSession()
{
    int activeIndex = 0;
    const QList<SessionManager::SessionTab> tabs = m_sessionManager->load(&activeIndex);
    if (tabs.isEmpty()) {
        showWelcomePage(); // 无记录：显示欢迎页
        return;
    }

    m_restoringSession = true;
    for (const SessionManager::SessionTab &tab : tabs) {
        restoreOneTab(tab);
    }
    refreshTabColors();
    m_restoringSession = false;

    if (m_tabManager->isEmpty()) {
        showWelcomePage(); // 会话项目全部失效：显示欢迎页
        return;
    }
    // 单标签恢复时 setCurrentIndex(activeIndex) 不会触发 currentChanged（index 未变），
    // 导致 onTabChanged / activateProjectContext 不执行 → 窗口标题缺项目名 + 后续
    // setProjectOpenState(true) 之前的 updateActionState 用的是错误的项目上下文缓存。
    // 这里显式对当前 tab 调用 activateProjectContext，保证标题与上下文缓存就绪。
    if (TabData *t = m_tabManager->tabAt(qBound(0, activeIndex, m_tabManager->count() - 1))) {
        activateProjectContext(t->projectPath);
    }
    m_tabManager->setCurrentIndex(qBound(0, activeIndex, m_tabManager->count() - 1));
    loadCurrentDirectory();
    setProjectOpenState(true);
    showStatus(tr("已恢复上次会话"));
}

void MainWindow::restoreOneTab(const SessionManager::SessionTab &st)
{
    if (!m_projectService->openProject(st.projectPath)) {
        qWarning() << "MainWindow: 会话项目失效，跳过" << st.projectPath;
        return;
    }
    m_drawingService->setProjectPath(st.projectPath);
    m_nodeService->setProjectPath(st.projectPath);
    registerProjectColor(st.projectPath);
    const QString projectName = m_projectService->currentProjectInfo().name;

    switch (st.type) {
    case TabManager::TabType::Directory: {
        // 节点已不存在时退回项目根节点
        qint64 nodeId = st.currentNodeId;
        HFADMNode node;
        if (!m_nodeService->getNode(nodeId, node)) {
            QVector<HFADMNode> roots;
            if (m_nodeService->loadDirectory(0, roots)) {
                for (const HFADMNode &candidate : roots) {
                    if (candidate.type == NodeType::Aircraft) {
                        nodeId = candidate.id;
                        break;
                    }
                }
            }
        }
        QString title = projectName;
        HFADMNode current;
        if (m_nodeService->getNode(nodeId, current)) {
            title = (current.type == NodeType::Aircraft)
                ? projectName
                : QStringLiteral("%1: %2").arg(projectName, current.name);
        }
        m_tabManager->openDirectoryTab(title, nodeId, st.projectPath, projectName);
        break;
    }
    case TabManager::TabType::Pdf:
        if (!st.pdfFilePath.isEmpty() && QFile::exists(st.pdfFilePath)) {
            Drawing drawing;
            drawing.fileName = QFileInfo(st.pdfFilePath).fileName();
            m_tabManager->openPdfTab(drawing, st.pdfFilePath, st.projectPath, projectName);
        }
        break;
    }
}

void MainWindow::saveColumnWidths()
{
    QList<int> widths;
    auto *header = ui->detailView->horizontalHeader();
    for (int c = 0; c < NodeTableModel::ColCount; ++c) {
        widths.append(header->sectionSize(c));
    }
    if (!m_sessionManager->saveColumnWidths(widths)) {
        qWarning() << "MainWindow: 保存列宽失败" << SessionManager::sessionFilePath();
    }
}

void MainWindow::restoreColumnWidths()
{
    auto *header = ui->detailView->horizontalHeader();
    const QList<int> saved = m_sessionManager->loadColumnWidths();
    // 内置默认列宽：名称列最宽，其余按内容类型取合理值；仅作为无记录时的兜底
    const int fallback[NodeTableModel::ColCount] = { 320, 120, 200, 90, 170, 160 };
    for (int c = 0; c < NodeTableModel::ColCount; ++c) {
        const int w = (c < saved.size() && saved.at(c) > 0) ? saved.at(c) : fallback[c];
        header->resizeSection(c, w);
    }
}

// ---------- 欢迎页（启动覆盖层） ----------

QStringList MainWindow::loadRecentProjects() const
{
    QSettings settings;
    return settings.value(kRecentProjectsKey).toStringList();
}

void MainWindow::showWelcomePage()
{
    if (!m_welcomePage) {
        return;
    }
    m_welcomePage->setGeometry(ui->centralwidget->rect());
    m_welcomePage->raise();
    m_welcomePage->show();
    refreshDetailView();
    updateNavigationState();
    updateActionState();
}

void MainWindow::hideWelcomePage()
{
    if (m_welcomePage) {
        m_welcomePage->hide();
    }
}

void MainWindow::setProjectOpenState(bool open)
{
    m_projectOpen = open;
    ui->backButton->setEnabled(open);
    ui->forwardButton->setEnabled(open);
    ui->upButton->setEnabled(open);
    ui->refreshButton->setEnabled(open);
    ui->newButton->setEnabled(open);
    ui->locationBar->setEnabled(open);
    ui->detailView->setEnabled(open);
    m_actionRename->setEnabled(false);
    m_actionDelete->setEnabled(false);
    m_actionProperties->setEnabled(false);
    m_actionCut->setEnabled(false);
    m_actionCopy->setEnabled(false);
    m_actionPaste->setEnabled(false);
    m_actionBackupProject->setEnabled(open);
    m_actionCloseProject->setEnabled(open);
    m_actionOpenRemote->setEnabled(false);
    m_actionCloseRemote->setEnabled(false);
    updateNavigationState();
    // m_projectOpen 是 updateActionState 中大量动作启用条件的依据；
    // 必须在 m_projectOpen 变更后立即刷新动作状态，否则首次右键（无 selection 变化）
    // 仍会看到上次 updateActionState 时的旧状态（典型场景：会话恢复 + 空白处右键）
    updateActionState();
}

// ---------- 目录加载与导航 ----------

MainWindow::TabData *MainWindow::currentTabData() const
{
    return m_tabManager->currentTab();
}

int MainWindow::currentTabIndex() const
{
    return m_tabManager->currentIndex();
}

void MainWindow::loadCurrentDirectory()
{
    TabData *tab = currentTabData();
    if (!tab) {
        return;
    }

    // 远程标签：目录数据异步获取（搜索走远程递归搜索），回调里填表
    if (isRemoteTab() && tab->type == TabManager::TabType::Remote) {
        const QString keyword = ui->locationBar->searchText();
        RemoteClient *client = tab->remoteClient;
        const qint64 nodeId = tab->currentNodeId;
        const qint64 reqId = keyword.isEmpty()
            ? client->listDirAsync(nodeId)
            : client->searchAsync(nodeId, keyword);
        QPointer<RemoteClient> guard(client);
        awaitOnce(client, reqId, this, [this, guard, nodeId](bool ok, const QJsonObject &data, const QString &err) {
            if (!guard) {
                return;
            }
            TabData *cur = currentTabData();
            if (!cur || cur->remoteClient != guard || cur->currentNodeId != nodeId) {
                return; // 标签已切换或导航离开，丢弃过期回调
            }
            if (!ok) {
                showError(tr("远程加载目录失败"), err);
                return;
            }
            QVector<DirectoryItem> items;
            const QJsonArray arr = data.value(QStringLiteral("items")).toArray();
            for (const QJsonValue &v : arr) {
                items.append(RemoteProtocol::directoryItemFromJson(v.toObject()));
            }
            cur->items = items;
            cur->model->setItems(items);
            cur->model->setFilterText(QString());
            // 当前节点类型缓存（服务端 listDir/search 响应附带），供动作状态判断
            cur->currentNodeType = static_cast<NodeType>(
                data.value(QStringLiteral("currentType"))
                    .toInt(static_cast<int>(cur->currentNodeType)));
            // 远程标签标题：与本地一致（根=机型名，子目录=机型名: 名称）
            const QString currentName = data.value(QStringLiteral("currentName")).toString();
            if (!currentName.isEmpty()) {
                const QString title = (cur->currentNodeType == NodeType::Aircraft)
                    ? cur->projectName
                    : QStringLiteral("%1: %2").arg(cur->projectName, currentName);
                if (title != cur->title) {
                    m_tabManager->setTabTitle(currentTabIndex(), title);
                }
            }
            refreshDetailView();
            updateLocationBar();
        });
        return;
    }

    // 目录标签标题随导航更新：根页=项目名，子目录=项目名: 目录名
    if (tab->type == TabManager::TabType::Directory) {
        HFADMNode current;
        if (m_nodeService->getNode(tab->currentNodeId, current)) {
            const QString title = (current.type == NodeType::Aircraft)
                ? tab->projectName
                : QStringLiteral("%1: %2").arg(tab->projectName, current.name);
            if (title != tab->title) {
                m_tabManager->setTabTitle(currentTabIndex(), title);
            }
        }
    }

    QVector<DirectoryItem> items;
    QString error;
    const QString keyword = ui->locationBar->searchText();
    bool ok = false;
    if (tab->type == TabManager::TabType::Directory && !keyword.isEmpty()) {
        // 递归搜索：当前目录 + 全部子目录（仅节点名称，不含图纸），结果带路径定位
        ok = assembleSearchResults(m_nodeService, m_drawingService,
                                   tab->currentNodeId, keyword, items, &error);
    } else {
        ok = assembleDirectoryItems(m_nodeService, m_drawingService, tab, items, &error);
    }
    if (!ok) {
        showError(tr("加载目录失败"), error);
        return;
    }

    tab->items = items;
    if (tab->model) {
        tab->model->setItems(items);
        tab->model->setFilterText(QString()); // 过滤已在装配层完成
    }
    refreshDetailView();
    updateLocationBar();

    // 本地目录标签、非搜索模式：状态栏临时显示子树零件/图纸统计
    // （远程标签异步分支已提前返回；PDF 标签被 type 判断挡掉；搜索模式跳过）
    if (tab->type == TabManager::TabType::Directory && keyword.isEmpty()) {
        showSubtreeStats(tab->currentNodeId);
    }
}

void MainWindow::updateLocationBar()
{
    TabData *tab = currentTabData();
    if (!tab) {
        ui->locationBar->setPath({});
        return;
    }
    if (tab->type == TabManager::TabType::Pdf) {
        ui->locationBar->setPdfMode(true);
        return;
    }
    // 搜索/刷新不改变目录：同一定位（项目+节点）不重复刷新链（远程避免额外网络往返）
    const QString key = QStringLiteral("%1:%2").arg(tab->projectPath).arg(tab->currentNodeId);
    if (key == m_locationBarKey) {
        return;
    }
    m_locationBarKey = key;
    if (tab->type == TabManager::TabType::Remote) {
        RemoteClient *client = tab->remoteClient;
        if (!client) {
            return;
        }
        const qint64 nodeId = tab->currentNodeId;
        const qint64 reqId = client->getPathAsync(nodeId, 0);
        QPointer<RemoteClient> guard(client);
        awaitOnce(client, reqId, this,
                  [this, guard, nodeId](bool ok, const QJsonObject &data, const QString &) {
            if (!guard) {
                return;
            }
            TabData *cur = currentTabData();
            if (!cur || cur->remoteClient != guard || cur->currentNodeId != nodeId) {
                return; // 标签已切换或导航离开，丢弃过期回调
            }
            // getPath 响应附带完整链（服务端从机型根到 nodeId，含两端）
            QVector<HFADMNode> chain;
            const QJsonArray arr = data.value(QStringLiteral("chain")).toArray();
            for (const QJsonValue &v : arr) {
                chain.append(RemoteProtocol::nodeFromJson(v.toObject()));
            }
            if (!chain.isEmpty()) {
                ui->locationBar->setPath(chain);
            }
        });
        return;
    }
    QVector<HFADMNode> chain;
    if (m_nodeService->getNodeChain(tab->currentNodeId, chain)) {
        ui->locationBar->setPath(chain);
    }
}

void MainWindow::onLocationPathSubmit(const QString &text)
{
    TabData *tab = currentTabData();
    if (!tab || tab->type == TabManager::TabType::Pdf || tab->rootNodeId == 0) {
        return;
    }
    // 统一分隔符为 '/' 后拆段（支持 / › >）
    QString normalized = text;
    normalized.replace(QStringLiteral("›"), QStringLiteral("/")); // 分隔符符号，不参与翻译
    normalized.replace(QLatin1Char('>'), QLatin1Char('/'));
    const QStringList segments = normalized.split(QLatin1Char('/'));
    const qint64 rootId = tab->rootNodeId;

    if (tab->type == TabManager::TabType::Remote) {
        RemoteClient *client = tab->remoteClient;
        if (!client) {
            return;
        }
        const qint64 reqId = client->resolvePathAsync(rootId, segments);
        QPointer<RemoteClient> guard(client);
        awaitOnce(client, reqId, this,
                  [this, guard](bool ok, const QJsonObject &data, const QString &err) {
            if (!guard) {
                return;
            }
            TabData *cur = currentTabData();
            if (!cur || cur->remoteClient != guard) {
                return;
            }
            if (!ok) {
                showError(tr("路径跳转失败"), err);
                return; // 保持编辑态供修改
            }
            const qint64 nodeId = data.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            if (nodeId == 0) {
                showError(tr("路径跳转失败"), tr("目标节点不存在"));
                return;
            }
            ui->locationBar->finishPathJump();
            navigateTo(nodeId);
        });
        return;
    }

    qint64 nodeId = 0;
    QString err;
    if (!m_nodeService->resolvePath(rootId, segments, nodeId, &err)) {
        showError(tr("路径跳转失败"), err);
        return; // 保持编辑态供修改
    }
    ui->locationBar->finishPathJump();
    navigateTo(nodeId);
}

bool MainWindow::showSubtreeStats(qint64 rootNodeId)
{
    int partCount = 0;
    int drawingCount = 0;
    if (!m_nodeService->countSubtreeStats(rootNodeId, partCount, drawingCount)) {
        return false;
    }
    // 链式 arg 两次（勿用 .arg(a, b) 重载：数字超过 9 会错位）
    showStatus(tr("共 %1 个零件、%2 张图纸").arg(partCount).arg(drawingCount), 5000);
    return true;
}

void MainWindow::refreshDetailView()
{
    TabData *tab = currentTabData();
    if (!tab) {
        ui->detailView->setModel(nullptr);
        updateNavigationState();
        updateActionState();
        return;
    }

    ui->detailView->setModel(tab->model);
    // 资源管理器式列表：所有列可拖动调整列宽、可拖动换位、点击表头排序（名称列按拼音）
    auto *header = ui->detailView->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setSectionsMovable(true);
    header->setSortIndicatorShown(true);
    header->setStretchLastSection(false);
    ui->detailView->setSortingEnabled(true);
    // 强调色 #39c5bb：悬停提示 16% 不透明度，选中高亮 66% 不透明度
    ui->detailView->setStyleSheet(QStringLiteral(
        "QTableView::item:hover {"
        "  background-color: rgba(57, 197, 187, 0.16);"
        "}"
        "QTableView::item:selected {"
        "  background-color: rgba(57, 197, 187, 0.66);"
        "  color: #111111;"
        "}"
        "QTableView::item:selected:!active {"
        "  background-color: rgba(57, 197, 187, 0.66);"
        "  color: #111111;"
        "}"));
    updateNavigationState();
    updateActionState();
}

void MainWindow::navigateTo(qint64 nodeId)
{
    TabData *tab = currentTabData();
    if (!tab) {
        return;
    }
    m_navigator->navigateTo(tab, nodeId);
    loadCurrentDirectory();
}

void MainWindow::navigateBack()
{
    TabData *tab = currentTabData();
    if (!tab) {
        return;
    }
    m_navigator->navigateBack(tab);
    loadCurrentDirectory();
}

void MainWindow::navigateForward()
{
    TabData *tab = currentTabData();
    if (!tab) {
        return;
    }
    m_navigator->navigateForward(tab);
    loadCurrentDirectory();
}

void MainWindow::navigateUp()
{
    TabData *tab = currentTabData();
    if (!tab) {
        return;
    }
    if (tab->type == TabManager::TabType::Remote) {
        // 远程：异步查父节点后入栈导航
        RemoteClient *client = tab->remoteClient;
        const qint64 nodeId = tab->currentNodeId;
        const qint64 reqId = client->getNodeAsync(nodeId);
        QPointer<RemoteClient> guard(client);
        awaitOnce(client, reqId, this, [this, guard, nodeId](bool ok, const QJsonObject &data, const QString &err) {
            if (!guard) {
                return;
            }
            TabData *cur = currentTabData();
            if (!cur || cur->remoteClient != guard || cur->currentNodeId != nodeId) {
                return;
            }
            if (!ok) {
                showError(tr("远程加载失败"), err);
                return;
            }
            const HFADMNode node = RemoteProtocol::nodeFromJson(
                data.value(QStringLiteral("node")).toObject());
            if (node.parentId != 0) {
                m_navigator->navigateTo(cur, node.parentId);
                loadCurrentDirectory();
            }
        });
        return;
    }
    if (m_navigator->navigateUp(tab)) {
        loadCurrentDirectory();
    }
}

void MainWindow::updateNavigationState()
{
    TabData *tab = currentTabData();
    // 目录模式：本地目录页依赖项目打开；远程目录页不依赖本地项目
    const bool dirMode = tab
        && ((m_projectOpen && tab->type == TabManager::TabType::Directory)
            || tab->type == TabManager::TabType::Remote);
    if (!dirMode) {
        ui->backButton->setEnabled(false);
        ui->forwardButton->setEnabled(false);
        ui->upButton->setEnabled(false);
        ui->homeButton->setEnabled(false);
        ui->newButton->setEnabled(false);
        ui->refreshButton->setEnabled(false);
        // 地址栏：PDF 标签显示占位（图纸），否则整体禁用
        ui->locationBar->setPdfMode(isCurrentTabPdf());
        ui->locationBar->setEnabled(false);
        ui->detailView->setEnabled(false);
        return;
    }

    ui->locationBar->setPdfMode(false);
    ui->locationBar->setEnabled(true);
    ui->backButton->setEnabled(!tab->backStack.isEmpty());
    ui->forwardButton->setEnabled(!tab->forwardStack.isEmpty());
    if (tab->type == TabManager::TabType::Remote) {
        // 远程：根节点不可上一级（用 currentNodeId != rootNodeId 判断，免远程查询）
        const bool canUp = tab->remoteClient
            && tab->currentNodeId != tab->remoteClient->rootNodeId();
        ui->upButton->setEnabled(canUp);
    } else {
        ui->upButton->setEnabled(m_navigator->canGoUp(tab));
    }
    // 受限（只读）远程授权：新建入口置灰（菜单项本身也已置灰）
    const bool remoteReadOnly = tab->type == TabManager::TabType::Remote
        && tab->remoteClient
        && tab->remoteClient->permission() == RemoteProtocol::Permission::ReadOnly;
    ui->newButton->setEnabled(!remoteReadOnly);
    // 主页按钮：始终可回到当前标签所属机型的根目录
    ui->homeButton->setEnabled(true);
    // 刷新按钮与目录表：远程标签不依赖本地项目打开，按 dirMode 启用
    ui->refreshButton->setEnabled(true);
    ui->detailView->setEnabled(true);
}

NodeType MainWindow::currentDirectoryNodeType() const
{
    const TabData *tab = currentTabData();
    if (!tab) {
        return static_cast<NodeType>(0);
    }
    if (tab->remoteClient) {
        // 远程标签：读 listDir/search 响应写入的缓存，避免每次 UI 事件发起网络往返
        return tab->currentNodeType;
    }
    HFADMNode current;
    if (m_nodeService && m_nodeService->getNode(tab->currentNodeId, current)) {
        return current.type;
    }
    return static_cast<NodeType>(0);
}

void MainWindow::updateActionState()
{
    // 目录页（本地或远程）；远程页不依赖本地项目打开状态
    TabData *tab = currentTabData();
    const bool dirTab = tab
        && ((m_projectOpen && tab->type == TabManager::TabType::Directory)
            || tab->type == TabManager::TabType::Remote);
    const bool remoteTab = isRemoteTab();
    // 受限（只读）授权：仅禁用向服务端提交修改的入口，浏览/搜索/查看/导出保持正常
    const RemoteClient *remoteClient = currentRemoteClient();
    const bool readOnly = remoteTab && remoteClient
        && remoteClient->permission() == RemoteProtocol::Permission::ReadOnly;
    const DirectoryItem item = selectedItem();
    const bool hasNode = item.kind == DirectoryItem::Kind::Node && item.node.id != 0;
    const bool hasDrawing = item.kind == DirectoryItem::Kind::Drawing && item.drawing.id != 0;
    const bool canRenameDelete = dirTab && hasNode
        && item.node.type != NodeType::Aircraft && !readOnly;

    // 复制/剪切/删除支持多选：以全部选中节点集合判断可用性
    const QVector<HFADMNode> selNodes = selectedNodes();
    const bool hasSelNode = !selNodes.isEmpty();
    const bool hasCuttable = std::any_of(selNodes.begin(), selNodes.end(),
                                         [](const HFADMNode &n) {
                                             return n.type != NodeType::Aircraft;
                                         });

    m_actionRename->setEnabled(canRenameDelete);
    m_actionDelete->setEnabled(dirTab && hasCuttable && !readOnly);
    m_actionProperties->setEnabled(dirTab && hasNode && !readOnly);
    m_actionCut->setEnabled(dirTab && hasCuttable && !readOnly);
    m_actionCopy->setEnabled(dirTab && hasSelNode); // 复制仅本地剪贴板，受限下仍可用
    m_actionPaste->setEnabled(dirTab && m_clipboard.valid() && !readOnly);

    m_ctxMenus.rename->setEnabled(canRenameDelete);
    m_ctxMenus.remove->setEnabled(dirTab && hasCuttable && !readOnly);
    m_ctxMenus.properties->setEnabled(dirTab && hasNode && !readOnly);
    m_ctxMenus.cut->setEnabled(dirTab && hasCuttable && !readOnly);
    m_ctxMenus.copy->setEnabled(dirTab && hasSelNode);
    m_ctxMenus.paste->setEnabled(dirTab && m_clipboard.valid() && !readOnly);

    // 在新标签页打开：仅部件/机型节点（零件不支持多标签打开）
    const bool canOpenInNewTab = dirTab && hasNode
        && (item.node.type == NodeType::Aircraft || item.node.type == NodeType::Component);
    m_ctxMenus.openInNewTab->setEnabled(canOpenInNewTab);

    // 导入 PDF：仅当当前目录是零件（图纸只能挂零件下，图纸行右键菜单使用）
    // 当前目录节点类型：远程标签用缓存（listDir/search 响应附带），本地标签查库
    const NodeType currentType = currentDirectoryNodeType();
    const bool inPartDirectory = dirTab && currentType == NodeType::Part;
    m_ctxMenus.importPdf->setEnabled(inPartDirectory && !readOnly);

    // 新建菜单：机型目录下仅允许新建部件；部件目录下部件/零件均可
    const bool newComponentEnabled = dirTab
        && (currentType == NodeType::Aircraft || currentType == NodeType::Component);
    const bool newPartEnabled = dirTab && currentType == NodeType::Component;
    m_ctxMenus.newComponent->setEnabled(newComponentEnabled && !readOnly);
    m_ctxMenus.newPart->setEnabled(newPartEnabled && !readOnly);

    m_ctxMenus.viewDrawing->setEnabled(hasDrawing);
    m_ctxMenus.setCurrent->setEnabled(hasDrawing && !readOnly);
    m_ctxMenus.deleteDrawing->setEnabled(hasDrawing && !readOnly);

    // 网络菜单：开放/关闭互斥（按当前标签页机型的开放状态）
    const bool remoteRunning = m_remoteServer && m_remoteServer->isRunning();
    const bool currentBound = remoteRunning
        && m_remoteServer->projectPath() == m_activeProjectPath;
    if (remoteTab) {
        m_actionOpenRemote->setEnabled(false);
        m_actionCloseRemote->setEnabled(false);
    } else {
        m_actionOpenRemote->setEnabled(m_projectOpen && !currentBound);
        m_actionCloseRemote->setEnabled(m_projectOpen && currentBound);
    }
    // 设备管理：授权全局存储，不依赖当前是否开放机型，始终可用
    m_actionManageDevices->setEnabled(true);
}

// ---------- 工具栏 ----------

void MainWindow::onBackClicked() { navigateBack(); }
void MainWindow::onForwardClicked() { navigateForward(); }
void MainWindow::onUpClicked() { navigateUp(); }

void MainWindow::onRefreshClicked()
{
    loadCurrentDirectory();
    // 本地非搜索模式下，子树统计已作为刷新反馈显示；搜索/远程无统计时回退"已刷新"
    if (isRemoteTab() || ui->locationBar->isSearching()) {
        showStatus(tr("已刷新"));
    }
}

void MainWindow::onHomeClicked()
{
    TabData *tab = currentTabData();
    if (!tab) {
        return;
    }
    if (tab->type == TabManager::TabType::Remote) {
        // 远程标签：回到服务端机型根节点（hello 握手时返回 projectRootNodeId）
        if (!tab->remoteClient) {
            return;
        }
        m_navigator->navigateTo(tab, tab->remoteClient->rootNodeId());
        loadCurrentDirectory();
        showStatus(tr("已回到机型根目录"));
        return;
    }
    if (tab->type != TabManager::TabType::Directory) {
        return; // PDF 等页面不适用
    }
    // 多项目标签共存：先切到当前标签所属项目的数据库上下文
    if (!activateProjectContext(tab->projectPath)) {
        return;
    }
    // 查找本项目机型根节点（type=Aircraft，parent_id=NULL）
    QVector<HFADMNode> roots;
    if (!m_nodeService->loadDirectory(0, roots)) {
        showError(tr("加载项目失败"), m_nodeService->lastError());
        return;
    }
    qint64 rootId = 0;
    for (const HFADMNode &candidate : roots) {
        if (candidate.type == NodeType::Aircraft) {
            rootId = candidate.id;
            break;
        }
    }
    if (rootId == 0) {
        showError(tr("操作失败"), tr("未找到机型根节点"));
        return;
    }
    if (tab->currentNodeId == rootId) {
        showStatus(tr("已在机型根目录"));
        return;
    }
    m_navigator->navigateTo(tab, rootId);
    loadCurrentDirectory();
    showStatus(tr("已回到机型根目录"));
}

void MainWindow::onNewClicked()
{
    if (!currentTabData() || isCurrentTabPdf()) {
        return;
    }
    m_ctxMenus.newMenu->popup(ui->newButton->mapToGlobal(
        QPoint(0, ui->newButton->height())));
}

// ---------- 新建 ----------

void MainWindow::onNewComponent() { createNewComponentDialog(); }
void MainWindow::onNewPart() { createNewPartDialog(); }

void MainWindow::createNewComponentDialog()
{
    TabData *tab = currentTabData();
    if (!tab) {
        return;
    }

    // 远程标签：前缀与写入均走协议
    if (tab->type == TabManager::TabType::Remote && tab->remoteClient) {
        RemoteClient *client = tab->remoteClient;
        QString prefix;
        QString full;
        if (client->computeFullPartNo(tab->currentNodeId, full, nullptr)) {
            const int dot = full.indexOf(QLatin1Char('.'));
            const QString aircraft = dot > 0 ? full.left(dot) : full;
            if (!aircraft.isEmpty()) {
                prefix = aircraft + QStringLiteral(".");
            }
        }
        QString name;
        QString partNo;
        int quantity = 1;
        QString remark;
        if (!showNewComponentDialog(this, name, partNo, prefix, quantity, remark)) {
            return;
        }
        QString err;
        if (!client->createComponent(tab->currentNodeId, name.trimmed(), partNo, quantity,
                                     &err, remark)) {
            showError(tr("新建部件失败"), err);
            return;
        }
        loadCurrentDirectory();
        showStatus(tr("部件已创建：%1").arg(name.trimmed()));
        return;
    }

    // 部件图号前缀 = 机型名 + "."（任意层级部件均不继承父部件段）
    QString prefix;
    HFADMNode current;
    if (m_nodeService->getNode(tab->currentNodeId, current)) {
        const QString full = m_nodeService->computeFullPartNo(tab->currentNodeId);
        const int dot = full.indexOf(QLatin1Char('.'));
        const QString aircraft = dot > 0 ? full.left(dot) : full;
        if (!aircraft.isEmpty()) {
            prefix = aircraft + QStringLiteral(".");
        }
    }

    QString name;
    QString partNo;
    int quantity = 1;
    QString remark;
    if (!showNewComponentDialog(this, name, partNo, prefix, quantity, remark)) {
        return;
    }

    if (!m_nodeService->createComponent(tab->currentNodeId, name.trimmed(), partNo,
                                        quantity, remark)) {
        showError(tr("新建部件失败"), m_nodeService->lastError());
        return;
    }
    loadCurrentDirectory();
    showStatus(tr("部件已创建：%1").arg(name.trimmed()));
}

void MainWindow::createNewPartDialog()
{
    TabData *tab = currentTabData();
    if (!tab) {
        return;
    }

    // 材质自动补全列表（当前激活项目已用材质，去重）；供对话框材质输入提示
    const QStringList materialList = m_nodeService->fetchMaterialList();

    // 远程标签：前缀、写入、随建导入 PDF 均走协议
    if (tab->type == TabManager::TabType::Remote && tab->remoteClient) {
        RemoteClient *client = tab->remoteClient;
        QString prefix;
        QString full;
        if (client->computeFullPartNo(tab->currentNodeId, full, nullptr)) {
            if (!full.isEmpty()) {
                prefix = full + QStringLiteral(".");
            }
        }
        QString name;
        QString partNo;
        QString material;
        int quantity = 1;
        QString pdfFilePath;
        QString remark;
        if (!showNewPartDialog(this, name, partNo, prefix, material, quantity, pdfFilePath, remark,
                               materialList)) {
            return;
        }
        QString err;
        qint64 newNodeId = 0;
        if (!client->createPart(tab->currentNodeId, name, partNo, material, quantity,
                                &newNodeId, &err, remark)) {
            showError(tr("新建零件失败"), err);
            return;
        }
        if (!pdfFilePath.isEmpty() && newNodeId != 0) {
            QString importErr;
            if (!client->importPdf(newNodeId, pdfFilePath, &importErr)) {
                showError(tr("零件已创建，但图纸导入失败"), importErr);
                loadCurrentDirectory();
                return;
            }
        }
        loadCurrentDirectory();
        showStatus(tr("零件已创建：%1").arg(name));
        return;
    }

    // 零件图号前缀 = 父节点完整图号 + "."
    QString prefix;
    HFADMNode current;
    if (m_nodeService->getNode(tab->currentNodeId, current)) {
        const QString full = m_nodeService->computeFullPartNo(tab->currentNodeId);
        if (!full.isEmpty()) {
            prefix = full + QStringLiteral(".");
        }
    }

    QString name;
    QString partNo;
    QString material;
    int quantity = 1;
    QString pdfFilePath;
    QString remark;
    if (!showNewPartDialog(this, name, partNo, prefix, material, quantity, pdfFilePath, remark,
                           materialList)) {
        return;
    }

    qint64 newNodeId = 0;
    if (!m_nodeService->createPart(tab->currentNodeId, name, partNo,
                                   material, quantity, &newNodeId, remark)) {
        showError(tr("新建零件失败"), m_nodeService->lastError());
        return;
    }

    // 创建时若选择了图纸 PDF，立即导入（自动命名 {完整图号}{版本字母}_{零件名}.pdf）
    if (!pdfFilePath.isEmpty() && newNodeId != 0 && m_drawingService) {
        if (!m_drawingService->importPdf(newNodeId, pdfFilePath)) {
            showError(tr("零件已创建，但图纸导入失败"),
                      m_drawingService->lastError());
            loadCurrentDirectory();
            return;
        }
    }
    loadCurrentDirectory();
    showStatus(tr("零件已创建：%1").arg(name));
}

// ---------- 重命名 / 删除 / 属性 ----------

void MainWindow::onRenameAction() { renameSelectedNode(); }
void MainWindow::onDeleteAction() { deleteSelectedNode(); }
void MainWindow::onPropertiesAction() { showPropertiesDialog(); }

void MainWindow::renameSelectedNode()
{
    if (isCurrentTabPdf()) {
        return;
    }
    const HFADMNode node = selectedNode();
    if (node.id == 0) {
        return;
    }
    if (isRemoteTab()) {
        TabData *tab = currentTabData();
        if (!tab || !tab->remoteClient) {
            return;
        }
        bool inputOk = false;
        const QString newName = QInputDialog::getText(
            this, tr("重命名"), tr("新名称："),
            QLineEdit::Normal, node.name, &inputOk);
        if (!inputOk || newName.trimmed().isEmpty() || newName.trimmed() == node.name) {
            return;
        }
        RemoteClient *client = tab->remoteClient;
        const qint64 reqId = client->renameNodeAsync(node.id, newName.trimmed());
        QPointer<RemoteClient> guard(client);
        awaitOnce(client, reqId, this, [this, guard](bool ok, const QJsonObject &, const QString &err) {
            if (!guard) {
                return;
            }
            TabData *cur = currentTabData();
            if (!cur || cur->remoteClient != guard) {
                return;
            }
            if (!ok) {
                showError(tr("重命名失败"), err);
                return;
            }
            loadCurrentDirectory();
        });
        return;
    }

    QString error;
    if (!renameNodeWithDialog(this, m_nodeService, node, &error)) {
        showError(tr("重命名失败"), error);
        return;
    }
    loadCurrentDirectory();
}

void MainWindow::deleteSelectedNode()
{
    if (isCurrentTabPdf()) {
        return;
    }
    QVector<HFADMNode> targets = selectedNodes();
    // 机型根节点不可删除（过滤）
    targets.erase(std::remove_if(targets.begin(), targets.end(),
                                 [](const HFADMNode &n) {
                                     return n.type == NodeType::Aircraft;
                                 }),
                  targets.end());
    if (targets.isEmpty()) {
        return;
    }

    if (isRemoteTab()) {
        TabData *tab = currentTabData();
        if (!tab || !tab->remoteClient) {
            return;
        }
        const QString message = targets.size() == 1
            ? tr("确定删除「%1」吗？将连同其全部子级、图纸一起删除，此操作不可恢复！")
                  .arg(targets.first().name)
            : tr("确定删除选中的 %1 项吗？将连同其全部子级、图纸一起删除，此操作不可恢复！")
                  .arg(targets.size());
        if (QMessageBox::warning(this, tr("确认删除"), message,
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        RemoteClient *client = tab->remoteClient;
        QPointer<RemoteClient> guard(client);
        const int total = targets.size();
        // 串行删除：前一个成功才发下一个，全部成功后刷新（shared_ptr 持有递归 lambda 所有权，异步安全）
        auto step = std::make_shared<std::function<void(int)>>();
        *step = [this, guard, client, targets, total, step](int idx) {
            if (!guard) {
                return;
            }
            if (idx >= total) {
                TabData *cur = currentTabData();
                if (cur && cur->remoteClient == guard) {
                    loadCurrentDirectory();
                    showStatus(total == 1
                        ? tr("已删除：%1").arg(targets.first().name)
                        : tr("已删除 %1 项").arg(total));
                }
                return;
            }
            const qint64 reqId = client->deleteNodeAsync(targets.at(idx).id);
            awaitOnce(client, reqId, this, [this, guard, idx, step](bool ok, const QJsonObject &, const QString &err) {
                if (!guard) {
                    return;
                }
                if (!ok) {
                    showError(tr("删除失败"), err);
                    return;
                }
                (*step)(idx + 1);
            });
        };
        (*step)(0);
        return;
    }

    // 1. 收集删除计划：统计待删节点/图纸/文件数（确认弹窗与进度展示共用）
    QVector<DeletionPlan> plans;
    plans.reserve(targets.size());
    int totalNodes = 0;
    int totalDrawings = 0;
    int totalFiles = 0;
    for (const HFADMNode &node : targets) {
        DeletionPlan plan;
        if (!m_nodeService->collectDeletionPlan(node.id, plan)) {
            showError(tr("删除失败"), m_nodeService->lastError());
            return;
        }
        totalNodes += plan.nodes.size();
        totalDrawings += plan.drawingCount();
        totalFiles += plan.fileCount();
        plans.append(plan);
    }

    // 2. 确认弹窗：提示删除范围与不可恢复（取消则无操作）
    const QString message = targets.size() == 1
        ? tr("确定删除「%1」吗？\n将删除其下全部 %2 个节点、%3 张图纸，此操作不可恢复！")
              .arg(targets.first().name).arg(totalNodes).arg(totalDrawings)
        : tr("确定删除选中的 %1 项吗？\n将删除其下全部 %2 个节点、%3 张图纸，此操作不可恢复！")
              .arg(targets.size()).arg(totalNodes).arg(totalDrawings);
    const auto answer = QMessageBox::warning(this, tr("确认删除"),
                                             message, QMessageBox::Yes | QMessageBox::No,
                                             QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    // 3. 模态进度弹窗：逐节点/逐文件删除并实时汇报，完成后可点击关闭结束删除
    DeleteProgressDialog progressDialog(this);
    progressDialog.setTotal(totalNodes, totalDrawings, totalFiles);
    progressDialog.show();
    QApplication::processEvents();

    for (int i = 0; i < targets.size(); ++i) {
        progressDialog.setCurrentText(tr("正在删除「%1」…").arg(targets[i].name));
        QApplication::processEvents();

        const DeletionPlan &plan = plans.at(i);
        int nodeIndex = 0; // plan.nodes 与删除回调一一对应（叶子优先）
        const bool ok = m_nodeService->deleteNode(
            targets[i].id,
            [&progressDialog, &plan, &nodeIndex](const HFADMNode &deleted) {
                progressDialog.advance(1);
                QString line = tr("已删除节点：%1").arg(deleted.name);
                const DeletionPlan::Node *item = nullptr;
                if (nodeIndex < plan.nodes.size()
                    && plan.nodes.at(nodeIndex).nodeId == deleted.id) {
                    item = &plan.nodes.at(nodeIndex);
                }
                if (item && !item->fullPartNo.isEmpty()) {
                    line += QStringLiteral(" (%1)").arg(item->fullPartNo);
                }
                progressDialog.addLog(line);
                if (item) {
                    for (const Drawing &drawing : item->drawings) {
                        progressDialog.addLog(tr("    已删除图纸：%1")
                                                  .arg(drawing.fileName));
                    }
                }
                ++nodeIndex;
                QApplication::processEvents();
            },
            [&progressDialog](const QString &filePath) {
                progressDialog.advance(1);
                progressDialog.addLog(tr("    已删除文件：%1")
                                          .arg(QFileInfo(filePath).fileName()));
                QApplication::processEvents();
            });
        if (!ok) {
            progressDialog.setFailed(m_nodeService->lastError());
            progressDialog.exec(); // 等待用户点击关闭结束
            return;
        }
    }

    progressDialog.finish();
    progressDialog.exec(); // 删除完成，等待用户点击关闭结束删除
    loadCurrentDirectory();
    showStatus(targets.size() == 1
        ? tr("已删除：%1").arg(targets.first().name)
        : tr("已删除 %1 项").arg(targets.size()));
}

void MainWindow::showPropertiesDialog()
{
    if (isCurrentTabPdf()) {
        return;
    }
    const HFADMNode node = selectedNode();
    if (node.id == 0) {
        return;
    }
    if (isRemoteTab()) {
        TabData *tab = currentTabData();
        if (!tab || !tab->remoteClient) {
            return;
        }
        RemoteClient *client = tab->remoteClient;
        QPointer<RemoteClient> guard(client);
        const bool isPart = node.type == NodeType::Part;
        const bool isComponent = node.type == NodeType::Component;
        const bool hasPartNo = node.type != NodeType::Aircraft;
        struct PropsState { Part part; Component component; QString fullPartNo; };
        auto st = std::make_shared<PropsState>();
        // 弹属性框 + 串行写
        std::function<void()> showAndApply = [this, guard, node, isPart, isComponent, hasPartNo, st]() {
            if (!guard) {
                return;
            }
            QString partNoPrefix;
            if (hasPartNo) {
                const QString full = st->fullPartNo;
                const QString partNo = node.partNo;
                if (!full.isEmpty() && !partNo.isEmpty() && full.endsWith(partNo)) {
                    partNoPrefix = full.left(full.size() - partNo.size());
                } else if (!full.isEmpty()) {
                    partNoPrefix = full + QStringLiteral(".");
                }
            }
            QString newName, newPartNo = node.partNo, newMaterial, newRemark = node.remark;
            int newQuantity = st->part.quantity;
            int newComponentQuantity = st->component.quantity;
            if (!showNodePropertiesDialog(this, node.name, nodeTypeDisplayName(node.type),
                                          node.createTime.toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                                          hasPartNo, partNoPrefix, node.partNo,
                                          isPart, st->part.material, st->part.quantity,
                                          isComponent, st->component.quantity, node.remark,
                                          newName, newPartNo, newMaterial, newQuantity,
                                          newComponentQuantity, newRemark)) {
                return; // 用户取消
            }
            // 串行写：rename → updatePartNo → updatePartAttributes → updateComponentQuantity
            //         → updateNodeRemark → 完成
            std::function<void()> finish = [this, guard]() {
                if (!guard) {
                    return;
                }
                loadCurrentDirectory();
                showStatus(tr("属性已保存"));
            };
            std::function<void()> doRemark = [this, guard, newRemark, node, finish]() {
                if (!guard) {
                    return;
                }
                if (newRemark != node.remark) {
                    const qint64 id = guard->updateNodeRemarkAsync(node.id, newRemark);
                    awaitOnce(guard, id, this, [this, guard, finish](bool ok, const QJsonObject &, const QString &err) {
                        if (!guard) {
                            return;
                        }
                        if (!ok) {
                            showError(tr("保存失败"), err);
                            return;
                        }
                        finish();
                    });
                } else {
                    finish();
                }
            };
            std::function<void()> doCompQty = [this, guard, isComponent, newComponentQuantity, st, node, doRemark]() {
                if (!guard) {
                    return;
                }
                if (isComponent && newComponentQuantity != st->component.quantity) {
                    const qint64 id = guard->updateComponentQuantityAsync(node.id, newComponentQuantity);
                    awaitOnce(guard, id, this, [this, guard, doRemark](bool ok, const QJsonObject &, const QString &err) {
                        if (!guard) {
                            return;
                        }
                        if (!ok) {
                            showError(tr("保存失败"), err);
                            return;
                        }
                        doRemark();
                    });
                } else {
                    doRemark();
                }
            };
            std::function<void()> doAttrs = [this, guard, isPart, newMaterial, newQuantity, st, node, doCompQty]() {
                if (!guard) {
                    return;
                }
                if (isPart && (newMaterial != st->part.material || newQuantity != st->part.quantity)) {
                    const qint64 id = guard->updatePartAttributesAsync(node.id, newMaterial, newQuantity);
                    awaitOnce(guard, id, this, [this, guard, doCompQty](bool ok, const QJsonObject &, const QString &err) {
                        if (!guard) {
                            return;
                        }
                        if (!ok) {
                            showError(tr("保存失败"), err);
                            return;
                        }
                        doCompQty();
                    });
                } else {
                    doCompQty();
                }
            };
            std::function<void()> doPartNo = [this, guard, hasPartNo, newPartNo, node, doAttrs]() {
                if (!guard) {
                    return;
                }
                if (hasPartNo && newPartNo != node.partNo) {
                    const qint64 id = guard->updatePartNoAsync(node.id, newPartNo);
                    awaitOnce(guard, id, this, [this, guard, doAttrs](bool ok, const QJsonObject &, const QString &err) {
                        if (!guard) {
                            return;
                        }
                        if (!ok) {
                            showError(tr("保存失败"), err);
                            return;
                        }
                        doAttrs();
                    });
                } else {
                    doAttrs();
                }
            };
            if (newName != node.name) {
                const qint64 id = guard->renameNodeAsync(node.id, newName);
                awaitOnce(guard, id, this, [this, guard, doPartNo](bool ok, const QJsonObject &, const QString &err) {
                    if (!guard) {
                        return;
                    }
                    if (!ok) {
                        showError(tr("保存失败"), err);
                        return;
                    }
                    doPartNo();
                });
            } else {
                doPartNo();
            }
        };
        // 加载链：loadPart → loadComponent → computeFullPartNo → showAndApply
        std::function<void()> loadFull = [this, guard, hasPartNo, node, st, showAndApply]() {
            if (!guard) {
                return;
            }
            if (hasPartNo) {
                const qint64 id = guard->computeFullPartNoAsync(node.id);
                awaitOnce(guard, id, this, [this, guard, st, showAndApply](bool ok, const QJsonObject &data, const QString &err) {
                    if (!guard) {
                        return;
                    }
                    if (!ok) {
                        showError(tr("加载失败"), err);
                        return;
                    }
                    st->fullPartNo = data.value(QStringLiteral("full")).toString();
                    showAndApply();
                });
            } else {
                showAndApply();
            }
        };
        std::function<void()> loadComp = [this, guard, isComponent, node, st, loadFull]() {
            if (!guard) {
                return;
            }
            if (isComponent) {
                const qint64 id = guard->loadComponentAsync(node.id);
                awaitOnce(guard, id, this, [this, guard, st, loadFull](bool ok, const QJsonObject &data, const QString &err) {
                    if (!guard) {
                        return;
                    }
                    if (!ok) {
                        showError(tr("加载失败"), err);
                        return;
                    }
                    st->component = RemoteProtocol::componentFromJson(
                        data.value(QStringLiteral("component")).toObject());
                    loadFull();
                });
            } else {
                loadFull();
            }
        };
        if (isPart) {
            const qint64 id = guard->loadPartAsync(node.id);
            awaitOnce(guard, id, this, [this, guard, st, loadComp](bool ok, const QJsonObject &data, const QString &err) {
                if (!guard) {
                    return;
                }
                if (!ok) {
                    showError(tr("加载失败"), err);
                    return;
                }
                st->part = RemoteProtocol::partFromJson(data.value(QStringLiteral("part")).toObject());
                loadComp();
            });
        } else {
            loadComp();
        }
        return;
    }

    QString error;
    if (!showNodePropertiesAndApply(this, m_nodeService, node, &error)) {
        showError(tr("保存失败"), error);
        return;
    }
    loadCurrentDirectory();
    showStatus(tr("属性已保存"));
}

// ---------- 剪贴板 ----------

void MainWindow::onCutAction()
{
    if (isCurrentTabPdf()) {
        return;
    }
    QVector<HFADMNode> nodes = selectedNodes();
    // 机型根节点不可剪切（过滤），其余全部进入剪贴板
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                               [](const HFADMNode &n) {
                                   return n.type == NodeType::Aircraft;
                               }),
                nodes.end());
    if (nodes.isEmpty()) {
        return;
    }
    m_clipboard.mode = NodeClipboard::Mode::Cut;
    m_clipboard.nodes = nodes;
    m_clipboardClient = isRemoteTab() ? currentRemoteClient() : nullptr;
    updateActionState();
    showStatus(tr("已剪切：%1，在目标目录执行粘贴").arg(m_clipboard.summary()));
}

void MainWindow::onCopyAction()
{
    if (isCurrentTabPdf()) {
        return;
    }
    QVector<HFADMNode> nodes = selectedNodes();
    if (nodes.isEmpty()) {
        return;
    }
    m_clipboard.mode = NodeClipboard::Mode::Copy;
    m_clipboard.nodes = nodes;
    m_clipboardClient = isRemoteTab() ? currentRemoteClient() : nullptr;
    updateActionState();
    showStatus(tr("已复制：%1，在目标目录执行粘贴").arg(m_clipboard.summary()));
}

void MainWindow::onPasteAction()
{
    TabData *tab = currentTabData();
    if (!tab || !m_clipboard.valid()) {
        return;
    }

    // 远程目录页：粘贴走协议（剪贴板必须来自同一远程连接）
    if (tab->type == TabManager::TabType::Remote) {
        if (m_clipboardClient != tab->remoteClient) {
            showStatus(tr("剪贴板来自其他来源，无法粘贴到此处"));
            return;
        }
        const qint64 targetNodeId = tab->currentNodeId;
        if (m_clipboard.mode == NodeClipboard::Mode::Cut) {
            // 剪切：异步串行 moveNode（shared_ptr<function> 递归持所有权）
            RemoteClient *client = tab->remoteClient;
            QPointer<RemoteClient> guard(client);
            const QVector<HFADMNode> nodes = m_clipboard.nodes;
            const QString summary = m_clipboard.summary();
            auto step = std::make_shared<std::function<void(int)>>();
            *step = [this, guard, client, nodes, targetNodeId, summary, step](int idx) {
                if (!guard) {
                    return;
                }
                if (idx >= nodes.size()) {
                    TabData *cur = currentTabData();
                    if (cur && cur->remoteClient == guard) {
                        loadCurrentDirectory();
                        showStatus(tr("已移动：%1").arg(summary));
                        updateActionState();
                    }
                    m_clipboard = NodeClipboard();
                    m_clipboardClient = nullptr;
                    return;
                }
                const qint64 id = client->moveNodeAsync(nodes.at(idx).id, targetNodeId);
                awaitOnce(client, id, this, [this, guard, idx, step](bool ok, const QJsonObject &, const QString &err) {
                    if (!guard) {
                        return;
                    }
                    if (!ok) {
                        showError(tr("粘贴失败"), err);
                        return;
                    }
                    (*step)(idx + 1);
                });
            };
            (*step)(0);
            return;
        }
        // 复制：冲突解析循环（多次 computeFullPartNo/isPartNoOccupied/copyNode）异步化复杂，暂走旧同步桥接
        QString error;
        if (!remotePasteClipboard(this, tab->remoteClient, m_clipboard, targetNodeId, &error)) {
            showError(tr("粘贴失败"), error);
            return;
        }
        showStatus(tr("已复制：%1").arg(m_clipboard.summary()));
        loadCurrentDirectory();
        updateActionState();
        return;
    }
    if (tab->type != TabManager::TabType::Directory) {
        return;
    }
    // 本地目录页：拒绝远程来源的剪贴板
    if (m_clipboardClient != nullptr) {
        showStatus(tr("剪贴板来自远程连接，无法粘贴到本地项目"));
        return;
    }

    QString error;
    if (!pasteNodeClipboard(this, m_clipboard, tab->currentNodeId, m_nodeService, &error)) {
        showError(tr("粘贴失败"), error);
        return;
    }

    if (m_clipboard.mode == NodeClipboard::Mode::Cut) {
        showStatus(tr("已移动：%1").arg(m_clipboard.summary()));
        m_clipboard = NodeClipboard();
    } else {
        showStatus(tr("已复制：%1").arg(m_clipboard.summary()));
    }
    loadCurrentDirectory();
    updateActionState();
}

// ---------- 图纸 ----------

void MainWindow::onImportPdfAction() { importPdfToSelectedPart(); }

void MainWindow::onOpenInNewTabAction()
{
    if (isCurrentTabPdf()) {
        return;
    }
    const HFADMNode node = selectedNode();
    if (node.id == 0) {
        return;
    }
    // 仅部件/机型支持在新标签页打开（零件走独立编辑窗口，不支持）
    if (node.type != NodeType::Aircraft && node.type != NodeType::Component) {
        return;
    }
    TabData *tab = currentTabData();
    if (!tab) {
        return;
    }
    if (tab->type == TabManager::TabType::Remote && tab->remoteClient) {
        // 远程：新标签页仍走同一连接
        m_tabManager->openRemoteTab(node.name, tab->projectName, tab->remoteClient, node.id);
        refreshTabColors();
        loadCurrentDirectory();
        showStatus(tr("已在新标签页打开：%1").arg(node.name));
        return;
    }
    if (m_activeProjectPath.isEmpty()) {
        return;
    }
    m_tabManager->openDirectoryTab(node.name, node.id, m_activeProjectPath,
                                   m_projectService->currentProjectInfo().name);
    refreshTabColors();
    loadCurrentDirectory();
    showStatus(tr("已在新标签页打开：%1").arg(node.name));
}
void MainWindow::onViewDrawingAction() { viewSelectedDrawing(); }
void MainWindow::onSetCurrentDrawingAction() { setCurrentSelectedDrawing(); }
void MainWindow::onDeleteDrawingAction() { deleteSelectedDrawing(); }

void MainWindow::importPdfToSelectedPart()
{
    TabData *tab = currentTabData();
    if (!tab) {
        return;
    }

    if (tab->type == TabManager::TabType::Remote && tab->remoteClient) {
        const QString filePath = QFileDialog::getOpenFileName(
            this, tr("选择 PDF 图纸"), QString(), tr("PDF 文件 (*.pdf)"));
        if (filePath.isEmpty()) {
            return;
        }
        RemoteClient *client = tab->remoteClient;
        const qint64 partNodeId = tab->currentNodeId;
        const qint64 reqId = client->importPdfAsync(partNodeId, filePath);
        QPointer<RemoteClient> guard(client);
        awaitOnce(client, reqId, this, [this, guard, partNodeId](bool ok, const QJsonObject &, const QString &err) {
            if (!guard) {
                return;
            }
            TabData *cur = currentTabData();
            if (!cur || cur->remoteClient != guard || cur->currentNodeId != partNodeId) {
                return;
            }
            if (!ok) {
                showError(tr("导入失败"), err);
                return;
            }
            loadCurrentDirectory();
            showStatus(tr("PDF 导入成功"));
        });
        return;
    }
    if (tab->type != TabManager::TabType::Directory) {
        return;
    }

    QString error;
    if (!importPdfForPart(this, m_drawingService, tab->currentNodeId, &error)) {
        if (!error.isEmpty()) {
            showError(tr("导入失败"), error);
        }
        return;
    }
    loadCurrentDirectory();
    showStatus(tr("PDF 导入成功"));
}

void MainWindow::viewSelectedDrawing()
{
    const Drawing drawing = selectedDrawing();
    if (drawing.id == 0) {
        return;
    }
    if (isRemoteTab()) {
        TabData *tab = currentTabData();
        if (!tab || !tab->remoteClient) {
            return;
        }
        RemoteClient *client = tab->remoteClient;
        const qint64 reqId = client->fetchDrawingFileAsync(drawing.id);
        QPointer<RemoteClient> guard(client);
        awaitOnce(client, reqId, this, [this, guard, drawing](bool ok, const QJsonObject &data, const QString &err) {
            if (!guard) {
                return;
            }
            if (!ok) {
                showError(tr("打开图纸失败"), err);
                return;
            }
            const QString tempPath = data.value(QStringLiteral("tempFilePath")).toString();
            if (tempPath.isEmpty()) {
                showError(tr("打开图纸失败"), tr("图纸数据为空"));
                return;
            }
            TabData *cur = currentTabData();
            if (!cur || cur->remoteClient != guard) {
                return;
            }
            bool pdfOk = false;
            m_tabManager->openPdfTab(drawing, tempPath, QString(), cur->projectName, &pdfOk, guard);
            if (!pdfOk) {
                showError(tr("打开图纸失败"),
                          tr("PDF 加载失败：%1").arg(drawing.fileName));
                return;
            }
            refreshTabColors();
            updateNavigationState();
            showStatus(tr("已打开远程图纸：%1").arg(drawing.fileName));
        });
        return;
    }
    openPdfTab(drawing);
}

void MainWindow::setCurrentSelectedDrawing()
{
    TabData *tab = currentTabData();
    if (!tab) {
        return;
    }
    const Drawing drawing = selectedDrawing();
    if (drawing.id == 0) {
        return;
    }

    if (tab->type == TabManager::TabType::Remote && tab->remoteClient) {
        RemoteClient *client = tab->remoteClient;
        const qint64 partNodeId = tab->currentNodeId;
        const qint64 reqId = client->setCurrentDrawingAsync(partNodeId, drawing.id);
        QPointer<RemoteClient> guard(client);
        awaitOnce(client, reqId, this, [this, guard, partNodeId, drawing](bool ok, const QJsonObject &, const QString &err) {
            if (!guard) {
                return;
            }
            TabData *cur = currentTabData();
            if (!cur || cur->remoteClient != guard || cur->currentNodeId != partNodeId) {
                return;
            }
            if (!ok) {
                showError(tr("操作失败"), err);
                return;
            }
            loadCurrentDirectory();
            showStatus(tr("已设为当前版本：%1").arg(drawing.fileName));
        });
        return;
    }
    if (tab->type != TabManager::TabType::Directory) {
        return;
    }

    QString error;
    if (!setCurrentVersionForPart(m_drawingService, tab->currentNodeId, drawing.id, &error)) {
        showError(tr("操作失败"), error);
        return;
    }
    loadCurrentDirectory();
    showStatus(tr("已设为当前版本：%1").arg(drawing.fileName));
}

void MainWindow::deleteSelectedDrawing()
{
    TabData *tab = currentTabData();
    if (!tab) {
        return;
    }
    const Drawing drawing = selectedDrawing();
    if (drawing.id == 0) {
        return;
    }

    if (tab->type == TabManager::TabType::Remote && tab->remoteClient) {
        const auto answer = QMessageBox::warning(
            this, tr("确认删除"),
            tr("确定删除图纸「%1」吗？文件将一并删除，此操作不可恢复。").arg(drawing.fileName),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
        RemoteClient *client = tab->remoteClient;
        const qint64 reqId = client->deleteDrawingAsync(drawing.id);
        QPointer<RemoteClient> guard(client);
        awaitOnce(client, reqId, this, [this, guard, drawing](bool ok, const QJsonObject &, const QString &err) {
            if (!guard) {
                return;
            }
            TabData *cur = currentTabData();
            if (!cur || cur->remoteClient != guard) {
                return;
            }
            if (!ok) {
                showError(tr("删除失败"), err);
                return;
            }
            loadCurrentDirectory();
            showStatus(tr("图纸已删除：%1").arg(drawing.fileName));
        });
        return;
    }
    if (tab->type != TabManager::TabType::Directory) {
        return;
    }

    QString error;
    if (!deleteDrawingWithConfirm(this, m_drawingService, drawing.id, drawing.fileName, &error)) {
        if (!error.isEmpty()) {
            showError(tr("删除失败"), error);
        }
        return;
    }
    loadCurrentDirectory();
    showStatus(tr("图纸已删除：%1").arg(drawing.fileName));
}

// ---------- 选中项 ----------

DirectoryItem MainWindow::selectedItem() const
{
    const QModelIndex index = ui->detailView->currentIndex();
    if (!index.isValid()) {
        return DirectoryItem();
    }
    TabData *tab = currentTabData();
    if (!tab) {
        return DirectoryItem();
    }
    return tab->model->itemAt(index.row());
}

HFADMNode MainWindow::selectedNode() const
{
    return selectedItem().node;
}

QVector<HFADMNode> MainWindow::selectedNodes() const
{
    QVector<HFADMNode> nodes;
    const QVector<DirectoryItem> items = selectedItems();
    for (const DirectoryItem &item : items) {
        if (item.kind == DirectoryItem::Kind::Node && item.node.id != 0) {
            nodes.append(item.node);
        }
    }
    return nodes;
}

QVector<DirectoryItem> MainWindow::selectedItems() const
{
    QVector<DirectoryItem> items;
    TabData *tab = currentTabData();
    if (!tab || !ui->detailView->selectionModel()) {
        return items;
    }
    const QModelIndexList rows = ui->detailView->selectionModel()->selectedRows();
    for (const QModelIndex &index : rows) {
        items.append(tab->model->itemAt(index.row()));
    }
    return items;
}

Drawing MainWindow::selectedDrawing() const
{
    return selectedItem().drawing;
}

// ---------- 浏览交互 ----------

bool MainWindow::activateProjectContext(const QString &projectPath)
{
    if (projectPath.isEmpty() || projectPath == m_activeProjectPath) {
        return true;
    }
    if (!m_projectService->openProject(projectPath)) {
        showError(tr("切换项目失败"), m_projectService->lastError());
        return false;
    }
    m_drawingService->setProjectPath(projectPath);
    m_nodeService->setProjectPath(projectPath);
    m_activeProjectPath = projectPath;
    registerProjectColor(projectPath);
    updateWindowTitle(); // 切到其他项目标签时标题跟随
    return true;
}

void MainWindow::registerProjectColor(const QString &projectPath)
{
    if (projectPath.isEmpty() || m_projectColors.contains(projectPath)) {
        return;
    }
    m_projectColors.insert(projectPath,
                           kProjectPalette[m_projectColors.size() % kProjectPalette.size()]);
}

void MainWindow::refreshTabColors()
{
    auto *bar = ui->tabWidget->browserTabBar();
    bar->clearProjectColors();
    for (int i = 0; i < m_tabManager->count(); ++i) {
        const TabData *tab = m_tabManager->tabAt(i);
        if (tab) {
            bar->setProjectColor(i, m_projectColors.value(tab->projectPath));
        }
    }
    bar->update();
}

void MainWindow::onTabChanged(int index)
{
    Q_UNUSED(index)
    if (m_restoringSession) {
        return; // 会话恢复期间逐个建标签，避免重复加载
    }
    TabData *tab = m_tabManager->currentTab();
    if (!tab) {
        refreshDetailView();
        return;
    }
    // 切到其他项目的标签时，先切换项目数据库上下文
    if (!activateProjectContext(tab->projectPath)) {
        return;
    }
    loadCurrentDirectory();
}

void MainWindow::onTabCloseRequested(int index)
{
    // 必须在 closeTab 前取出 projectPath / remoteClient（closeTab 会 delete tab）
    const TabData *closing = m_tabManager->tabAt(index);
    const QString closedProject = closing ? closing->projectPath : QString();
    RemoteClient *closedClient = closing ? closing->remoteClient : nullptr;
    m_tabManager->closeTab(index);

    // 关闭的标签若属于已开放的机型：自动停止远程开放（含断开全部连接）
    if (m_remoteServer && m_remoteServer->isRunning()
        && !closedProject.isEmpty()
        && closedProject == m_remoteServer->projectPath()) {
        m_remoteServer->stop();
        updateActionState();
        updateRemoteStatusBar();
        showStatus(tr("远程访问已关闭：所属机型标签已关闭"));
    }

    // 关闭远程标签：若这是该连接（即该服务端机型）的最后一个远程标签，
    // 释放客户端 → 析构断开 socket → 服务端 RemoteConnection.onDisconnected
    // → removeConnection → connectionCountChanged（连接数 -1、状态栏实时更新）
    if (closedClient) {
        bool stillUsed = false;
        for (int i = 0; i < m_tabManager->count(); ++i) {
            if (m_tabManager->tabAt(i)->remoteClient == closedClient) {
                stillUsed = true;
                break;
            }
        }
        if (!stillUsed) {
            if (m_clipboardClient == closedClient) {
                m_clipboardClient = nullptr;
            }
            closedClient->deleteLater();
        }
    }

    if (m_tabManager->isEmpty()) {
        closeProject();
        return;
    }
    m_tabManager->setCurrentIndex(qBound(0, index, m_tabManager->count() - 1));

    // 项目的最后一个标签被关闭后，释放其颜色
    if (!closedProject.isEmpty() && !m_tabManager->hasTabOfProject(closedProject)) {
        m_projectColors.remove(closedProject);
    }
    TabData *tab = m_tabManager->currentTab();
    if (tab && !activateProjectContext(tab->projectPath)) {
        return;
    }
    refreshTabColors();
    refreshDetailView();
}

void MainWindow::onCloseProject()
{
    if (m_tabManager->isEmpty()) {
        return;
    }
    const QString projectPath = m_activeProjectPath;
    if (projectPath.isEmpty()) {
        return;
    }

    // 关闭当前项目所有标签（从后往前避免索引错位）
    const QList<int> indices = m_tabManager->projectTabIndices(projectPath);
    for (int i = indices.size() - 1; i >= 0; --i) {
        m_tabManager->closeTab(indices.at(i));
    }
    m_projectColors.remove(projectPath);

    if (m_tabManager->isEmpty()) {
        closeProject();
        return;
    }
    TabData *tab = m_tabManager->currentTab();
    if (tab && !activateProjectContext(tab->projectPath)) {
        return;
    }
    refreshTabColors();
    refreshDetailView();
    showStatus(tr("已关闭项目"));
}

void MainWindow::onTableDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid() || isCurrentTabPdf()) {
        return;
    }
    TabData *tab = currentTabData();
    if (!tab) {
        return;
    }

    const DirectoryItem item = tab->model->itemAt(index.row());
    if (item.kind == DirectoryItem::Kind::Drawing) {
        if (isRemoteTab()) {
            viewSelectedDrawing(); // 远程：拉取文件到临时目录后打开
            return;
        }
        openPdfTab(item.drawing);
    } else if (item.node.type == NodeType::Part) {
        if (isRemoteTab()) {
            // 远程：完整零件编辑（属性 + 图纸列表/预览/导出/删除/导入/设当前，全部走协议）
            QString error;
            if (!showRemotePartEditorDialog(this, tab->remoteClient, item.node, &error)) {
                if (!error.isEmpty()) {
                    showError(tr("零件编辑失败"), error);
                }
                return;
            }
            loadCurrentDirectory();
            updateActionState();
            return;
        }
        // 零件节点双击：打开独立编辑窗口（属性 + 图纸导入/更新）
        QString error;
        if (showPartEditorDialog(this, m_nodeService, m_drawingService,
                                 item.node.id, &error)) {
            loadCurrentDirectory();
            updateActionState();
        }
    } else if (item.node.type == NodeType::Aircraft
               || item.node.type == NodeType::Component) {
        // 搜索结果中打开部件目录：先清空搜索（资源管理器式：回到目录模式）再进入
        if (ui->locationBar->isSearching()) {
            ui->locationBar->clearSearch();
        }
        navigateTo(item.node.id);
    }
}

void MainWindow::onSearchTextChanged(const QString &text)
{
    Q_UNUSED(text)
    loadCurrentDirectory();
}

void MainWindow::onTableContextMenuRequested(const QPoint &pos)
{
    const QModelIndex index = ui->detailView->indexAt(pos);
    if (index.isValid() && ui->detailView->selectionModel()) {
        auto *selModel = ui->detailView->selectionModel();
        if (selModel->isSelected(index)) {
            // 右键已选中的项：仅移动焦点，保留多选（对齐资源管理器）
            selModel->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        } else {
            // 右键未选中的项：改为单选该项（setCurrentIndex 会清除旧选中）
            ui->detailView->setCurrentIndex(index);
        }
    }
    TabData *tab = currentTabData();
    // PDF 页无右键菜单；远程目录页不依赖本地项目打开即可操作，
    // 本地目录页需要本地项目已打开
    if (!tab || tab->type == TabManager::TabType::Pdf) {
        return;
    }
    if (tab->type != TabManager::TabType::Remote && !m_projectOpen) {
        return;
    }

    updateActionState();

    QMenu *menu = nullptr;
    const DirectoryItem item = selectedItem();
    if (item.kind == DirectoryItem::Kind::Drawing) {
        menu = m_ctxMenus.drawingMenu;
    } else {
        menu = m_ctxMenus.nodeMenu;
    }
    menu->exec(ui->detailView->viewport()->mapToGlobal(pos));
}

void MainWindow::onPartNoClicked(const QModelIndex &index)
{
    if (index.column() != NodeTableModel::ColPartNo) {
        return;
    }
    TabData *tab = currentTabData();
    if (!tab || !tab->model) {
        return;
    }
    const DirectoryItem item = tab->model->itemAt(index.row());
    if (item.kind != DirectoryItem::Kind::Node || item.node.type != NodeType::Part) {
        return;
    }
    openLatestDrawingForPart(item.node);
}

void MainWindow::openLatestDrawingForPart(const HFADMNode &node)
{
    if (!m_pdfCacheDir || !m_pdfCacheDir->isValid()) {
        showError(tr("打开图纸失败"), tr("无法创建缓存目录"));
        return;
    }
    if (isRemoteTab()) {
        TabData *tab = currentTabData();
        if (!tab || !tab->remoteClient) {
            return;
        }
        RemoteClient *client = tab->remoteClient;
        QPointer<RemoteClient> guard(client);
        // 先 listDirAsync 拿图纸列表，找 isCurrent（否则取最新一个）
        const qint64 listReqId = client->listDirAsync(node.id);
        awaitOnce(client, listReqId, this, [this, guard](bool ok, const QJsonObject &data, const QString &err) {
            if (!guard) {
                return;
            }
            if (!ok) {
                showError(tr("打开图纸失败"), err);
                return;
            }
            qint64 drawingId = 0;
            QString fileName;
            qint64 fallbackId = 0;
            QString fallbackName;
            const QJsonArray arr = data.value(QStringLiteral("items")).toArray();
            for (const QJsonValue &v : arr) {
                const DirectoryItem it = RemoteProtocol::directoryItemFromJson(v.toObject());
                if (it.kind == DirectoryItem::Kind::Drawing) {
                    fallbackId = it.drawing.id;
                    fallbackName = it.drawing.fileName;
                    if (it.drawing.isCurrent) {
                        drawingId = it.drawing.id;
                        fileName = it.drawing.fileName;
                        break;
                    }
                }
            }
            if (drawingId == 0) {
                drawingId = fallbackId;
                fileName = fallbackName;
            }
            if (drawingId == 0) {
                showStatus(tr("该零件暂无图纸"));
                return;
            }
            const qint64 fetchReqId = guard->fetchDrawingFileAsync(drawingId);
            awaitOnce(guard, fetchReqId, this, [this, guard, fileName](bool ok2, const QJsonObject &d2, const QString &err2) {
                if (!guard) {
                    return;
                }
                if (!ok2) {
                    showError(tr("打开图纸失败"), err2);
                    return;
                }
                const QString tempPath = d2.value(QStringLiteral("tempFilePath")).toString();
                if (tempPath.isEmpty()) {
                    showError(tr("打开图纸失败"), tr("图纸数据为空"));
                    return;
                }
                openCachedPdf(tempPath, fileName);
            });
        });
        return;
    }
    // 本地：查图纸列表找最新
    QVector<Drawing> drawings;
    if (!m_drawingService->queryDrawings(node.id, drawings) || drawings.isEmpty()) {
        showStatus(tr("该零件暂无图纸"));
        return;
    }
    const Drawing *latest = nullptr;
    for (const Drawing &d : drawings) {
        if (d.isCurrent) {
            latest = &d;
            break;
        }
    }
    if (!latest) {
        latest = &drawings.last();
    }
    const QString src = DrawingService::resolveDrawingPath(m_activeProjectPath, latest->filePath);
    openCachedPdf(src, latest->fileName);
}

void MainWindow::openCachedPdf(const QString &srcPath, const QString &fileName)
{
    const QString cachePath = m_pdfCacheDir->filePath(fileName);
    if (QFile::exists(cachePath)) {
        QFile::remove(cachePath);
    }
    if (!QFile::copy(srcPath, cachePath)) {
        showError(tr("打开图纸失败"), tr("无法缓存图纸文件"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(cachePath));
    showStatus(tr("已用默认程序打开：%1").arg(fileName));
}

void MainWindow::onSelectionChanged()
{
    updateActionState();
}

// ---------- 其他 ----------

void MainWindow::onAbout()
{
    showAboutDialog(this);
}

// ---------- 远程访问 ----------

bool MainWindow::isRemoteTab() const
{
    const TabData *tab = currentTabData();
    return tab && tab->remoteClient != nullptr;
}

RemoteClient *MainWindow::currentRemoteClient() const
{
    const TabData *tab = currentTabData();
    return tab ? tab->remoteClient : nullptr;
}

void MainWindow::updateRemoteStatusBar()
{
    if (!m_remoteStatusLabel) {
        return;
    }
    if (m_remoteServer && m_remoteServer->isRunning()) {
        const QStringList addrs = RemoteServer::localAddresses();
        const QString ipText = addrs.isEmpty()
            ? tr("无局域网IP")
            : addrs.join(QStringLiteral("/"));
        m_remoteStatusLabel->setText(
            tr("远程访问已开启：%1:%2 · %3 连接")
                .arg(ipText)
                .arg(RemoteProtocol::kPort)
                .arg(m_remoteServer->connectionCount()));
        m_remoteStatusLabel->show();
    } else {
        m_remoteStatusLabel->clear();
        m_remoteStatusLabel->hide();
    }
}

void MainWindow::onOpenRemoteAccess()
{
    if (!m_projectOpen || m_activeProjectPath.isEmpty()) {
        return;
    }
    if (m_remoteServer->isRunning()) {
        if (m_remoteServer->projectPath() == m_activeProjectPath) {
            return; // 当前机型已开放（按钮已置灰，双保险）
        }
        QMessageBox::information(
            this, tr("远程访问"),
            tr("已开放「%1」的远程访问。请先执行「关闭所有连接」，再开放其他机型。")
                .arg(m_remoteServer->projectName()));
        return;
    }

    QString error;
    if (!m_remoteServer->start(m_activeProjectPath,
                               m_projectService->currentProjectInfo().name, &error)) {
        showError(tr("开放远程访问失败"), error);
        return;
    }
    updateActionState();
    updateRemoteStatusBar();
    const QStringList addrs = RemoteServer::localAddresses();
    showStatus(tr("已开放远程访问：%1:%2")
                   .arg(addrs.isEmpty() ? tr("本机") : addrs.join(QStringLiteral("/")))
                   .arg(RemoteProtocol::kPort));
}

void MainWindow::onCloseRemoteAccess()
{
    if (!m_remoteServer || !m_remoteServer->isRunning()) {
        return;
    }
    m_remoteServer->stop();
    updateActionState();
    updateRemoteStatusBar();
    showStatus(tr("远程访问已关闭，所有连接已断开"));
}

void MainWindow::onManageDevices()
{
    if (!m_remoteServer) {
        return;
    }
    // 授权管理使用全局设备存储，与当前是否开放机型无关
    DeviceManagerDialog dlg(m_remoteServer, this);
    dlg.exec();
}

void MainWindow::onConnectRemote()
{
    // 对话框内完成：地址输入/校验、连接中可中止、失败可重试；成功返回已连接的客户端
    RemoteClient *client = nullptr;
    if (!showRemoteConnectDialog(this, &client) || !client) {
        return;
    }
    client->setParent(this); // 由主窗口接管生命周期（断线/窗口关闭时释放）

    // 口令窗口已在连接对话框流程中（配对开始时）弹出，此处无需再弹

    const QString projectName = client->projectName();
    m_tabManager->openRemoteTab(tr("远程：%1").arg(projectName),
                                projectName, client, client->rootNodeId());
    refreshTabColors();
    loadCurrentDirectory();
    hideWelcomePage();
    showStatus(tr("已连接到远程：%1（%2）").arg(projectName, client->peerAddress()));

    // 断线：关闭该连接的全部标签并释放客户端
    connect(client, &RemoteClient::connectionLost, this,
            [this, client](const QString &reason) {
        for (int i = m_tabManager->count() - 1; i >= 0; --i) {
            if (m_tabManager->tabAt(i)->remoteClient == client) {
                m_tabManager->closeTab(i);
            }
        }
        if (m_clipboardClient == client) {
            m_clipboardClient = nullptr;
        }
        client->deleteLater();
        if (m_tabManager->isEmpty()) {
            showWelcomePage(); // 无任何标签可看：回到欢迎页
        }
        refreshDetailView(); // 内部同步导航与动作状态
        showError(tr("远程连接已断开"), reason);
    });
}

// ---------- 最近打开 ----------

void MainWindow::addRecentProject(const QString &projectPath)
{
    QSettings settings;
    QStringList recents = settings.value(kRecentProjectsKey).toStringList();
    recents.removeAll(projectPath);
    recents.prepend(projectPath);
    while (recents.size() > kMaxRecentProjects) {
        recents.removeLast();
    }
    settings.setValue(kRecentProjectsKey, recents);
    rebuildRecentProjectsMenu();
}

void MainWindow::rebuildRecentProjectsMenu()
{
    QSettings settings;
    const QStringList recents = settings.value(kRecentProjectsKey).toStringList();
    m_recentMenu->rebuild(recents);
}

// ---------- 工具 ----------

void MainWindow::showStatus(const QString &message, int timeout)
{
    m_statusBar->showMessage(message, timeout);
}

void MainWindow::showError(const QString &title, const QString &message)
{
    QMessageBox::warning(this, title, message);
    qWarning() << "MainWindow:" << title << message;
}
