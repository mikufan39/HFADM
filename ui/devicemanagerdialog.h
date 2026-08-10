#ifndef DEVICEMANAGERDIALOG_H
#define DEVICEMANAGERDIALOG_H

#include <QDialog>
#include <QString>
#include <QVector>

class RemoteServer;
class QTableWidget;
class QPushButton;

// 授权管理对话框（服务端）：
// 列出全部远程设备（全局授权存储，与当前开放机型无关；已授权设备 + 被禁止连接的设备），
// 权限列提供下拉选择（受限的访问权限 / 完全访问权限 / 不允许的连接），
// 更改在下一次客户端连接认证时生效（不影响当前在线会话）；
// 顶部提供 刷新 / 重命名 / 删除设备 三个操作。
// 模态执行；操作即时写回全局设备存储与黑名单并刷新列表。
class DeviceManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DeviceManagerDialog(RemoteServer *server, QWidget *parent = nullptr);

private slots:
    void refresh();

private:
    void onDelete();
    void onRename();
    // 根据当前选中行刷新按钮可用状态（仅黑名单设备不可重命名）
    void updateButtonState();
    // 当前选中行的 uuid / 仅黑名单标记（无选中返回空）
    QString currentUuid() const;
    bool currentIgnoredOnly() const;

    RemoteServer *m_server;
    QTableWidget *m_table;
    QPushButton *m_btnRename = nullptr;
    QPushButton *m_btnDelete = nullptr;
};

#endif // DEVICEMANAGERDIALOG_H
