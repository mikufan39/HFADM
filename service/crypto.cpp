#include "crypto.h"

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <bcrypt.h>
#  pragma comment(lib, "bcrypt.lib")
#endif

#include <QRandomGenerator>

namespace AesGcm {

#ifdef Q_OS_WIN

namespace {

// BCrypt 算法提供者句柄（AES-GCM），进程内单例，首次使用时初始化
BCRYPT_ALG_HANDLE g_aesGcmAlg = nullptr;

BCRYPT_ALG_HANDLE aesGcmProvider()
{
    if (g_aesGcmAlg) {
        return g_aesGcmAlg;
    }
    if (BCryptOpenAlgorithmProvider(&g_aesGcmAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) {
        g_aesGcmAlg = nullptr;
        return nullptr;
    }
    const WCHAR mode[] = BCRYPT_CHAIN_MODE_GCM;
    if (BCryptSetProperty(g_aesGcmAlg, BCRYPT_CHAINING_MODE,
                          reinterpret_cast<PUCHAR>(const_cast<WCHAR *>(mode)),
                          sizeof(mode), 0) != 0) {
        BCryptCloseAlgorithmProvider(g_aesGcmAlg, 0);
        g_aesGcmAlg = nullptr;
        return nullptr;
    }
    return g_aesGcmAlg;
}

} // namespace

QByteArray generateKey()
{
    BCRYPT_ALG_HANDLE alg = aesGcmProvider();
    QByteArray key(16, '\0');
    if (!alg || BCryptGenRandom(alg, reinterpret_cast<PUCHAR>(key.data()), 16,
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        // 回退：Qt 随机数（非密码学级，仅兜底）
        QRandomGenerator::global()->generate(key.begin(), key.end());
    }
    return key;
}

QByteArray generateNonce()
{
    BCRYPT_ALG_HANDLE alg = aesGcmProvider();
    QByteArray nonce(12, '\0');
    if (!alg || BCryptGenRandom(alg, reinterpret_cast<PUCHAR>(nonce.data()), 12,
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        QRandomGenerator::global()->generate(nonce.begin(), nonce.end());
    }
    return nonce;
}

bool encrypt(const QByteArray &key, const QByteArray &nonce,
             const QByteArray &plaintext, const QByteArray &aad,
             QByteArray &ciphertext, QByteArray &tag)
{
    ciphertext.clear();
    tag.clear();
    BCRYPT_ALG_HANDLE alg = aesGcmProvider();
    if (!alg || key.size() != 16 || nonce.size() != 12) {
        return false;
    }

    BCRYPT_KEY_HANDLE hKey = nullptr;
    if (BCryptGenerateSymmetricKey(alg, &hKey, nullptr, 0,
                                   reinterpret_cast<PUCHAR>(const_cast<char *>(key.constData())),
                                   16, 0) != 0) {
        return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char *>(nonce.constData()));
    authInfo.cbNonce = 12;
    authInfo.pbTag = nullptr;
    authInfo.cbTag = 16;
    if (!aad.isEmpty()) {
        authInfo.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char *>(aad.constData()));
        authInfo.cbAuthData = static_cast<ULONG>(aad.size());
    }

    // 第一遍：查询输出长度
    ULONG outLen = 0;
    NTSTATUS st = BCryptEncrypt(hKey, reinterpret_cast<PUCHAR>(const_cast<char *>(plaintext.constData())),
                                static_cast<ULONG>(plaintext.size()), &authInfo,
                                nullptr, 0, nullptr, 0, &outLen, 0);
    if (st != 0) {
        BCryptDestroyKey(hKey);
        return false;
    }

    QByteArray tagBuf(16, '\0');
    QByteArray cipherBuf(static_cast<int>(outLen), '\0');
    authInfo.pbTag = reinterpret_cast<PUCHAR>(tagBuf.data());
    authInfo.cbTag = 16;

    st = BCryptEncrypt(hKey, reinterpret_cast<PUCHAR>(const_cast<char *>(plaintext.constData())),
                       static_cast<ULONG>(plaintext.size()), &authInfo,
                       nullptr, 0, reinterpret_cast<PUCHAR>(cipherBuf.data()),
                       static_cast<ULONG>(cipherBuf.size()), &outLen, 0);
    BCryptDestroyKey(hKey);
    if (st != 0) {
        return false;
    }
    cipherBuf.resize(static_cast<int>(outLen));
    ciphertext = cipherBuf;
    tag = tagBuf;
    return true;
}

bool decrypt(const QByteArray &key, const QByteArray &nonce,
             const QByteArray &ciphertext, const QByteArray &aad,
             const QByteArray &tag, QByteArray &plaintext)
{
    plaintext.clear();
    BCRYPT_ALG_HANDLE alg = aesGcmProvider();
    if (!alg || key.size() != 16 || nonce.size() != 12 || tag.size() != 16) {
        return false;
    }

    BCRYPT_KEY_HANDLE hKey = nullptr;
    if (BCryptGenerateSymmetricKey(alg, &hKey, nullptr, 0,
                                   reinterpret_cast<PUCHAR>(const_cast<char *>(key.constData())),
                                   16, 0) != 0) {
        return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char *>(nonce.constData()));
    authInfo.cbNonce = 12;
    authInfo.pbTag = reinterpret_cast<PUCHAR>(const_cast<char *>(tag.constData()));
    authInfo.cbTag = 16;
    if (!aad.isEmpty()) {
        authInfo.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char *>(aad.constData()));
        authInfo.cbAuthData = static_cast<ULONG>(aad.size());
    }

    ULONG outLen = 0;
    NTSTATUS st = BCryptDecrypt(hKey, reinterpret_cast<PUCHAR>(const_cast<char *>(ciphertext.constData())),
                                static_cast<ULONG>(ciphertext.size()), &authInfo,
                                nullptr, 0, nullptr, 0, &outLen, 0);
    if (st != 0) {
        BCryptDestroyKey(hKey);
        return false; // 标签不匹配或密钥错误
    }

    QByteArray plainBuf(static_cast<int>(outLen), '\0');
    st = BCryptDecrypt(hKey, reinterpret_cast<PUCHAR>(const_cast<char *>(ciphertext.constData())),
                       static_cast<ULONG>(ciphertext.size()), &authInfo,
                       nullptr, 0, reinterpret_cast<PUCHAR>(plainBuf.data()),
                       static_cast<ULONG>(plainBuf.size()), &outLen, 0);
    BCryptDestroyKey(hKey);
    if (st != 0) {
        return false;
    }
    plainBuf.resize(static_cast<int>(outLen));
    plaintext = plainBuf;
    return true;
}

#else
// 非 Windows 平台：当前项目仅面向 Windows，此处提供兜底实现（不安全，仅供编译通过）
QByteArray generateKey()
{
    QByteArray k(16, '\0');
    QRandomGenerator::global()->generate(k.begin(), k.end());
    return k;
}
QByteArray generateNonce()
{
    QByteArray n(12, '\0');
    QRandomGenerator::global()->generate(n.begin(), n.end());
    return n;
}
bool encrypt(const QByteArray &, const QByteArray &, const QByteArray &,
             const QByteArray &, QByteArray &, QByteArray &) { return false; }
bool decrypt(const QByteArray &, const QByteArray &, const QByteArray &,
             const QByteArray &, const QByteArray &, QByteArray &) { return false; }
#endif

QString generatePin()
{
    // 4 位随机数字口令，前导补零
    return QStringLiteral("%1").arg(QRandomGenerator::global()->bounded(10000), 4, 10, QChar('0'));
}

} // namespace AesGcm
