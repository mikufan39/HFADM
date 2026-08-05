#ifndef CLIENTCREDENTIALSTORE_H
#define CLIENTCREDENTIALSTORE_H

#include <QByteArray>
#include <QString>

// 客户端配对凭证存储：按服务端地址持久化 {uuid, aesKey, deviceName}
// 首次配对成功后保存，二次连接直接读取走认证流程；撤销授权后清除。
// 存储位置：QSettings（组织 HFADM、应用 RemoteClient），键 remote/credentials/<host>
namespace ClientCredentialStore {

struct Credential {
    QString uuid;          // 设备唯一 ID
    QByteArray aesKey;     // 128 位 AES 密钥（16 字节）
    QString deviceName;    // 设备名称
};

// 按服务端地址加载凭证；不存在返回 false
bool load(const QString &host, Credential &cred);
// 保存凭证（覆盖）
bool save(const QString &host, const Credential &cred);
// 清除某地址的凭证（被服务端撤销授权时调用）
bool clear(const QString &host);

} // namespace ClientCredentialStore

#endif // CLIENTCREDENTIALSTORE_H
