#include "welcomepage.h"

#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kButtonSize = 192;
constexpr int kButtonSpacing = 64;
constexpr int kIconSize = 100;

// 半透明圆角模拟磨砂（Qt Widgets 无原生背景模糊）
const char *kButtonStyleSheet = R"(
QToolButton#welcomeActionButton {
    background: rgba(255, 255, 255, 0.14);
    border: 1px solid rgba(255, 255, 255, 0.28);
    border-radius: 18px;
    color: #ffffff;
}
QToolButton#welcomeActionButton:hover {
    background: rgba(255, 255, 255, 0.24);
    border-color: rgba(255, 255, 255, 0.50);
}
QToolButton#welcomeActionButton:pressed {
    background: rgba(255, 255, 255, 0.32);
}
)";
}

WelcomePage::WelcomePage(QWidget *parent)
    : QWidget(parent)
    , m_background(QStringLiteral(":/assets/welcome/welcome-page-background.jpg"))
{
    setObjectName(QStringLiteral("welcomePage"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addStretch(1);

    // 中上方欢迎标题：#39c5bb 仿宋（位于页面顶部与按钮行之间居中）
    m_titleLabel = new QLabel(this);
    QFont titleFont(QStringLiteral("仿宋"), 34); // 字体名不参与翻译
    titleFont.setBold(false);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setStyleSheet(QStringLiteral("color:#39c5bb;"));
    m_titleLabel->setAlignment(Qt::AlignHCenter);
    root->addWidget(m_titleLabel);
    root->addStretch(1);

    // 三个按钮横向居中排列，间隔 64px
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->setSpacing(kButtonSpacing);

    m_createButton = makeActionButton(
        QStringLiteral(":/assets/welcome/welcome-page-create-project-icon.svg"), QString());
    m_openButton = makeActionButton(
        QStringLiteral(":/assets/welcome/welcome-page-open-project-icon.svg"), QString());
    m_remoteButton = makeActionButton(
        QStringLiteral(":/assets/welcome/welcome-page-connect_remote_icon.svg"), QString());

    buttonRow->addWidget(m_createButton);
    buttonRow->addWidget(m_openButton);
    buttonRow->addWidget(m_remoteButton);
    buttonRow->addStretch(1);
    root->addLayout(buttonRow);

    root->addStretch(1);

    // 标题与按钮文本统一由 applyTexts() 设置（语言切换时重调）
    applyTexts();

    connect(m_createButton, &QToolButton::clicked,
            this, &WelcomePage::createProjectRequested);
    connect(m_openButton, &QToolButton::clicked,
            this, &WelcomePage::openProjectRequested);
    connect(m_remoteButton, &QToolButton::clicked,
            this, &WelcomePage::connectRemoteRequested);
}

void WelcomePage::applyTexts()
{
    m_titleLabel->setText(tr("欢迎使用艾锐奥智能图纸管理系统"));
    m_createButton->setText(tr("创建项目"));
    m_openButton->setText(tr("打开项目"));
    m_remoteButton->setText(tr("连接到远程"));
}

void WelcomePage::changeEvent(QEvent *event)
{
    // 语言切换：QApplication::installTranslator 后自动收到 LanguageChange，重设欢迎页文本
    if (event->type() == QEvent::LanguageChange) {
        applyTexts();
    }
    QWidget::changeEvent(event);
}

QToolButton *WelcomePage::makeActionButton(const QString &iconPath, const QString &text)
{
    auto *button = new QToolButton(this);
    button->setObjectName(QStringLiteral("welcomeActionButton"));
    button->setFixedSize(kButtonSize, kButtonSize);
    button->setIcon(QIcon(iconPath));
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setText(text);
    // 图标在上、文字在下
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    // Windows 默认字体：不修改字体族，仅放大字号保证可读性
    QFont font = button->font();
    font.setPointSize(12);
    button->setFont(font);
    button->setStyleSheet(QString::fromLatin1(kButtonStyleSheet));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

void WelcomePage::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    if (m_background.isNull()) {
        return;
    }
    QPainter painter(this);
    // cover 效果：等比放大铺满并居中裁剪
    const QPixmap scaled = m_background.scaled(size(), Qt::KeepAspectRatioByExpanding,
                                               Qt::SmoothTransformation);
    const QPoint offset((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
    painter.drawPixmap(offset, scaled);
}
