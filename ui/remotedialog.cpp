#include "remotedialog.h"

#include "service/remoteclient.h"
#include "ui/pinpromptdialog.h"

#include <QCloseEvent>
#include <QDialog>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// 严格校验标准 IPv4 格式：四段、每段 0~255、无前导零、无空白
bool isIPv4Address(const QString &text)
{
    const QStringList parts = text.trimmed().split(QLatin1Char('.'));
    if (parts.size() != 4) {
        return false;
    }
    for (const QString &p : parts) {
        if (p.isEmpty()) {
            return false;
        }
        bool ok = false;
        const int v = p.toInt(&ok);
        if (!ok || v < 0 || v > 255) {
            return false;
        }
        if (p.size() > 1 && p.startsWith(QLatin1Char('0'))) {
            return false; // 拒绝 01 之类前导零写法
        }
    }
    return true;
}

// 连接交互对话框：输入地址 → 连接中（可中止）→ 错误（可重试）→ 成功
class RemoteConnectDialog : public QDialog
{
public:
    explicit RemoteConnectDialog(QWidget *parent = nullptr);

    // 连接成功后返回已连接的客户端（所有权已转移，不再由本对话框管理）
    RemoteClient *connectedClient() const { return m_connectedClient; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void onConnectClicked();
    // 阻塞连接：期间对话框事件循环嵌套运行，用户可点「取消」中止
    void startConnecting(const QString &address);

    QLabel *m_hint = nullptr;
    QLineEdit *m_edit = nullptr;
    QPushButton *m_connectBtn = nullptr;
    RemoteClient *m_client = nullptr;        // 连接尝试用客户端（成功前归本对话框管理）
    RemoteClient *m_connectedClient = nullptr; // 连接成功后转移给调用方
    PinPromptDialog *m_pinDialog = nullptr;  // 客户端口令窗口（parent=本对话框，连接结束后关闭）
    bool m_connecting = false;
};

RemoteConnectDialog::RemoteConnectDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("连接到远程"));
    setMinimumWidth(440);

    auto *layout = new QVBoxLayout(this);
    m_hint = new QLabel(QStringLiteral("请输入目标地址"), this);
    m_hint->setWordWrap(true);
    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(QStringLiteral("例如 192.168.1.100"));
    m_edit->setClearButtonEnabled(true);
    m_connectBtn = new QPushButton(QStringLiteral("连接"), this);
    m_connectBtn->setDefault(true);

    layout->addWidget(m_hint);
    layout->addWidget(m_edit);
    layout->addWidget(m_connectBtn);

    connect(m_connectBtn, &QPushButton::clicked, this, &RemoteConnectDialog::onConnectClicked);
}

void RemoteConnectDialog::closeEvent(QCloseEvent *event)
{
    // 连接进行中禁止用窗口关闭按钮中止，统一走「取消」按钮
    if (m_connecting) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void RemoteConnectDialog::onConnectClicked()
{
    if (!m_connecting) {
        const QString text = m_edit->text().trimmed();
        if (!isIPv4Address(text)) {
            // 输入不合法：清空输入框，占位提示变为「输入内容错误」
            m_edit->clear();
            m_edit->setPlaceholderText(QStringLiteral("输入内容错误"));
            return;
        }
        m_edit->setPlaceholderText(QStringLiteral("例如 192.168.1.100"));
        startConnecting(text);
    } else {
        if (m_client) {
            m_client->abortConnecting();
        }
    }
}

void RemoteConnectDialog::startConnecting(const QString &address)
{
    m_connecting = true;
    m_hint->setText(QStringLiteral("正在尝试连接"));
    m_connectBtn->setText(QStringLiteral("取消"));

    if (!m_client) {
        m_client = new RemoteClient(this);
        // 配对顺序（客户端与服务端并行展示口令）：
        //   TCP 连通后客户端立即弹出口令窗口（非模态，不阻塞），
        //   同时发送配对请求 → 服务端弹窗 → 双方用户可同时看到口令。
        // 客户端口令窗口点「取消」/口令过期 → 通知服务端关闭其弹窗并中止连接。
        connect(m_client, &RemoteClient::pairingStarted, this,
                [this](const QString &pin, const QString &) {
            if (m_pinDialog) {
                m_pinDialog->close();
                delete m_pinDialog;
                m_pinDialog = nullptr;
            }
            m_pinDialog = new PinPromptDialog(pin, this);
            // 队列连接：取消动作会在 cancelPairing 内同步走到连接流程收尾，
            // 避免在口令窗口按钮槽的栈上执行删除/关闭逻辑
            connect(m_pinDialog, &PinPromptDialog::cancelRequested, this,
                    [this] {
                if (m_client) {
                    m_client->cancelPairing();
                }
            }, Qt::QueuedConnection);
            m_pinDialog->show();
        });
    }

    QString error;
    const bool ok = m_client->connectTo(address, &error);

    // 连接流程结束（成功/失败/中止）：口令窗口使命完成，关闭（对象由父对话框管理）
    if (m_pinDialog) {
        m_pinDialog->close();
    }
    m_connecting = false;

    if (m_client->connectWasCancelled()) {
        // 用户中止：回到初始状态，可重新输入/连接
        m_hint->setText(QStringLiteral("请输入目标地址"));
        m_connectBtn->setText(QStringLiteral("连接"));
        return;
    }

    if (ok) {
        // 连接成功：客户端所有权转移给调用方，关闭弹窗
        m_connectedClient = m_client;
        m_connectedClient->setParent(nullptr);
        m_client = nullptr;
        accept();
        return;
    }

    // 连接失败：弹窗保持，提示「连接错误」，输入内容保留供修改，按钮变回「连接」
    m_hint->setText(QStringLiteral("连接错误"));
    m_connectBtn->setText(QStringLiteral("连接"));
}

} // namespace

bool showRemoteConnectDialog(QWidget *parent, RemoteClient **client)
{
    if (client) {
        *client = nullptr;
    }
    RemoteConnectDialog dlg(parent);
    if (dlg.exec() == QDialog::Accepted && dlg.connectedClient()) {
        if (client) {
            *client = dlg.connectedClient();
        }
        return true;
    }
    return false;
}
