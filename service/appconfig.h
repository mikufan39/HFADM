#ifndef APPCONFIG_H
#define APPCONFIG_H

#include "model/remotedevice.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

// 应用配置文件（程序同目录/hfadm.session，INI 格式，QSettings 序列化）：
// hfadm.session 即软件的配置文件，集中保存所有持久化配置：
//   - 标签页会话：SessionManager 维护（键 tab*/activeIndex/count）
//   - 客户端连接凭证：remote/credentials/<host>/*（本组件）
//   - 服务端拒绝黑名单：remote/ignoredDevices（本组件）
//   - 服务端全局设备授权：remote/devices/<uuid>/*（本组件，2026-08-10 起不再按机型项目库存储）
namespace AppConfig {

// 配置文件路径（程序同目录/hfadm.session）
QString filePath();

// ---- 客户端本机设备身份 ----
// 本机设备唯一 uuid：首次生成后持久化，配对/认证始终复用同一身份，
// 保证被服务端「拒绝且不再提示」后重试仍能命中黑名单
QString deviceUuid();

// ---- 客户端连接凭证（按服务端地址） ----
// 加载凭证；不存在返回 false
bool loadCredential(const QString &host, QString &uuid, QByteArray &aesKey,
                    QString &deviceName);
// 保存凭证（覆盖）；成功返回 true
bool saveCredential(const QString &host, const QString &uuid, const QByteArray &aesKey,
                    const QString &deviceName);
// 清除某地址的凭证（被服务端撤销授权时调用）
bool clearCredential(const QString &host);

// ---- 服务端拒绝黑名单（拒绝连接且不再提示的设备 uuid） ----
QStringList ignoredDevices();
bool isIgnoredDevice(const QString &uuid);
// 加入黑名单；已存在则视为成功
bool addIgnoredDevice(const QString &uuid);
// 移出黑名单（解除不再提示）
bool removeIgnoredDevice(const QString &uuid);

// ---- 服务端全局设备授权（不再按机型项目库存储）----
// 设备记录按 uuid 存于 remote/devices/<uuid>/ 组：名称/AES 密钥/权限/首次授权/上次连接。
// 授权全局生效：服务端切换绑定机型后，同一设备无需重新配对即可直接认证连接。
// 返回是否成功；记录不存在（如仅黑名单设备）返回 false
bool listDevices(QVector<RemoteDevice> &devices);
bool getDevice(const QString &uuid, RemoteDevice &device);
bool saveDevice(const RemoteDevice &device); // 新增或覆盖
bool deleteDevice(const QString &uuid);
bool updateDevicePermission(const QString &uuid, RemoteProtocol::Permission permission);
bool updateDeviceName(const QString &uuid, const QString &newName);
bool updateDeviceLastSeen(const QString &uuid, const QDateTime &time);

} // namespace AppConfig

#endif // APPCONFIG_H
