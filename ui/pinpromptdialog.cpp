#include "pinpromptdialog.h"

#include "ui/pinlabel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// 口令有效时间（秒），与连接握手超时 kConnectTimeoutMs 一致
constexpr int kPinValidSeconds = 60;

} // namespace

PinPromptDialog::PinPromptDialog(const QString &pin, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("配对口令"));
    setMinimumWidth(380);
    // 顶层 WindowModal：在连接对话框（ApplicationModal）期间仍可显示与交互
    setWindowModality(Qt::WindowModal);

    auto *layout = new QVBoxLayout(this);

    m_remaining = kPinValidSeconds;
    m_remainLabel = new QLabel(
        tr("你的口令，剩余有效时间%1秒").arg(m_remaining), this);
    m_remainLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_remainLabel);

    auto *pinLabel = new PinLabel(pin, this);
    pinLabel->setFixedHeight(84);
    layout->addWidget(pinLabel);

    // 底部「取消」按钮：取消本次配对，通知服务端关闭其确认弹窗
    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch(1);
    auto *cancelBtn = new QPushButton(tr("取消"), this);
    cancelBtn->setDefault(true);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);
    connect(cancelBtn, &QPushButton::clicked, this, [this] {
        emit cancelRequested();
        reject(); // 关闭口令窗口
    });

    // 每秒刷新剩余时间；倒计时结束自动取消（口令过期）
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, [this] {
        --m_remaining;
        m_remainLabel->setText(tr("你的口令，剩余有效时间%1秒").arg(m_remaining));
        if (m_remaining <= 0) {
            m_timer->stop();
            emit cancelRequested();
            reject();
        }
    });
    m_timer->start();
}
