#ifndef REMOTEDEVICE_H
#define REMOTEDEVICE_H

#include "service/remoteprotocol.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

// 远程访问已授权设备记录（服务端持久化，按机型项目存储）
// 首次配对成功后写入；二次连接时按 uuid 查找密钥与权限
struct RemoteDevice {
    QString uuid;            // 客户端设备唯一 ID（主键）
    QString deviceName;      // 设备显示名称
    QByteArray aesKey;       // 128 位 AES 会话密钥（16 字节）
    RemoteProtocol::Permission permission = RemoteProtocol::Permission::ReadOnly;
    QDateTime createdAt;     // 首次授权时间
    QDateTime lastSeen;      // 上次成功连接时间
};

#endif // REMOTEDEVICE_H
