#ifndef PINPROMPTDIALOG_H
#define PINPROMPTDIALOG_H

#include <QDialog>
#include <QString>

class QLabel;
class QTimer;

// 配对口令展示窗口（客户端，TCP 连通后与服务端确认弹窗并行显示）：
//   第一行「你的口令，剩余有效时间N秒」每秒自动刷新；
//   第二行大号 #39c5bb 显示 4 位口令，数字下带红色下划线；
//   底部「取消」按钮；倒计时结束自动取消（口令过期）。
// 客户端点击取消或口令过期时发出 cancelRequested()，由连接流程通知服务端
// 关闭其挂起的确认弹窗，双方均不记录任何配对结果。
// 以 WindowModal 顶层窗口非阻塞显示：可交互且不被连接对话框的模态循环禁用。
class PinPromptDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PinPromptDialog(const QString &pin, QWidget *parent = nullptr);

signals:
    // 用户点击「取消」或倒计时结束（口令过期）
    void cancelRequested();

private:
    QLabel *m_remainLabel = nullptr;
    QTimer *m_timer = nullptr;
    int m_remaining = 60;
};

#endif // PINPROMPTDIALOG_H
