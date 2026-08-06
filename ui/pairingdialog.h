#ifndef PAIRINGDIALOG_H
#define PAIRINGDIALOG_H

#include "service/remoteprotocol.h"

#include <QString>

class QWidget;
class RemoteServer;

// 首次配对确认对话框（服务端）：
//   第一行「新的连接请求待批准」+ 来源信息 + 大号口令（#39c5bb 红下划线）；
//   右下角两个按钮：
//     - 「允许受限的访问请求」：授权但仅只读（后续可在设备管理中提升为可写）
//     - 「拒绝连接且不再提示（N）」：N 为 60 秒倒计时，点击或倒计时结束即拒绝并拉黑该设备
// 弹窗通过 server 注册为「当前挂起弹窗」，客户端发送取消配对命令时可被外部关闭
// （关闭视为拒绝且不记录）。
// 返回 PairingResult：accepted=false 表示拒绝，permission 固定为只读，
// neverAskAgain=true 表示拒绝且不再提示（仅由按钮/倒计时触发）。
RemoteProtocol::PairingResult showPairingDialog(QWidget *parent,
                                                const RemoteProtocol::PairingRequest &req,
                                                RemoteServer *server);

#endif // PAIRINGDIALOG_H
