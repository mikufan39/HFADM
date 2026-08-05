#ifndef DEVICEMANAGERDIALOG_H
#define DEVICEMANAGERDIALOG_H

#include <QDialog>
#include <QString>
#include <QVector>

class RemoteServer;
class QTableWidget;

// 已授权设备管理对话框（服务端）：
// 列出当前开放机型的全部已授权远程设备，支持删除、修改权限、重命名。
// 模态执行；操作即时写回 remote_device 表并刷新列表。
class DeviceManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DeviceManagerDialog(RemoteServer *server, QWidget *parent = nullptr);

private slots:
    void refresh();
    void onDelete();
    void onChangePermission();
    void onRename();

private:
    RemoteServer *m_server;
    QTableWidget *m_table;
};

#endif // DEVICEMANAGERDIALOG_H
