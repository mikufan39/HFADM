#include "pairingdialog.h"

#include "service/remoteserver.h"
#include "ui/pinlabel.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// 拒绝按钮倒计时（秒）：点击或倒计时结束均触发「拒绝且不再提示」
constexpr int kRejectCountdownSeconds = 60;

} // namespace

RemoteProtocol::PairingResult showPairingDialog(QWidget *parent,
                                                const RemoteProtocol::PairingRequest &req,
                                                RemoteServer *server)
{
    auto *dlg = new QDialog(parent);
    dlg->setWindowTitle(QStringLiteral("新客户端请求连接"));
    dlg->setMinimumWidth(420);

    auto *layout = new QVBoxLayout(dlg);

    // 第一行：待批准提示
    auto *title = new QLabel(QStringLiteral("新的连接请求待批准"), dlg);
    QFont tf = title->font();
    tf.setPointSize(12);
    tf.setBold(true);
    title->setFont(tf);
    layout->addWidget(title);

    // 来源信息（辅助辨识请求方）
    auto *fromLabel = new QLabel(
        QStringLiteral("来源：%1（%2）")
            .arg(req.deviceName.isEmpty() ? QStringLiteral("(未知设备)") : req.deviceName,
                 req.peerAddress),
        dlg);
    fromLabel->setWordWrap(true);
    layout->addWidget(fromLabel);

    // 大号口令：#39c5bb + 红色下划线（共享自绘组件）
    auto *pinLabel = new PinLabel(req.pin, dlg);
    pinLabel->setFixedHeight(84);
    layout->addWidget(pinLabel);

    // 右下角两个按钮
    int remaining = kRejectCountdownSeconds;
    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch(1);
    auto *allowBtn = new QPushButton(QStringLiteral("允许受限的访问请求"), dlg);
    auto *rejectBtn = new QPushButton(
        QStringLiteral("拒绝连接且不再提示（%1）").arg(remaining), dlg);
    allowBtn->setDefault(true);
    btnLayout->addWidget(allowBtn);
    btnLayout->addWidget(rejectBtn);
    layout->addLayout(btnLayout);

    // 拒绝是否由「拒绝且不再提示」按钮/倒计时触发（窗口 X 关闭不算拉黑）
    bool rejectWithNeverAskAgain = false;

    // 拒绝按钮倒计时：每秒刷新；归零自动执行「拒绝且不再提示」
    auto *timer = new QTimer(dlg);
    timer->setInterval(1000);
    QObject::connect(timer, &QTimer::timeout, dlg, [dlg, rejectBtn, timer, &remaining,
                                                    &rejectWithNeverAskAgain] {
        --remaining;
        rejectBtn->setText(QStringLiteral("拒绝连接且不再提示（%1）").arg(remaining));
        if (remaining <= 0) {
            timer->stop();
            rejectWithNeverAskAgain = true;
            dlg->reject();
        }
    });
    timer->start();

    QObject::connect(allowBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    QObject::connect(rejectBtn, &QPushButton::clicked, dlg, [dlg, &rejectWithNeverAskAgain] {
        rejectWithNeverAskAgain = true;
        dlg->reject();
    });

    // 注册为当前挂起弹窗：客户端「取消配对」时可被外部关闭（视为拒绝且不记录）
    if (server) {
        server->setActivePairingDialog(dlg);
    }
    const bool accepted = (dlg->exec() == QDialog::Accepted);
    timer->stop(); // 弹窗已关闭：停止倒计时，避免 lambda 访问已失效的栈引用
    if (server) {
        server->setActivePairingDialog(nullptr);
    }

    RemoteProtocol::PairingResult result;
    result.accepted = accepted;
    // 受限访问：仅授权只读，后续可在设备管理中将权限提升为可写
    result.permission = RemoteProtocol::Permission::ReadOnly;
    result.neverAskAgain = !accepted && rejectWithNeverAskAgain;
    dlg->deleteLater();
    return result;
}
