#ifndef REMOTEDEVICE_H
#define REMOTEDEVICE_H

#include "service/remoteprotocol.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

// 远程访问已授权设备记录（服务端持久化，全局存储于 hfadm.session，不按机型项目区分）
// 首次配对成功后写入；二次连接时按 uuid 查找密钥与权限；
// 服务端切换绑定机型后同一设备无需重新配对
struct RemoteDevice {
    QString uuid;            // 客户端设备唯一 ID（主键）
    QString deviceName;      // 设备显示名称
    QByteArray aesKey;       // 128 位 AES 会话密钥（16 字节）
    RemoteProtocol::Permission permission = RemoteProtocol::Permission::ReadOnly;
    QDateTime createdAt;     // 首次授权时间
    QDateTime lastSeen;      // 上次成功连接时间
    // 仅黑名单设备标记（配对被拒后只写入黑名单、无设备记录）：
    // 列表由黑名单合成显示，无密钥/授权时间，重命名等操作不可用
    bool ignoredOnly = false;
};

#endif // REMOTEDEVICE_H
