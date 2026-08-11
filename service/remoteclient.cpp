#include "remoteclient.h"
#include "remoteprotocol.h"
#include "crypto.h"
#include "service/appconfig.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSysInfo>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

namespace {
QByteArray frameBytes(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}

QString defaultDeviceName()
{
    return QSysInfo::machineHostName();
}
} // namespace

RemoteClient::RemoteClient(QObject *parent)
    : QObject(parent)
{
    m_socket = new QTcpSocket(this);
    m_tempDir = new QTemporaryDir;
    m_lastReceive = new QElapsedTimer;
    m_lastReceive->start();

    connect(m_socket, &QTcpSocket::readyRead, this, [this] {
        m_lastReceive->restart();
        m_buffer.append(m_socket->readAll());
        int newline = -1;
        while ((newline = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(newline);
            m_buffer.remove(0, newline + 1);
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) {
                continue;
            }
            const QJsonObject obj = doc.object();
            // 仅处理响应帧；按 envelope id 匹配等待中的请求
            if (obj.value(QStringLiteral("type")).toString() != QLatin1String("response")) {
                continue;
            }
            const qint64 respId = obj.value(QStringLiteral("id")).toVariant().toLongLong();
            if (m_pendingBusiness.contains(respId)) {
                // 业务帧异步响应：解密后发 requestFinished
                dispatchBusinessResponse(respId, obj.value(QStringLiteral("body")).toObject());
            } else if (m_pendingId != 0 && respId == m_pendingId) {
                // 控制帧响应（握手阶段，sendControlRequest 同步等待）
                m_pendingResponse = obj;
                m_pendingId = 0;
                emit responseReady();
            }
            // 其余（心跳响应、未登记帧）静默丢弃
        }
    });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (isConnected()) {
            return; // 连接已建立后的错误另行由静默检测处理
        }
    });
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        m_heartbeatTimer->stop();
        m_silenceTimer->stop();
        // 断线：对所有在途异步请求发失败信号，避免 awaitOnce 永久挂起
        const auto pending = m_pendingBusiness;
        m_pendingBusiness.clear();
        for (auto it = pending.begin(); it != pending.end(); ++it) {
            emit requestFinished(it.key(), false, QJsonObject(),
                                 QStringLiteral("连接已断开"));
        }
        if (!m_disconnecting && !m_peerAddress.isEmpty()) {
            emit connectionLost(QStringLiteral("与 %1 的连接已断开").arg(m_peerAddress));
            m_peerAddress.clear();
        }
    });

    // 心跳：每 15s 发送一次加密心跳帧（不等待响应；响应刷新静默计时）
    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(RemoteProtocol::kHeartbeatIntervalMs);
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this] {
        if (!isConnected() || m_sessionKey.isEmpty()) {
            return;
        }
        sendFireAndForgetRequest(QLatin1String(RemoteProtocol::kReqHeartbeat), QJsonObject());
    });

    // 静默检测：超过 60s 未收到任何帧判定断线
    m_silenceTimer = new QTimer(this);
    m_silenceTimer->setInterval(5000);
    connect(m_silenceTimer, &QTimer::timeout, this, [this] {
        if (isConnected() && m_lastReceive->elapsed() > RemoteProtocol::kSilenceTimeoutMs) {
            emit connectionLost(QStringLiteral("远程连接超时（%1）").arg(m_peerAddress));
            disconnectFrom();
        }
    });
}

RemoteClient::~RemoteClient()
{
    disconnectFrom();
    delete m_tempDir;
    delete m_lastReceive;
}

bool RemoteClient::connectTo(const QString &address, QString *error)
{
    m_connectAborted = false;
    m_lastPairPin.clear();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        disconnectFrom();
    }
    m_disconnecting = false;
    m_peerAddress = address;

    m_socket->connectToHost(address, RemoteProtocol::kPort);
    QEventLoop loop;
    QTimer::singleShot(RemoteProtocol::kConnectTimeoutMs, &loop, &QEventLoop::quit);
    connect(m_socket, &QTcpSocket::connected, &loop, &QEventLoop::quit);
    connect(m_socket, &QTcpSocket::errorOccurred, &loop, &QEventLoop::quit);
    connect(this, &RemoteClient::connectCancelled, &loop, &QEventLoop::quit);
    loop.exec();

    if (m_connectAborted) {
        if (error) {
            *error = QStringLiteral("连接已取消");
        }
        disconnectFrom(); // 清理未完成的 TCP 连接
        return false;
    }
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        if (error) {
            *error = QStringLiteral("无法连接到 %1：%2")
                         .arg(address, m_socket->errorString());
        }
        m_peerAddress.clear();
        return false;
    }

    // 优先尝试已有凭证的认证流程；失败或无凭证则走配对流程
    ClientCredentialStore::Credential cred;
    if (ClientCredentialStore::load(address, cred)) {
        if (doAuth(address, cred, error)) {
            m_lastReceive->restart();
            m_heartbeatTimer->start();
            m_silenceTimer->start();
            return true;
        }
        // 被禁止的设备（黑名单/「不允许的连接」）：无需回退重新配对，重试同样会被拒绝
        if (error && error->contains(QStringLiteral("禁止连接"))) {
            disconnectFrom();
            return false;
        }
        // 其他认证失败：凭证可能已被服务端撤销，清除后回退到重新配对
        ClientCredentialStore::clear(address);
        m_sessionKey.clear();
    }

    if (!doPair(address, error)) {
        disconnectFrom();
        return false;
    }

    m_lastReceive->restart();
    m_heartbeatTimer->start();
    m_silenceTimer->start();
    return true;
}

bool RemoteClient::doAuth(const QString &host,
                          const ClientCredentialStore::Credential &cred, QString *error)
{
    // 1. 发送 AuthRequest{uuid}，服务端返回 AuthChallenge{nonce}
    QJsonObject reqBody;
    reqBody.insert(QStringLiteral("uuid"), cred.uuid);
    QJsonObject challengeResp;
    if (!sendControlRequest(QLatin1String(RemoteProtocol::kCmdAuth), reqBody, challengeResp, error)) {
        return false;
    }
    const QJsonObject challengeBody = challengeResp.value(QStringLiteral("body")).toObject();
    // 认证失败（未授权）时 success=false
    if (!challengeResp.value(QStringLiteral("success")).toBool(true) && challengeBody.contains(QStringLiteral("message"))) {
        if (error) {
            *error = challengeBody.value(QStringLiteral("message")).toString();
        }
        return false;
    }
    const QByteArray nonce = QByteArray::fromBase64(
        challengeBody.value(QStringLiteral("nonce")).toString().toLatin1());
    if (nonce.isEmpty()) {
        if (error) {
            *error = QStringLiteral("认证挑战无效");
        }
        return false;
    }

    // 2. 用凭证密钥加密挑战 nonce，返回 AuthResponse
    //    明文 = nonce 本身；AAD 绑定本次请求 id（与服务端一致）
    const qint64 respId = ++m_requestId; // 显式领取 id，确保 AAD 与发送 id 一致
    const QByteArray aad = QByteArray::number(respId);
    QByteArray cipher, tag;
    if (!AesGcm::encrypt(cred.aesKey, nonce, nonce, aad, cipher, tag)) {
        if (error) {
            *error = QStringLiteral("认证加密失败");
        }
        return false;
    }
    QJsonObject authRespBody;
    authRespBody.insert(QStringLiteral("nonce"), QString::fromLatin1(nonce.toBase64()));
    authRespBody.insert(QStringLiteral("ciphertext"), QString::fromLatin1(cipher.toBase64()));
    authRespBody.insert(QStringLiteral("tag"), QString::fromLatin1(tag.toBase64()));

    QJsonObject authRespFrame;
    authRespFrame.insert(QStringLiteral("type"), QStringLiteral("request"));
    authRespFrame.insert(QStringLiteral("cmd"), QLatin1String(RemoteProtocol::kCmdAuthResp));
    authRespFrame.insert(QStringLiteral("id"), respId);
    authRespFrame.insert(QStringLiteral("body"), authRespBody);
    m_socket->write(frameBytes(authRespFrame));

    m_pendingId = respId;
    m_pendingResponse = QJsonObject();
    {
        QEventLoop loop;
        connect(this, &RemoteClient::responseReady, &loop, &QEventLoop::quit);
        connect(this, &RemoteClient::connectCancelled, &loop, &QEventLoop::quit);
        QTimer::singleShot(RemoteProtocol::kConnectTimeoutMs, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (m_connectAborted) {
        m_pendingId = 0;
        if (error) {
            *error = QStringLiteral("连接已取消");
        }
        return false;
    }
    if (m_pendingId != 0) {
        m_pendingId = 0;
        if (error) {
            *error = QStringLiteral("认证响应超时");
        }
        return false;
    }
    const QJsonObject authResult = m_pendingResponse;
    if (!authResult.value(QStringLiteral("success")).toBool(false)) {
        if (error) {
            const QString msg = authResult.value(QStringLiteral("body")).toObject()
                .value(QStringLiteral("message")).toString();
            *error = msg.isEmpty() ? QStringLiteral("认证失败") : msg;
        }
        return false;
    }

    m_sessionKey = cred.aesKey;
    applyHandshakeBody(authResult.value(QStringLiteral("body")).toObject());
    return true;
}

bool RemoteClient::doPair(const QString &host, QString *error)
{
    // 复用本机持久化设备 uuid（首次生成后固定）：被服务端「拒绝且不再提示」后，
    // 重试仍使用同一身份，服务端黑名单才能命中
    const QString uuid = AppConfig::deviceUuid();
    const QString pin = AesGcm::generatePin();
    const QByteArray key = AesGcm::generateKey();
    const QString deviceName = defaultDeviceName();
    m_lastPairPin = pin; // 配对成功后供 UI 展示口令（倒计时窗口）

    emit pairingStarted(pin, deviceName);

    QJsonObject body;
    body.insert(QStringLiteral("uuid"), uuid);
    body.insert(QStringLiteral("deviceName"), deviceName);
    body.insert(QStringLiteral("pin"), pin);
    body.insert(QStringLiteral("key"), QString::fromLatin1(key.toBase64()));

    QJsonObject response;
    if (!sendControlRequest(QLatin1String(RemoteProtocol::kCmdConnect), body, response, error)) {
        return false;
    }
    if (!response.value(QStringLiteral("success")).toBool(false)) {
        if (error) {
            const QString msg = response.value(QStringLiteral("body")).toObject()
                .value(QStringLiteral("message")).toString();
            *error = msg.isEmpty() ? QStringLiteral("配对被拒绝") : msg;
        }
        return false;
    }

    // 配对成功：保存凭证，建立会话
    ClientCredentialStore::Credential cred{uuid, key, deviceName};
    ClientCredentialStore::save(host, cred);
    m_sessionKey = key;
    applyHandshakeBody(response.value(QStringLiteral("body")).toObject());
    return true;
}

void RemoteClient::applyHandshakeBody(const QJsonObject &body)
{
    m_projectName = body.value(QStringLiteral("projectName")).toString();
    m_projectPath = body.value(QStringLiteral("projectPath")).toString();
    m_projectRootNodeId = body.value(QStringLiteral("projectRootNodeId")).toVariant().toLongLong();
    m_permission = static_cast<RemoteProtocol::Permission>(
        body.value(QStringLiteral("permission")).toInt(static_cast<int>(m_permission)));
}

void RemoteClient::abortConnecting()
{
    m_connectAborted = true;
    // 通知内部等待循环退出；socket 由 connectTo 失败路径的 disconnectFrom() 统一清理，
    // 避免在事件循环分发期间直接 abort socket 触发 QSocketNotifier 竞态
    emit connectCancelled();
}

void RemoteClient::cancelPairing()
{
    // 先通知服务端关闭挂起的配对确认弹窗（明文控制帧），再中止本地连接流程；
    // 服务端弹窗被关闭后走「拒绝且不记录」路径，双方均不留任何配对结果
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject frame;
        frame.insert(QStringLiteral("type"), QStringLiteral("request"));
        frame.insert(QStringLiteral("cmd"), QLatin1String(RemoteProtocol::kCmdCancelPair));
        frame.insert(QStringLiteral("id"), ++m_requestId);
        frame.insert(QStringLiteral("body"), QJsonObject());
        m_socket->write(frameBytes(frame));
        m_socket->flush(); // 尽力发出取消帧（随后连接即被中止）
    }
    abortConnecting();
}

bool RemoteClient::connectWasCancelled() const
{
    return m_connectAborted;
}

QString RemoteClient::lastPairPin() const
{
    return m_lastPairPin;
}

void RemoteClient::disconnectFrom()
{
    m_disconnecting = true; // 主动断开：不触发 connectionLost 提示
    m_heartbeatTimer->stop();
    m_silenceTimer->stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->abort();
        }
    }
    m_peerAddress.clear();
    m_buffer.clear();
    m_sessionKey.clear();
}

bool RemoteClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

QString RemoteClient::projectName() const
{
    return m_projectName;
}

QString RemoteClient::projectPath() const
{
    return m_projectPath;
}

qint64 RemoteClient::rootNodeId() const
{
    return m_projectRootNodeId;
}

QString RemoteClient::peerAddress() const
{
    return m_peerAddress;
}

RemoteProtocol::Permission RemoteClient::permission() const
{
    return m_permission;
}

bool RemoteClient::sendControlRequest(const QString &cmd, const QJsonObject &body,
                                      QJsonObject &response, QString *error)
{
    if (!isConnected()) {
        if (error) {
            *error = QStringLiteral("未连接到远程");
        }
        return false;
    }
    QJsonObject frame;
    frame.insert(QStringLiteral("type"), QStringLiteral("request"));
    frame.insert(QStringLiteral("cmd"), cmd);
    frame.insert(QStringLiteral("id"), ++m_requestId);
    frame.insert(QStringLiteral("body"), body);
    m_socket->write(frameBytes(frame));

    m_pendingId = m_requestId;
    m_pendingResponse = QJsonObject();

    QEventLoop loop;
    connect(this, &RemoteClient::responseReady, &loop, &QEventLoop::quit);
    connect(this, &RemoteClient::connectCancelled, &loop, &QEventLoop::quit);
    QTimer::singleShot(RemoteProtocol::kConnectTimeoutMs, &loop, &QEventLoop::quit);
    loop.exec();

    if (m_connectAborted) {
        m_pendingId = 0;
        if (error) {
            *error = QStringLiteral("连接已取消");
        }
        return false;
    }
    if (m_pendingId != 0) {
        m_pendingId = 0;
        if (error) {
            *error = QStringLiteral("远程请求超时（%1）").arg(cmd);
        }
        return false;
    }
    response = m_pendingResponse;
    return true;
}

bool RemoteClient::sendRequest(const QString &req, const QJsonObject &params,
                               QJsonObject &response, QString *error)
{
    if (!isConnected()) {
        if (error) {
            *error = QStringLiteral("未连接到远程");
        }
        return false;
    }
    if (m_sessionKey.isEmpty()) {
        if (error) {
            *error = QStringLiteral("会话未建立");
        }
        return false;
    }

    // 桥接异步核心：发异步请求 + QEventLoop 等 requestFinished（旧同步 API，迁移完毕后删除）
    const qint64 id = sendRequestAsync(req, params);
    bool ok = false;
    QString err;
    QEventLoop loop;
    QMetaObject::Connection c = connect(this, &RemoteClient::requestFinished, &loop,
        [&](qint64 rid, bool rOk, const QJsonObject &data, const QString &rErr) {
            if (rid != id) {
                return;
            }
            ok = rOk;
            err = rErr;
            response = data;
            loop.quit();
        });
    QTimer::singleShot(RemoteProtocol::kRequestTimeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    disconnect(c);
    if (m_pendingBusiness.contains(id)) {
        // 超时：pending 未被 dispatch 清除
        m_pendingBusiness.remove(id);
        if (error) {
            *error = QStringLiteral("远程请求超时（%1）").arg(req);
        }
        return false;
    }
    if (!ok && error) {
        *error = err.isEmpty() ? QStringLiteral("远程操作失败（%1）").arg(req) : err;
    }
    return ok;
}

void RemoteClient::sendFireAndForgetRequest(const QString &req, const QJsonObject &params)
{
    if (m_sessionKey.isEmpty() || !isConnected()) {
        return;
    }
    const qint64 id = ++m_requestId;
    QJsonObject body;
    if (!RemoteProtocol::encryptBody(m_sessionKey, id, params, body)) {
        return;
    }
    QJsonObject frame;
    frame.insert(QStringLiteral("type"), QStringLiteral("request"));
    frame.insert(QStringLiteral("cmd"), req);
    frame.insert(QStringLiteral("id"), id);
    frame.insert(QStringLiteral("body"), body);
    m_socket->write(frameBytes(frame));
}

// ---- 异步核心 ----

qint64 RemoteClient::sendRequestAsync(const QString &req, const QJsonObject &params)
{
    const qint64 id = ++m_requestId;
    if (!isConnected() || m_sessionKey.isEmpty()) {
        QTimer::singleShot(0, this, [this, id]() {
            emit requestFinished(id, false, QJsonObject(), QStringLiteral("未连接或会话未建立"));
        });
        return id;
    }
    // 写操作附加幂等键（服务端按 设备uuid+键 去重）
    QJsonObject effectiveParams = params;
    if (RemoteProtocol::isWriteCommand(req)) {
        effectiveParams.insert(QStringLiteral("idempotencyKey"),
                               RemoteProtocol::computeIdempotencyKey(req, params));
    }
    QJsonObject body;
    if (!RemoteProtocol::encryptBody(m_sessionKey, id, effectiveParams, body)) {
        QTimer::singleShot(0, this, [this, id]() {
            emit requestFinished(id, false, QJsonObject(), QStringLiteral("请求加密失败"));
        });
        return id;
    }
    QJsonObject frame;
    frame.insert(QStringLiteral("type"), QStringLiteral("request"));
    frame.insert(QStringLiteral("cmd"), req);
    frame.insert(QStringLiteral("id"), id);
    frame.insert(QStringLiteral("body"), body);
    m_socket->write(frameBytes(frame));
    m_pendingBusiness.insert(id, PendingReq{req});
    return id;
}

void RemoteClient::dispatchBusinessResponse(qint64 id, const QJsonObject &body)
{
    auto it = m_pendingBusiness.find(id);
    if (it == m_pendingBusiness.end()) {
        return;
    }
    const QString cmd = it.value().cmd;
    m_pendingBusiness.erase(it);

    QJsonObject payload;
    if (!RemoteProtocol::decryptBody(m_sessionKey, id, body, payload)) {
        emit requestFinished(id, false, QJsonObject(), QStringLiteral("响应解密失败"));
        return;
    }
    const bool ok = payload.value(QStringLiteral("success")).toBool(false);
    if (!ok) {
        const QString msg = payload.value(QStringLiteral("message")).toString();
        emit requestFinished(id, false, QJsonObject(),
                             msg.isEmpty() ? QStringLiteral("远程操作失败") : msg);
        return;
    }
    QJsonObject data = payload.value(QStringLiteral("data")).toObject();
    // getDrawingFile 响应：base64 写临时目录，data 改放 tempFilePath
    if (cmd == QLatin1String(RemoteProtocol::kReqGetDrawingFile)) {
        const QString fileName = data.value(QStringLiteral("fileName")).toString();
        const QString base64 = data.value(QStringLiteral("data")).toString();
        if (fileName.isEmpty() || base64.isEmpty()) {
            emit requestFinished(id, false, QJsonObject(), QStringLiteral("图纸数据为空"));
            return;
        }
        const QString path = m_tempDir->filePath(fileName);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit requestFinished(id, false, QJsonObject(), QStringLiteral("临时文件写入失败"));
            return;
        }
        file.write(QByteArray::fromBase64(base64.toLatin1()));
        file.close();
        QJsonObject d;
        d.insert(QStringLiteral("tempFilePath"), path);
        data = d;
    }
    emit requestFinished(id, true, data, QString());
}

// ---- 异步业务 API ----

qint64 RemoteClient::listDirAsync(qint64 nodeId)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqListDir), params);
}

qint64 RemoteClient::searchAsync(qint64 rootNodeId, const QString &keyword)
{
    QJsonObject params;
    params.insert(QStringLiteral("rootNodeId"), rootNodeId);
    params.insert(QStringLiteral("keyword"), keyword);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqSearch), params);
}

qint64 RemoteClient::getNodeAsync(qint64 nodeId)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqGetNode), params);
}

qint64 RemoteClient::getPathAsync(qint64 nodeId, qint64 stopAtId)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("stopAtId"), stopAtId);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqGetPath), params);
}

qint64 RemoteClient::createComponentAsync(qint64 parentId, const QString &name,
                                          const QString &partNo, int quantity,
                                          const QString &remark)
{
    QJsonObject params;
    params.insert(QStringLiteral("parentId"), parentId);
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("partNo"), partNo);
    params.insert(QStringLiteral("quantity"), quantity);
    params.insert(QStringLiteral("remark"), remark);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqCreateComponent), params);
}

qint64 RemoteClient::createPartAsync(qint64 parentId, const QString &name, const QString &partNo,
                                     const QString &material, int quantity,
                                     const QString &remark)
{
    QJsonObject params;
    params.insert(QStringLiteral("parentId"), parentId);
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("partNo"), partNo);
    params.insert(QStringLiteral("material"), material);
    params.insert(QStringLiteral("quantity"), quantity);
    params.insert(QStringLiteral("remark"), remark);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqCreatePart), params);
}

qint64 RemoteClient::renameNodeAsync(qint64 nodeId, const QString &newName)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("newName"), newName);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqRenameNode), params);
}

qint64 RemoteClient::updatePartNoAsync(qint64 nodeId, const QString &newPartNo)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("newPartNo"), newPartNo);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqUpdatePartNo), params);
}

qint64 RemoteClient::updatePartAttributesAsync(qint64 nodeId, const QString &material, int quantity)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("material"), material);
    params.insert(QStringLiteral("quantity"), quantity);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqUpdatePartAttrs), params);
}

qint64 RemoteClient::updateComponentQuantityAsync(qint64 nodeId, int quantity)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("quantity"), quantity);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqUpdateComponentQty), params);
}

qint64 RemoteClient::updateNodeRemarkAsync(qint64 nodeId, const QString &remark)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("remark"), remark);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqUpdateNodeRemark), params);
}

qint64 RemoteClient::deleteNodeAsync(qint64 nodeId)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqDeleteNode), params);
}

qint64 RemoteClient::moveNodeAsync(qint64 nodeId, qint64 newParentId)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("newParentId"), newParentId);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqMoveNode), params);
}

qint64 RemoteClient::copyNodeAsync(qint64 nodeId, qint64 newParentId, const QString &newName,
                                   const QString &forcedPartNo)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("newParentId"), newParentId);
    params.insert(QStringLiteral("newName"), newName);
    params.insert(QStringLiteral("forcedPartNo"), forcedPartNo);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqCopyNode), params);
}

qint64 RemoteClient::isPartNoOccupiedAsync(NodeType type, const QString &partNo,
                                           qint64 targetParentId, qint64 excludeNodeId)
{
    QJsonObject params;
    params.insert(QStringLiteral("type"), static_cast<int>(type));
    params.insert(QStringLiteral("partNo"), partNo);
    params.insert(QStringLiteral("targetParentId"), targetParentId);
    params.insert(QStringLiteral("excludeNodeId"), excludeNodeId);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqIsPartNoOccupied), params);
}

qint64 RemoteClient::isValidPartNoFormatAsync(NodeType type, const QString &partNo)
{
    QJsonObject params;
    params.insert(QStringLiteral("type"), static_cast<int>(type));
    params.insert(QStringLiteral("partNo"), partNo);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqIsValidPartNo), params);
}

qint64 RemoteClient::computeFullPartNoAsync(qint64 nodeId)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqComputeFullPartNo), params);
}

qint64 RemoteClient::loadPartAsync(qint64 nodeId)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqLoadPart), params);
}

qint64 RemoteClient::loadComponentAsync(qint64 nodeId)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqLoadComponent), params);
}

qint64 RemoteClient::importPdfAsync(qint64 partNodeId, const QString &sourceFilePath)
{
    QFile file(sourceFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        const qint64 id = ++m_requestId;
        const QString err = QStringLiteral("读取本地文件失败：%1").arg(sourceFilePath);
        QTimer::singleShot(0, this, [this, id, err]() {
            emit requestFinished(id, false, QJsonObject(), err);
        });
        return id;
    }
    const QByteArray fileData = file.readAll();
    file.close();
    QJsonObject params;
    params.insert(QStringLiteral("partNodeId"), partNodeId);
    params.insert(QStringLiteral("fileName"), QFileInfo(sourceFilePath).fileName());
    params.insert(QStringLiteral("data"), QString::fromLatin1(fileData.toBase64()));
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqImportPdf), params);
}

qint64 RemoteClient::setCurrentDrawingAsync(qint64 partNodeId, qint64 drawingId)
{
    QJsonObject params;
    params.insert(QStringLiteral("partNodeId"), partNodeId);
    params.insert(QStringLiteral("drawingId"), drawingId);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqSetCurrentDrawing), params);
}

qint64 RemoteClient::deleteDrawingAsync(qint64 drawingId)
{
    QJsonObject params;
    params.insert(QStringLiteral("drawingId"), drawingId);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqDeleteDrawing), params);
}

qint64 RemoteClient::fetchDrawingFileAsync(qint64 drawingId)
{
    QJsonObject params;
    params.insert(QStringLiteral("drawingId"), drawingId);
    return sendRequestAsync(QLatin1String(RemoteProtocol::kReqGetDrawingFile), params);
}

// ---- 目录 / 节点 ----

bool RemoteClient::listDir(qint64 nodeId, QVector<DirectoryItem> &items, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    QJsonObject response;
    if (!sendRequest(QLatin1String(RemoteProtocol::kReqListDir), params, response, error)) {
        return false;
    }
    items.clear();
    const QJsonArray arr = response.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : arr) {
        items.append(RemoteProtocol::directoryItemFromJson(value.toObject()));
    }
    return true;
}

bool RemoteClient::search(qint64 rootNodeId, const QString &keyword,
                          QVector<DirectoryItem> &items, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("rootNodeId"), rootNodeId);
    params.insert(QStringLiteral("keyword"), keyword);
    QJsonObject response;
    if (!sendRequest(QLatin1String(RemoteProtocol::kReqSearch), params, response, error)) {
        return false;
    }
    items.clear();
    const QJsonArray arr = response.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : arr) {
        items.append(RemoteProtocol::directoryItemFromJson(value.toObject()));
    }
    return true;
}

bool RemoteClient::getNode(qint64 nodeId, HFADMNode &node, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    QJsonObject response;
    if (!sendRequest(QLatin1String(RemoteProtocol::kReqGetNode), params, response, error)) {
        return false;
    }
    node = RemoteProtocol::nodeFromJson(response.value(QStringLiteral("node")).toObject());
    return true;
}

bool RemoteClient::getPath(qint64 nodeId, qint64 stopAtId, QString &path, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("stopAtId"), stopAtId);
    QJsonObject response;
    if (!sendRequest(QLatin1String(RemoteProtocol::kReqGetPath), params, response, error)) {
        return false;
    }
    path = response.value(QStringLiteral("path")).toString();
    return true;
}

// ---- 写操作 ----

bool RemoteClient::createComponent(qint64 parentId, const QString &name,
                                   const QString &partNo, int quantity, QString *error,
                                   const QString &remark)
{
    QJsonObject params;
    params.insert(QStringLiteral("parentId"), parentId);
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("partNo"), partNo);
    params.insert(QStringLiteral("quantity"), quantity);
    params.insert(QStringLiteral("remark"), remark);
    QJsonObject response;
    return sendRequest(QLatin1String(RemoteProtocol::kReqCreateComponent), params, response, error);
}

bool RemoteClient::createPart(qint64 parentId, const QString &name, const QString &partNo,
                              const QString &material, int quantity, qint64 *newNodeId,
                              QString *error, const QString &remark)
{
    QJsonObject params;
    params.insert(QStringLiteral("parentId"), parentId);
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("partNo"), partNo);
    params.insert(QStringLiteral("material"), material);
    params.insert(QStringLiteral("quantity"), quantity);
    params.insert(QStringLiteral("remark"), remark);
    QJsonObject response;
    if (!sendRequest(QLatin1String(RemoteProtocol::kReqCreatePart), params, response, error)) {
        return false;
    }
    if (newNodeId) {
        *newNodeId = response.value(QStringLiteral("nodeId")).toVariant().toLongLong();
    }
    return true;
}

bool RemoteClient::renameNode(qint64 nodeId, const QString &newName, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("newName"), newName);
    QJsonObject response;
    return sendRequest(QLatin1String(RemoteProtocol::kReqRenameNode), params, response, error);
}

bool RemoteClient::updatePartNo(qint64 nodeId, const QString &newPartNo, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("newPartNo"), newPartNo);
    QJsonObject response;
    return sendRequest(QLatin1String(RemoteProtocol::kReqUpdatePartNo), params, response, error);
}

bool RemoteClient::updatePartAttributes(qint64 nodeId, const QString &material, int quantity,
                                        QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("material"), material);
    params.insert(QStringLiteral("quantity"), quantity);
    QJsonObject response;
    return sendRequest(QLatin1String(RemoteProtocol::kReqUpdatePartAttrs), params, response, error);
}

bool RemoteClient::updateComponentQuantity(qint64 nodeId, int quantity, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("quantity"), quantity);
    QJsonObject response;
    return sendRequest(QLatin1String(RemoteProtocol::kReqUpdateComponentQty), params, response, error);
}

bool RemoteClient::updateNodeRemark(qint64 nodeId, const QString &remark, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("remark"), remark);
    QJsonObject response;
    return sendRequest(QLatin1String(RemoteProtocol::kReqUpdateNodeRemark), params, response, error);
}

bool RemoteClient::deleteNode(qint64 nodeId, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    QJsonObject response;
    return sendRequest(QLatin1String(RemoteProtocol::kReqDeleteNode), params, response, error);
}

bool RemoteClient::moveNode(qint64 nodeId, qint64 newParentId, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("newParentId"), newParentId);
    QJsonObject response;
    return sendRequest(QLatin1String(RemoteProtocol::kReqMoveNode), params, response, error);
}

bool RemoteClient::copyNode(qint64 nodeId, qint64 newParentId, const QString &newName,
                            const QString &forcedPartNo, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("newParentId"), newParentId);
    params.insert(QStringLiteral("newName"), newName);
    params.insert(QStringLiteral("forcedPartNo"), forcedPartNo);
    QJsonObject response;
    return sendRequest(QLatin1String(RemoteProtocol::kReqCopyNode), params, response, error);
}

// ---- 图号 ----

bool RemoteClient::isPartNoOccupied(NodeType type, const QString &partNo,
                                    qint64 targetParentId, qint64 excludeNodeId,
                                    QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("type"), static_cast<int>(type));
    params.insert(QStringLiteral("partNo"), partNo);
    params.insert(QStringLiteral("targetParentId"), targetParentId);
    params.insert(QStringLiteral("excludeNodeId"), excludeNodeId);
    QJsonObject response;
    if (!sendRequest(QLatin1String(RemoteProtocol::kReqIsPartNoOccupied), params, response, error)) {
        return false;
    }
    return response.value(QStringLiteral("occupied")).toBool();
}

bool RemoteClient::isValidPartNoFormat(NodeType type, const QString &partNo, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("type"), static_cast<int>(type));
    params.insert(QStringLiteral("partNo"), partNo);
    QJsonObject response;
    if (!sendRequest(QLatin1String(RemoteProtocol::kReqIsValidPartNo), params, response, error)) {
        return false;
    }
    return response.value(QStringLiteral("valid")).toBool();
}

bool RemoteClient::computeFullPartNo(qint64 nodeId, QString &full, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    QJsonObject response;
    if (!sendRequest(QLatin1String(RemoteProtocol::kReqComputeFullPartNo), params, response, error)) {
        return false;
    }
    full = response.value(QStringLiteral("full")).toString();
    return true;
}

// ---- 属性 ----

bool RemoteClient::loadPart(qint64 nodeId, Part &part, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    QJsonObject response;
    if (!sendRequest(QLatin1String(RemoteProtocol::kReqLoadPart), params, response, error)) {
        return false;
    }
    part = RemoteProtocol::partFromJson(response.value(QStringLiteral("part")).toObject());
    return true;
}

bool RemoteClient::loadComponent(qint64 nodeId, Component &component, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("nodeId"), nodeId);
    QJsonObject response;
    if (!sendRequest(QLatin1String(RemoteProtocol::kReqLoadComponent), params, response, error)) {
        return false;
    }
    component = RemoteProtocol::componentFromJson(
        response.value(QStringLiteral("component")).toObject());
    return true;
}

// ---- 图纸 ----

bool RemoteClient::importPdf(qint64 partNodeId, const QString &sourceFilePath, QString *error)
{
    QFile file(sourceFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("读取本地文件失败：%1").arg(sourceFilePath);
        }
        return false;
    }
    QJsonObject params;
    params.insert(QStringLiteral("partNodeId"), partNodeId);
    params.insert(QStringLiteral("fileName"), QFileInfo(sourceFilePath).fileName());
    params.insert(QStringLiteral("data"), QString::fromLatin1(file.readAll().toBase64()));
    QJsonObject response;
    return sendRequest(QLatin1String(RemoteProtocol::kReqImportPdf), params, response, error);
}

bool RemoteClient::setCurrentDrawing(qint64 partNodeId, qint64 drawingId, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("partNodeId"), partNodeId);
    params.insert(QStringLiteral("drawingId"), drawingId);
    QJsonObject response;
    return sendRequest(QLatin1String(RemoteProtocol::kReqSetCurrentDrawing), params, response, error);
}

bool RemoteClient::deleteDrawing(qint64 drawingId, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("drawingId"), drawingId);
    QJsonObject response;
    return sendRequest(QLatin1String(RemoteProtocol::kReqDeleteDrawing), params, response, error);
}

bool RemoteClient::fetchDrawingFile(const Drawing &drawing, QString &tempFilePath, QString *error)
{
    // 桥接异步核心：dispatchBusinessResponse 已将 base64 写入临时目录，data 含 tempFilePath
    QJsonObject params;
    params.insert(QStringLiteral("drawingId"), drawing.id);
    QJsonObject response;
    if (!sendRequest(QLatin1String(RemoteProtocol::kReqGetDrawingFile), params, response, error)) {
        return false;
    }
    tempFilePath = response.value(QStringLiteral("tempFilePath")).toString();
    if (tempFilePath.isEmpty()) {
        if (error) {
            *error = QStringLiteral("图纸数据为空");
        }
        return false;
    }
    return true;
}
