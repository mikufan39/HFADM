#ifndef REMOTEDIALOG_H
#define REMOTEDIALOG_H

#include <QString>

class QWidget;

// 连接到远程对话框：输入目标机器局域网 IPv4 地址（仅私有网段，端口固定 312）
// 校验通过后返回 true 并输出地址；取消返回 false
bool showRemoteConnectDialog(QWidget *parent, QString &address);

#endif // REMOTEDIALOG_H
