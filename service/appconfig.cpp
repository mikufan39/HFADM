#include "appconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QUuid>

namespace AppConfig {

QString filePath()
{
    // 程序同目录（与 SessionManager 会话文件同一路径，hfadm.session 即软件配置文件）
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("hfadm.session"));
}

namespace {
QString credentialGroup(const QString &host)
{
    return QStringLiteral("remote/credentials/%1").arg(host);
}
} // namespace

// ---- 客户端本机设备身份 ----

QString deviceUuid()
{
    QSettings settings(filePath(), QSettings::IniFormat);
    QString uuid = settings.value(QStringLiteral("remote/deviceUuid")).toString();
    if (uuid.isEmpty()) {
        uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue(QStringLiteral("remote/deviceUuid"), uuid);
        settings.sync();
    }
    return uuid;
}

// ---- 客户端连接凭证 ----

bool loadCredential(const QString &host, QString &uuid, QByteArray &aesKey,
                    QString &deviceName)
{
    if (host.isEmpty()) {
        return false;
    }
    QSettings settings(filePath(), QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
        return false;
    }
    settings.beginGroup(credentialGroup(host));
    const bool has = settings.contains(QStringLiteral("uuid"));
    uuid = settings.value(QStringLiteral("uuid")).toString();
    aesKey = QByteArray::fromBase64(
        settings.value(QStringLiteral("key")).toString().toLatin1());
    deviceName = settings.value(QStringLiteral("name")).toString();
    settings.endGroup();
    return has && !uuid.isEmpty() && aesKey.size() == 16;
}

bool saveCredential(const QString &host, const QString &uuid, const QByteArray &aesKey,
                    const QString &deviceName)
{
    if (host.isEmpty() || uuid.isEmpty() || aesKey.size() != 16) {
        return false;
    }
    QSettings settings(filePath(), QSettings::IniFormat);
    settings.beginGroup(credentialGroup(host));
    settings.setValue(QStringLiteral("uuid"), uuid);
    settings.setValue(QStringLiteral("key"), QString::fromLatin1(aesKey.toBase64()));
    settings.setValue(QStringLiteral("name"), deviceName);
    settings.endGroup();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool clearCredential(const QString &host)
{
    if (host.isEmpty()) {
        return false;
    }
    QSettings settings(filePath(), QSettings::IniFormat);
    settings.remove(credentialGroup(host));
    settings.sync();
    return settings.status() == QSettings::NoError;
}

// ---- 服务端拒绝黑名单 ----

QStringList ignoredDevices()
{
    QSettings settings(filePath(), QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
        return {};
    }
    return settings.value(QStringLiteral("remote/ignoredDevices")).toStringList();
}

bool isIgnoredDevice(const QString &uuid)
{
    if (uuid.isEmpty()) {
        return false;
    }
    return ignoredDevices().contains(uuid);
}

bool addIgnoredDevice(const QString &uuid)
{
    if (uuid.isEmpty()) {
        return false;
    }
    QStringList list = ignoredDevices();
    if (!list.contains(uuid)) {
        list.append(uuid);
        QSettings settings(filePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("remote/ignoredDevices"), list);
        settings.sync();
        return settings.status() == QSettings::NoError;
    }
    return true;
}

bool removeIgnoredDevice(const QString &uuid)
{
    if (uuid.isEmpty()) {
        return false;
    }
    QStringList list = ignoredDevices();
    if (!list.removeAll(uuid)) {
        return true; // 本就不在黑名单中
    }
    QSettings settings(filePath(), QSettings::IniFormat);
    if (list.isEmpty()) {
        // 空列表直接删键，避免写入 @Invalid() 占位
        settings.remove(QStringLiteral("remote/ignoredDevices"));
    } else {
        settings.setValue(QStringLiteral("remote/ignoredDevices"), list);
    }
    settings.sync();
    return settings.status() == QSettings::NoError;
}

} // namespace AppConfig
