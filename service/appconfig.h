#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QByteArray>
#include <QString>
#include <QStringList>

// 应用配置文件（程序同目录/hfadm.session，INI 格式，QSettings 序列化）：
// hfadm.session 即软件的配置文件，集中保存所有持久化配置：
//   - 标签页会话：SessionManager 维护（键 tab*/activeIndex/count）
//   - 客户端连接凭证：remote/credentials/<host>/*（本组件）
//   - 服务端拒绝黑名单：remote/ignoredDevices（本组件）
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

} // namespace AppConfig

#endif // APPCONFIG_H
