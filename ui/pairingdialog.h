#ifndef PAIRINGDIALOG_H
#define PAIRINGDIALOG_H

#include "service/remoteprotocol.h"

#include <QString>

class QWidget;

// 首次配对确认对话框（服务端）：
// 显示客户端名称、来源 IP 与 4 位口令，由用户选择权限并确认/拒绝。
// 返回 PairingResult：accepted=false 表示拒绝，permission 为所选权限。
RemoteProtocol::PairingResult showPairingDialog(QWidget *parent,
                                                const RemoteProtocol::PairingRequest &req);

#endif // PAIRINGDIALOG_H
