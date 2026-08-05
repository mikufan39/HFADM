#include "clientcredentialstore.h"

#include <QSettings>

namespace ClientCredentialStore {

namespace {
// QSettings 组路径：remote/credentials/<host>
QString groupKey(const QString &host)
{
    return QStringLiteral("remote/credentials/%1").arg(host);
}
} // namespace

bool load(const QString &host, Credential &cred)
{
    if (host.isEmpty()) {
        return false;
    }
    QSettings settings;
    settings.beginGroup(groupKey(host));
    const bool has = settings.contains(QStringLiteral("uuid"));
    cred.uuid = settings.value(QStringLiteral("uuid")).toString();
    cred.aesKey = QByteArray::fromBase64(settings.value(QStringLiteral("key")).toString().toLatin1());
    cred.deviceName = settings.value(QStringLiteral("name")).toString();
    settings.endGroup();
    return has && !cred.uuid.isEmpty() && cred.aesKey.size() == 16;
}

bool save(const QString &host, const Credential &cred)
{
    if (host.isEmpty() || cred.uuid.isEmpty() || cred.aesKey.size() != 16) {
        return false;
    }
    QSettings settings;
    settings.beginGroup(groupKey(host));
    settings.setValue(QStringLiteral("uuid"), cred.uuid);
    settings.setValue(QStringLiteral("key"), QString::fromLatin1(cred.aesKey.toBase64()));
    settings.setValue(QStringLiteral("name"), cred.deviceName);
    settings.endGroup();
    return true;
}

bool clear(const QString &host)
{
    if (host.isEmpty()) {
        return false;
    }
    QSettings settings;
    settings.remove(groupKey(host));
    return true;
}

} // namespace ClientCredentialStore
