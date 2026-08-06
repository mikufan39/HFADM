#include "welcomepage.h"

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
    auto *titleLabel = new QLabel(QStringLiteral("欢迎使用艾锐奥智能图纸管理系统"), this);
    QFont titleFont(QStringLiteral("仿宋"), 34);
    titleFont.setBold(false);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet(QStringLiteral("color:#39c5bb;"));
    titleLabel->setAlignment(Qt::AlignHCenter);
    root->addWidget(titleLabel);
    root->addStretch(1);

    // 三个按钮横向居中排列，间隔 64px
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->setSpacing(kButtonSpacing);

    QToolButton *createButton = makeActionButton(
        QStringLiteral(":/assets/welcome/welcome-page-create-project-icon.svg"),
        QStringLiteral("创建项目"));
    QToolButton *openButton = makeActionButton(
        QStringLiteral(":/assets/welcome/welcome-page-open-project-icon.svg"),
        QStringLiteral("打开项目"));
    QToolButton *remoteButton = makeActionButton(
        QStringLiteral(":/assets/welcome/welcome-page-connect_remote_icon.svg"),
        QStringLiteral("连接到远程"));

    buttonRow->addWidget(createButton);
    buttonRow->addWidget(openButton);
    buttonRow->addWidget(remoteButton);
    buttonRow->addStretch(1);
    root->addLayout(buttonRow);

    root->addStretch(1);

    connect(createButton, &QToolButton::clicked,
            this, &WelcomePage::createProjectRequested);
    connect(openButton, &QToolButton::clicked,
            this, &WelcomePage::openProjectRequested);
    connect(remoteButton, &QToolButton::clicked,
            this, &WelcomePage::connectRemoteRequested);
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
