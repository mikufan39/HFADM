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

QString deviceGroup(const QString &uuid)
{
    return QStringLiteral("remote/devices/%1").arg(uuid);
}

QString dateTimeToString(const QDateTime &dt)
{
    return dt.isValid() ? dt.toString(Qt::ISODate) : QString();
}

QDateTime dateTimeFromString(const QString &text)
{
    const QDateTime dt = QDateTime::fromString(text, Qt::ISODate);
    return dt.isValid() ? dt : QDateTime();
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

// ---- 服务端全局设备授权 ----

bool listDevices(QVector<RemoteDevice> &devices)
{
    devices.clear();
    QSettings settings(filePath(), QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
        return false;
    }
    settings.beginGroup(QStringLiteral("remote/devices"));
    const QStringList uuids = settings.childGroups();
    settings.endGroup();
    for (const QString &uuid : uuids) {
        RemoteDevice device;
        if (getDevice(uuid, device)) {
            devices.append(device);
        }
    }
    return true;
}

bool getDevice(const QString &uuid, RemoteDevice &device)
{
    if (uuid.isEmpty()) {
        return false;
    }
    QSettings settings(filePath(), QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
        return false;
    }
    settings.beginGroup(deviceGroup(uuid));
    const bool has = settings.contains(QStringLiteral("key"));
    device = RemoteDevice{};
    device.uuid = uuid;
    device.deviceName = settings.value(QStringLiteral("name")).toString();
    device.aesKey = QByteArray::fromBase64(
        settings.value(QStringLiteral("key")).toString().toLatin1());
    device.permission = static_cast<RemoteProtocol::Permission>(
        settings.value(QStringLiteral("permission"),
                       static_cast<int>(RemoteProtocol::Permission::ReadOnly)).toInt());
    device.createdAt = dateTimeFromString(settings.value(QStringLiteral("createdAt")).toString());
    device.lastSeen = dateTimeFromString(settings.value(QStringLiteral("lastSeen")).toString());
    settings.endGroup();
    return has && device.aesKey.size() == 16;
}

bool saveDevice(const RemoteDevice &device)
{
    if (device.uuid.isEmpty() || device.aesKey.size() != 16) {
        return false;
    }
    QSettings settings(filePath(), QSettings::IniFormat);
    settings.beginGroup(deviceGroup(device.uuid));
    settings.setValue(QStringLiteral("name"), device.deviceName);
    settings.setValue(QStringLiteral("key"), QString::fromLatin1(device.aesKey.toBase64()));
    settings.setValue(QStringLiteral("permission"), static_cast<int>(device.permission));
    settings.setValue(QStringLiteral("createdAt"), dateTimeToString(device.createdAt));
    settings.setValue(QStringLiteral("lastSeen"), dateTimeToString(device.lastSeen));
    settings.endGroup();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool deleteDevice(const QString &uuid)
{
    if (uuid.isEmpty()) {
        return false;
    }
    QSettings settings(filePath(), QSettings::IniFormat);
    settings.remove(deviceGroup(uuid));
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool updateDevicePermission(const QString &uuid, RemoteProtocol::Permission permission)
{
    RemoteDevice device;
    if (!getDevice(uuid, device)) {
        return false;
    }
    device.permission = permission;
    return saveDevice(device);
}

bool updateDeviceName(const QString &uuid, const QString &newName)
{
    RemoteDevice device;
    if (!getDevice(uuid, device)) {
        return false;
    }
    device.deviceName = newName;
    return saveDevice(device);
}

bool updateDeviceLastSeen(const QString &uuid, const QDateTime &time)
{
    RemoteDevice device;
    if (!getDevice(uuid, device)) {
        return false;
    }
    device.lastSeen = time;
    return saveDevice(device);
}

} // namespace AppConfig
