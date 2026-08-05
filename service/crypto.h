#ifndef CRYPTO_H
#define CRYPTO_H

#include <QByteArray>
#include <QString>

// AES-128-GCM 加解密（Windows BCrypt/CNG 实现）
// 约定：密钥 16 字节、nonce 12 字节、认证标签 16 字节
// 用于远程访问协议的会话加密：每条消息使用新的随机 nonce，附加认证数据(AAD)绑定帧序号
namespace AesGcm {

// 生成 16 字节（128 位）随机 AES 密钥
QByteArray generateKey();
// 生成 12 字节（96 位）随机 nonce/IV
QByteArray generateNonce();
// 生成 4 位随机数字口令（首次配对用，用户在服务端人工比对）
QString generatePin();

// 加密：成功返回 true，ciphertext 不含 tag，tag 单独输出；aad 可为空
bool encrypt(const QByteArray &key, const QByteArray &nonce,
             const QByteArray &plaintext, const QByteArray &aad,
             QByteArray &ciphertext, QByteArray &tag);

// 解密：验证 tag，成功返回 true 并输出 plaintext；失败（标签不匹配/密钥错误）返回 false
bool decrypt(const QByteArray &key, const QByteArray &nonce,
             const QByteArray &ciphertext, const QByteArray &aad,
             const QByteArray &tag, QByteArray &plaintext);

} // namespace AesGcm

#endif // CRYPTO_H
