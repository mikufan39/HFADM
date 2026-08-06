#ifndef REMOTEDIALOG_H
#define REMOTEDIALOG_H

class QWidget;
class RemoteClient;
class PinPromptDialog;

// 连接到远程对话框（客户端）：
//   ① 提示「请输入目标地址」，仅保留「连接」按钮；
//      地址非合法 IPv4 时清空输入框，占位提示变为「输入内容错误」
//   ② 连接中弹窗保持：提示「正在尝试连接」，按钮变「取消」可中止连接；
//      首次配对时（TCP 已连通）立即弹出客户端口令窗口，与服务端确认并行
//   ③ 连接失败弹窗保持：提示「连接错误」，输入内容保留可修改重试
//   ④ 连接成功关闭对话框，输出已连接的 RemoteClient（所有权转移给调用方）
// 返回 true=连接成功（*client 非空）；false=用户取消或连接未成功
bool showRemoteConnectDialog(QWidget *parent, RemoteClient **client);

#endif // REMOTEDIALOG_H
