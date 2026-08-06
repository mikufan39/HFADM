#include "clientcredentialstore.h"
#include "service/appconfig.h"

namespace ClientCredentialStore {

bool load(const QString &host, Credential &cred)
{
    return AppConfig::loadCredential(host, cred.uuid, cred.aesKey, cred.deviceName);
}

bool save(const QString &host, const Credential &cred)
{
    return AppConfig::saveCredential(host, cred.uuid, cred.aesKey, cred.deviceName);
}

bool clear(const QString &host)
{
    return AppConfig::clearCredential(host);
}

} // namespace ClientCredentialStore
