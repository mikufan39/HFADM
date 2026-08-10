#include "remoteserver.h"
#include "remoteprotocol.h"
#include "crypto.h"
#include "service/appconfig.h"
#include "database/databasemanager.h"
#include "service/drawingservice.h"
#include "service/nodeservice.h"
#include "service/projectservice.h"
#include "ui/directoryassembler.h"
#include "ui/tabmanager.h"

#include <QDateTime>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>

namespace {
// 帧 = 单行 JSON + '\n'
QByteArray frameBytes(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}
} // namespace

// 单个远程连接：握手阶段处理配对/认证控制帧，认证后业务帧全程 AES-GCM 加密。
class RemoteConnection : public QObject
{
    Q_OBJECT

public:
    RemoteConnection(QTcpSocket *socket, RemoteServer *server, QObject *parent = nullptr)
        : QObject(parent)
        , m_socket(socket)
        , m_server(server)
        , m_peerAddressCached(socket->peerAddress().toString())
    {
        // 接管 socket 生命周期：nextPendingConnection 的 socket 原本挂在 QTcpServer 下，
        // 若随 server 析构会被提前删除，导致本对象的 m_socket 悬空。
        m_socket->setParent(this);
        m_lastReceive.start();
        connect(m_socket, &QTcpSocket::readyRead, this, &RemoteConnection::onReadyRead);
        connect(m_socket, &QTcpSocket::disconnected, this, &RemoteConnection::onDisconnected);

        m_silenceTimer = new QTimer(this);
        m_silenceTimer->setInterval(5000);
        connect(m_silenceTimer, &QTimer::timeout, this, &RemoteConnection::checkSilence);
        m_silenceTimer->start();
    }

    // socket 已由 setParent(this) 接管：随本对象由 QObject 自动销毁，
    // 切勿在析构中 deleteLater/手动删除（DeferredDelete 销毁路径下会悬空崩溃）

    QString peerAddress() const
    {
        // 使用构造时缓存的地址：断开后 socket->peerAddress() 会失效，提示仍可用
        return m_peerAddressCached;
    }

    // 认证/配对成功后绑定到的设备 uuid（未认证时为空；供按设备断开连接）
    QString deviceId() const
    {
        return m_deviceId;
    }

    // 主动断开该连接：断开 socket 并触发 onDisconnected 统一清理
    void disconnectRemote()
    {
        if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->disconnectFromHost();
        }
    }

    void sendFrame(const QJsonObject &obj)
    {
        if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->write(frameBytes(obj));
        }
    }

    // 明文控制响应（握手阶段）
    void sendControlResponse(qint64 id, bool success, const QJsonObject &body)
    {
        QJsonObject resp;
        resp.insert(QStringLiteral("type"), QStringLiteral("response"));
        resp.insert(QStringLiteral("id"), id);
        resp.insert(QStringLiteral("success"), success);
        resp.insert(QStringLiteral("body"), body);
        sendFrame(resp);
    }

    // 加密业务响应（认证后）
    void sendEncryptedResponse(qint64 id, bool success,
                               const QJsonObject &data, const QString &message)
    {
        QJsonObject payload;
        payload.insert(QStringLiteral("success"), success);
        if (success) {
            payload.insert(QStringLiteral("data"), data);
        } else {
            payload.insert(QStringLiteral("message"), message);
        }
        QJsonObject body;
        if (!RemoteProtocol::encryptBody(m_sessionKey, id, payload, body)) {
            // 加密失败：连接已不可信，断开
            if (m_socket) {
                m_socket->disconnectFromHost();
            }
            return;
        }
        QJsonObject resp;
        resp.insert(QStringLiteral("type"), QStringLiteral("response"));
        resp.insert(QStringLiteral("id"), id);
        resp.insert(QStringLiteral("body"), body);
        sendFrame(resp);
    }

private slots:
    void onReadyRead()
    {
        m_lastReceive.restart();
        m_buffer.append(m_socket->readAll());
        int newline = -1;
        while ((newline = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(newline);
            m_buffer.remove(0, newline + 1);
            processLine(line);
        }
    }

    void onDisconnected()
    {
        // 客户端断开：若服务端正挂起配对确认弹窗（handleConnect 阻塞在其 exec 中），
        // 必须先关闭弹窗让处理栈安全退出，再删除连接对象，避免悬空访问崩溃
        m_server->cancelActivePairing(peerAddress());
        m_server->removeConnection(this);
    }

    void checkSilence()
    {
        if (m_lastReceive.elapsed() > RemoteProtocol::kSilenceTimeoutMs) {
            // 静默超时断开：同样先关挂起弹窗再清理，防止弹窗 exec 期间对象被删
            m_server->cancelActivePairing(peerAddress());
            if (m_socket) {
                m_socket->disconnectFromHost();
                if (m_socket->state() == QAbstractSocket::UnconnectedState) {
                    m_server->removeConnection(this);
                }
            }
        }
    }

private:
    void processLine(const QByteArray &line)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            return;
        }
        const QJsonObject obj = doc.object();
        const QString type = obj.value(QStringLiteral("type")).toString();
        if (type != QLatin1String("request")) {
            return; // 服务端只处理请求帧
        }
        const qint64 id = obj.value(QStringLiteral("id")).toVariant().toLongLong();
        const QString cmd = obj.value(QStringLiteral("cmd")).toString();
        const QJsonObject body = obj.value(QStringLiteral("body")).toObject();

        if (!m_authenticated) {
            handleControl(id, cmd, body);
        } else {
            handleBusiness(id, cmd, body);
        }
    }

    // ---- 握手阶段：配对 / 认证（明文）----
    void handleControl(qint64 id, const QString &cmd, const QJsonObject &body)
    {
        if (cmd == QLatin1String(RemoteProtocol::kCmdConnect)) {
            handleConnect(id, body);
        } else if (cmd == QLatin1String(RemoteProtocol::kCmdAuth)) {
            handleAuthStart(id, body);
        } else if (cmd == QLatin1String(RemoteProtocol::kCmdAuthResp)) {
            handleAuthResp(id, body);
        } else if (cmd == QLatin1String(RemoteProtocol::kCmdCancelPair)) {
            handleCancelPair(id, body);
        }
        // 未知控制命令：忽略
    }

    // 客户端取消配对：关闭服务端挂起的确认弹窗，双方不记录任何结果
    void handleCancelPair(qint64 id, const QJsonObject &body)
    {
        Q_UNUSED(body);
        m_server->cancelActivePairing(peerAddress());
        sendControlResponse(id, false, {{QStringLiteral("message"),
            QStringLiteral("配对已取消")}});
        // 客户端随后断开；若未断开则主动断开
        if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->disconnectFromHost();
        }
    }

    void handleConnect(qint64 id, const QJsonObject &body)
    {
        const QString uuid = body.value(QStringLiteral("uuid")).toString();
        const QString deviceName = body.value(QStringLiteral("deviceName")).toString();
        const QString pin = body.value(QStringLiteral("pin")).toString();
        const QByteArray key = QByteArray::fromBase64(
            body.value(QStringLiteral("key")).toString().toLatin1());

        if (uuid.isEmpty() || key.size() != 16) {
            sendControlResponse(id, false, {{QStringLiteral("message"),
                QStringLiteral("配对请求参数无效")}});
            return;
        }
        // 黑名单：被「拒绝且不再提示」/权限设为「不允许的连接」的设备直接拒绝，不再弹窗确认
        if (AppConfig::isIgnoredDevice(uuid)) {
            sendControlResponse(id, false, {{QStringLiteral("message"),
                QStringLiteral("该设备已被禁止连接，请在服务端“授权管理”中解除")}});
            return;
        }
        // 已授权设备不允许重复配对（应直接走认证；如需重授权请先删除）
        RemoteDevice existing;
        if (m_server->findDevice(uuid, existing)) {
            if (existing.permission == RemoteProtocol::Permission::Denied) {
                // 设备记录为「不允许的连接」（状态一致性兜底，正常路径已被黑名单检查拦截）
                sendControlResponse(id, false, {{QStringLiteral("message"),
                    QStringLiteral("该设备已被禁止连接，请在服务端“授权管理”中解除")}});
            } else {
                sendControlResponse(id, false, {{QStringLiteral("message"),
                    QStringLiteral("设备已授权，请直接连接；如需重新授权请先在服务端删除该设备")}});
            }
            return;
        }

        // 并发保护：同一时刻只放行一个配对确认弹窗；
        // 其他并发配对请求暂时拒绝（不加入黑名单），避免弹窗堆积难以处理
        if (m_server->isPairingPending()) {
            sendControlResponse(id, false, {{QStringLiteral("message"),
                QStringLiteral("当前有配对请求正在处理中，请稍后重试")}});
            return;
        }

        RemoteProtocol::PairingRequest req{uuid, deviceName, pin, peerAddress()};
        m_server->setPairingPending(true);
        const RemoteProtocol::PairingResult result = m_server->resolvePairing(req);
        m_server->setPairingPending(false);
        if (!result.accepted) {
            // 「拒绝且不再提示」：持久化黑名单，后续该设备直接拒绝不再弹窗
            if (result.neverAskAgain) {
                AppConfig::addIgnoredDevice(uuid);
            }
            sendControlResponse(id, false, {{QStringLiteral("message"),
                QStringLiteral("服务端拒绝了配对请求")}});
            return;
        }

        // 保存设备记录
        RemoteDevice device;
        device.uuid = uuid;
        device.deviceName = deviceName.isEmpty() ? peerAddress() : deviceName;
        device.aesKey = key;
        device.permission = result.permission;
        device.createdAt = QDateTime::currentDateTime();
        device.lastSeen = device.createdAt;
        if (!m_server->saveNewDevice(device)) {
            sendControlResponse(id, false, {{QStringLiteral("message"),
                QStringLiteral("保存设备记录失败")}});
            return;
        }

        // 配对成功即建立加密会话
        m_sessionKey = key;
        m_permission = result.permission;
        m_authenticated = true;
        m_deviceId = uuid;
        emit m_server->deviceListChanged();

        QJsonObject respBody = m_server->buildHandshakeBody(result.permission);
        respBody.insert(QStringLiteral("message"), QStringLiteral("已授权"));
        sendControlResponse(id, true, respBody);
    }

    void handleAuthStart(qint64 id, const QJsonObject &body)
    {
        const QString uuid = body.value(QStringLiteral("uuid")).toString();
        RemoteDevice device;
        if (!m_server->findDevice(uuid, device)) {
            // 未授权设备：直接返回失败（不发挑战）
            sendControlResponse(id, false, {{QStringLiteral("message"),
                QStringLiteral("设备未授权")}});
            return;
        }
        // 黑名单为行为真相源：uuid 在黑名单中（无论表内权限是否已同步为 Denied，
        // 例如改权限时数据库写失败导致状态不一致）一律拒绝认证
        if (AppConfig::isIgnoredDevice(uuid)) {
            sendControlResponse(id, false, {{QStringLiteral("message"),
                QStringLiteral("该设备已被禁止连接，请在服务端“授权管理”中解除")}});
            return;
        }
        // 「不允许的连接」设备：即使凭证未删也不得认证
        if (device.permission == RemoteProtocol::Permission::Denied) {
            sendControlResponse(id, false, {{QStringLiteral("message"),
                QStringLiteral("该设备已被禁止连接，请在服务端“授权管理”中解除")}});
            return;
        }
        // 发起挑战：生成随机 nonce
        m_pendingAuthUuid = uuid;
        m_pendingAuthKey = device.aesKey;
        m_pendingAuthNonce = AesGcm::generateNonce();
        m_pendingAuthPermission = device.permission;
        QJsonObject body2;
        body2.insert(QStringLiteral("nonce"),
                     QString::fromLatin1(m_pendingAuthNonce.toBase64()));
        // 挑战是响应帧（无 success 字段，body 直接含 nonce）
        QJsonObject resp;
        resp.insert(QStringLiteral("type"), QStringLiteral("response"));
        resp.insert(QStringLiteral("id"), id);
        resp.insert(QStringLiteral("body"), body2);
        sendFrame(resp);
    }

    void handleAuthResp(qint64 id, const QJsonObject &body)
    {
        const QByteArray nonce = QByteArray::fromBase64(
            body.value(QStringLiteral("nonce")).toString().toLatin1());
        const QByteArray cipher = QByteArray::fromBase64(
            body.value(QStringLiteral("ciphertext")).toString().toLatin1());
        const QByteArray tag = QByteArray::fromBase64(
            body.value(QStringLiteral("tag")).toString().toLatin1());

        if (m_pendingAuthKey.isEmpty() || nonce != m_pendingAuthNonce) {
            sendControlResponse(id, false, {{QStringLiteral("message"),
                QStringLiteral("认证失败：挑战不匹配")}});
            m_pendingAuthKey.clear();
            return;
        }
        // AAD 绑定帧 id；明文应为挑战 nonce 本身
        const QByteArray aad = QByteArray::number(id);
        QByteArray plain;
        if (!AesGcm::decrypt(m_pendingAuthKey, nonce, cipher, aad, tag, plain)
            || plain != m_pendingAuthNonce) {
            sendControlResponse(id, false, {{QStringLiteral("message"),
                QStringLiteral("认证失败：密钥不匹配或数据已篡改")}});
            m_pendingAuthKey.clear();
            return;
        }

        // 认证通过：建立加密会话
        m_sessionKey = m_pendingAuthKey;
        m_permission = m_pendingAuthPermission;
        m_authenticated = true;
        m_deviceId = m_pendingAuthUuid;
        m_pendingAuthKey.clear();
        m_server->touchDeviceLastSeen(m_deviceId);

        QJsonObject respBody = m_server->buildHandshakeBody(m_permission);
        respBody.insert(QStringLiteral("message"), QStringLiteral("已认证"));
        sendControlResponse(id, true, respBody);
    }

    // ---- 业务阶段：AES-GCM 加密分发 ----
    void handleBusiness(qint64 id, const QString &cmd, const QJsonObject &body)
    {
        // 防重放：业务帧 id 必须严格递增
        if (id <= m_lastSeq) {
            return; // 丢弃重放/乱序帧，不响应
        }
        m_lastSeq = id;

        QJsonObject params;
        if (!RemoteProtocol::decryptBody(m_sessionKey, id, body, params)) {
            // 解密失败：连接不可信，断开
            if (m_socket) {
                m_socket->disconnectFromHost();
            }
            return;
        }

        // 权限拦截：不允许的连接（纵深防御，正常路径在握手阶段已拒绝）
        if (RemoteProtocol::isDenied(m_permission)) {
            sendEncryptedResponse(id, false, QJsonObject(),
                                  QStringLiteral("权限不足：该设备已被禁止连接"));
            return;
        }
        // 只读权限拦截写操作
        if (RemoteProtocol::isWriteCommand(cmd) && m_permission == RemoteProtocol::Permission::ReadOnly) {
            sendEncryptedResponse(id, false, QJsonObject(),
                                  QStringLiteral("权限不足：当前为只读授权"));
            return;
        }

        // 写操作幂等去重：命中则原样返回首次响应，不重复执行（防超时重试重复创建）
        if (RemoteProtocol::isWriteCommand(cmd)) {
            const QString idKey = params.value(QStringLiteral("idempotencyKey")).toString();
            if (!idKey.isEmpty()) {
                QJsonObject cached;
                if (m_server->getIdempotencyResult(m_deviceId, idKey, cached)) {
                    const bool ok = cached.value(QStringLiteral("success")).toBool(false);
                    sendEncryptedResponse(id, ok,
                                          cached.value(QStringLiteral("data")).toObject(),
                                          cached.value(QStringLiteral("message")).toString());
                    return;
                }
                // 未命中：执行并记录首次响应，后续相同键重试直接返回此结果
                QJsonObject request = params;
                request.insert(QStringLiteral("req"), cmd);
                request.insert(QStringLiteral("id"), id);
                QJsonObject data;
                QString message;
                const bool ok = m_server->handleRequest(request, data, message);
                QJsonObject payload;
                payload.insert(QStringLiteral("success"), ok);
                if (ok) {
                    payload.insert(QStringLiteral("data"), data);
                } else {
                    payload.insert(QStringLiteral("message"), message);
                }
                m_server->insertIdempotencyResult(m_deviceId, idKey, payload);
                if (ok) {
                    sendEncryptedResponse(id, true, data, QString());
                } else {
                    sendEncryptedResponse(id, false, QJsonObject(), message);
                }
                return;
            }
        }

        // 重建旧式请求对象交给现有 handleRequest（保持业务逻辑不变）
        QJsonObject request = params;
        request.insert(QStringLiteral("req"), cmd);
        request.insert(QStringLiteral("id"), id);

        QJsonObject data;
        QString message;
        if (m_server->handleRequest(request, data, message)) {
            sendEncryptedResponse(id, true, data, QString());
        } else {
            sendEncryptedResponse(id, false, QJsonObject(), message);
        }
    }

    QTcpSocket *m_socket = nullptr;
    RemoteServer *m_server = nullptr;
    QString m_peerAddressCached; // 构造时缓存的来源 IP（断开后仍可用）
    QByteArray m_buffer;
    QElapsedTimer m_lastReceive;
    QTimer *m_silenceTimer = nullptr;

    // 会话状态
    bool m_authenticated = false;
    QByteArray m_sessionKey;            // 16 字节会话密钥
    RemoteProtocol::Permission m_permission = RemoteProtocol::Permission::ReadOnly;
    QString m_deviceId;
    qint64 m_lastSeq = 0;               // 防重放：已处理的最大业务帧 id

    // 认证挑战临时态
    QString m_pendingAuthUuid;
    QByteArray m_pendingAuthKey;
    QByteArray m_pendingAuthNonce;
    RemoteProtocol::Permission m_pendingAuthPermission = RemoteProtocol::Permission::ReadOnly;
};

// ---------------- RemoteServer ----------------

RemoteServer::RemoteServer(ProjectService *projectService, NodeService *nodeService,
                           DrawingService *drawingService, QObject *parent)
    : QObject(parent)
    , m_projectService(projectService)
    , m_nodeService(nodeService)
    , m_drawingService(drawingService)
{
}

RemoteServer::~RemoteServer()
{
    stop();
}

void RemoteServer::setPairingResolver(std::function<PairingResult(const PairingRequest &)> resolver)
{
    m_pairingResolver = std::move(resolver);
}

RemoteProtocol::PairingResult RemoteServer::resolvePairing(const RemoteProtocol::PairingRequest &req)
{
    if (m_pairingResolver) {
        return m_pairingResolver(req);
    }
    // 默认拒绝（未配置解析器时）
    return PairingResult{};
}

void RemoteServer::setActivePairingDialog(QDialog *dialog)
{
    m_activePairingDialog = dialog;
}

void RemoteServer::cancelActivePairing(const QString &address)
{
    if (!m_activePairingDialog) {
        return; // 无挂起弹窗（如认证中连接断开）：不提示、不处理
    }
    // 关闭挂起的配对确认弹窗：exec() 返回 Rejected → 配对按「拒绝且不记录」处理
    m_activePairingDialog->reject();
    m_activePairingDialog = nullptr;
    emit pairingCancelled(address);
}

bool RemoteServer::start(const QString &projectPath, const QString &projectName, QString *error)
{
    stop();
    m_boundProjectPath = projectPath;
    m_boundProjectName = projectName;
    m_boundRootNodeId = 0;

    // 先切到绑定项目并查询机型根节点 id（握手响应返回给客户端作为初始浏览根）
    qint64 rootId = 0;
    if (!withProjectContext([&]() -> bool {
            QSqlQuery q(m_projectService->databaseManager()->database());
            if (q.exec(QStringLiteral("SELECT id FROM node WHERE type=1 AND deleted=0 LIMIT 1"))
                && q.next()) {
                rootId = q.value(0).toLongLong();
            }
            return rootId != 0;
        }, error)) {
        m_boundProjectPath.clear();
        m_boundProjectName.clear();
        if (error && error->isEmpty()) {
            *error = QStringLiteral("无法定位绑定项目的机型根节点");
        }
        return false;
    }
    m_boundRootNodeId = rootId;

    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::AnyIPv4, RemoteProtocol::kPort)) {
        if (error) {
            *error = QStringLiteral("监听端口 %1 失败：%2")
                         .arg(RemoteProtocol::kPort)
                         .arg(m_server->errorString());
        }
        delete m_server;
        m_server = nullptr;
        m_boundProjectPath.clear();
        m_boundProjectName.clear();
        m_boundRootNodeId = 0;
        return false;
    }
    connect(m_server, &QTcpServer::newConnection, this, &RemoteServer::onNewConnection);
    emit stateChanged(true);
    return true;
}

void RemoteServer::stop()
{
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    // 若有挂起的配对确认弹窗（handleConnect 阻塞在其 exec 中），先关闭让处理栈
    // 安全退出，再清理连接；服务端主动停止不提示「取消连接」
    if (m_activePairingDialog) {
        m_activePairingDialog->reject();
        m_activePairingDialog = nullptr;
    }
    const QList<RemoteConnection *> conns = m_connections;
    for (RemoteConnection *conn : conns) {
        removeConnection(conn);
    }
    if (!m_boundProjectPath.isEmpty() || !m_boundProjectName.isEmpty()
        || m_boundRootNodeId != 0) {
        m_boundProjectPath.clear();
        m_boundProjectName.clear();
        m_boundRootNodeId = 0;
        emit stateChanged(false);
    }
}

bool RemoteServer::isRunning() const
{
    return m_server != nullptr && m_server->isListening();
}

QString RemoteServer::projectPath() const
{
    return m_boundProjectPath;
}

QString RemoteServer::projectName() const
{
    return m_boundProjectName;
}

qint64 RemoteServer::projectRootNodeId() const
{
    return m_boundRootNodeId;
}

int RemoteServer::connectionCount() const
{
    return m_connections.size();
}

QStringList RemoteServer::localAddresses()
{
    QStringList result;
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &addr : addresses) {
        if (addr.protocol() != QAbstractSocket::IPv4Protocol || addr.isLoopback()) {
            continue;
        }
        const quint32 v = addr.toIPv4Address();
        // 仅展示 192.168.0.0/16 段（与客户端地址校验一致）
        if ((v & 0xFFFF0000u) == 0xC0A80000u) {
            result.append(addr.toString());
        }
    }
    return result;
}

bool RemoteServer::listAuthorizedDevices(QVector<RemoteDevice> &devices)
{
    devices.clear();
    // 全局设备授权存储：设备记录与当前开放机型无关
    if (!AppConfig::listDevices(devices)) {
        return false;
    }
    // 合并黑名单设备：被「拒绝且不再提示」/权限改为「不允许的连接」的设备统一显示在列表中
    // 1) 表内记录若在黑名单中，权限统一视为 Denied（以黑名单为行为真相源，避免状态不一致）
    // 2) 仅黑名单（配对被拒、无表记录）的设备合成条目，标记 ignoredOnly
    const QStringList ignored = AppConfig::ignoredDevices();
    for (RemoteDevice &d : devices) {
        if (ignored.contains(d.uuid)) {
            d.permission = Permission::Denied;
        }
    }
    for (const QString &uuid : ignored) {
        if (uuid.isEmpty()) {
            continue;
        }
        bool inTable = false;
        for (const RemoteDevice &d : devices) {
            if (d.uuid == uuid) {
                inTable = true;
                break;
            }
        }
        if (!inTable) {
            RemoteDevice d;
            d.uuid = uuid;
            d.deviceName = QStringLiteral("已拒绝设备·%1").arg(uuid.left(8));
            d.permission = Permission::Denied;
            d.ignoredOnly = true;
            devices.append(d);
        }
    }
    // 排序：正常授权设备在前（按授权时间倒序），黑名单设备在后
    std::stable_sort(devices.begin(), devices.end(),
        [](const RemoteDevice &a, const RemoteDevice &b) {
            if (a.ignoredOnly != b.ignoredOnly) {
                return !a.ignoredOnly; // 非 ignoredOnly 排前
            }
            return a.createdAt > b.createdAt;
        });
    return true;
}

bool RemoteServer::deleteAuthorizedDevice(const QString &uuid)
{
    // 删除 = 撤销授权 + 解除禁止：无论设备状态如何，一律移出黑名单
    AppConfig::removeIgnoredDevice(uuid);
    const bool ok = AppConfig::deleteDevice(uuid);
    // 仅黑名单设备：无全局记录，deleteDevice 同样返回成功（键不存在即视为已删除）
    if (ok) {
        disconnectDevice(uuid); // 若该设备正在线，同步断开
        emit deviceListChanged();
    }
    return ok;
}

bool RemoteServer::updateDevicePermission(const QString &uuid, Permission permission)
{
    const bool denied = RemoteProtocol::isDenied(permission);
    // 1) 同步黑名单（行为真相源）：Denied 加入，否则移出
    if (denied) {
        AppConfig::addIgnoredDevice(uuid);
    } else {
        AppConfig::removeIgnoredDevice(uuid);
    }
    // 2) 全局记录更新权限（仅黑名单设备无记录，跳过）
    //    权限变更不影响当前在线会话，下次连接认证时按新权限生效
    bool ok = true;
    RemoteDevice device;
    if (AppConfig::getDevice(uuid, device)) {
        device.permission = permission;
        ok = AppConfig::saveDevice(device);
    }
    if (ok) {
        emit deviceListChanged();
    }
    return ok;
}

void RemoteServer::disconnectDevice(const QString &uuid)
{
    if (uuid.isEmpty()) {
        return;
    }
    const QList<RemoteConnection *> conns = m_connections;
    for (RemoteConnection *conn : conns) {
        if (conn->deviceId() == uuid) {
            conn->disconnectRemote(); // 断开后由 onDisconnected 统一清理
        }
    }
}

bool RemoteServer::renameAuthorizedDevice(const QString &uuid, const QString &newName)
{
    RemoteDevice device;
    if (!AppConfig::getDevice(uuid, device)) {
        return false;
    }
    device.deviceName = newName;
    const bool ok = AppConfig::saveDevice(device);
    if (ok) {
        emit deviceListChanged();
    }
    return ok;
}

bool RemoteServer::findDevice(const QString &uuid, RemoteDevice &device)
{
    return AppConfig::getDevice(uuid, device);
}

bool RemoteServer::saveNewDevice(const RemoteDevice &device)
{
    return AppConfig::saveDevice(device);
}

bool RemoteServer::touchDeviceLastSeen(const QString &uuid)
{
    return AppConfig::updateDeviceLastSeen(uuid, QDateTime::currentDateTime());
}

bool RemoteServer::getIdempotencyResult(const QString &deviceUuid, const QString &idKey,
                                        QJsonObject &payload)
{
    bool hit = false;
    withProjectContext([&]() -> bool {
        hit = m_projectService->databaseManager()->getIdempotencyResult(deviceUuid, idKey, payload);
        return true;
    }, nullptr);
    return hit;
}

bool RemoteServer::insertIdempotencyResult(const QString &deviceUuid, const QString &idKey,
                                           const QJsonObject &payload)
{
    bool ok = false;
    withProjectContext([&]() -> bool {
        ok = m_projectService->databaseManager()->insertIdempotencyResult(deviceUuid, idKey, payload);
        return true;
    }, nullptr);
    return ok;
}

QJsonObject RemoteServer::buildHandshakeBody(Permission permission) const
{
    QJsonObject body;
    body.insert(QStringLiteral("protocolVersion"), RemoteProtocol::kProtocolVersion);
    body.insert(QStringLiteral("projectName"), m_boundProjectName);
    body.insert(QStringLiteral("projectPath"), m_boundProjectPath);
    body.insert(QStringLiteral("projectRootNodeId"), m_boundRootNodeId);
    body.insert(QStringLiteral("permission"), static_cast<int>(permission));
    return body;
}

void RemoteServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        auto *conn = new RemoteConnection(socket, this);
        m_connections.append(conn);
        emit clientConnected(socket->peerAddress().toString());
        emit connectionCountChanged(m_connections.size());
    }
}

void RemoteServer::removeConnection(RemoteConnection *conn)
{
    if (!conn) {
        return;
    }
    const QString address = conn->peerAddress();
    m_connections.removeAll(conn);
    conn->deleteLater();
    emit clientDisconnected(address);
    emit connectionCountChanged(m_connections.size());
}

template <typename Fn>
bool RemoteServer::withProjectContext(Fn &&fn, QString *message)
{
    // 单线程事件循环：远程请求处理期间本地 UI 事件排队，上下文切换安全
    const QString saved = m_projectService ? m_projectService->currentProjectPath() : QString();
    const QString savedNodePath = m_nodeService ? m_nodeService->projectPath() : QString();
    const bool needSwitch = saved != m_boundProjectPath;
    if (needSwitch) {
        if (!m_projectService->openProject(m_boundProjectPath)) {
            if (message) {
                *message = m_projectService->lastError();
            }
            return false;
        }
        m_drawingService->setProjectPath(m_boundProjectPath);
        m_nodeService->setProjectPath(m_boundProjectPath);
    }
    const bool ok = fn();
    if (needSwitch && !saved.isEmpty()) {
        if (!m_projectService->openProject(saved)) {
            qWarning() << "RemoteServer: 恢复项目上下文失败" << saved;
        } else {
            m_drawingService->setProjectPath(saved);
        }
        m_nodeService->setProjectPath(savedNodePath);
    }
    return ok;
}

bool RemoteServer::handleRequest(const QJsonObject &request, QJsonObject &data, QString &message)
{
    const QString type = request.value(QStringLiteral("req")).toString();
    return withProjectContext([&]() -> bool {
        // 握手：机型信息 + 根节点 id
        if (type == QLatin1String(RemoteProtocol::kReqHello)) {
            data.insert(QStringLiteral("protocolVersion"), RemoteProtocol::kProtocolVersion);
            data.insert(QStringLiteral("projectName"), m_boundProjectName);
            data.insert(QStringLiteral("projectPath"), m_boundProjectPath);
            data.insert(QStringLiteral("projectRootNodeId"), m_boundRootNodeId);
            return true;
        }
        // 目录项列表
        if (type == QLatin1String(RemoteProtocol::kReqListDir)) {
            const qint64 nodeId = request.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            TabManager::TabData tmp;
            tmp.type = TabManager::TabType::Directory;
            tmp.currentNodeId = nodeId;
            QVector<DirectoryItem> items;
            QString err;
            if (!assembleDirectoryItems(m_nodeService, m_drawingService, &tmp, items, &err)) {
                message = err.isEmpty() ? QStringLiteral("加载目录失败") : err;
                return false;
            }
            QJsonArray arr;
            for (const DirectoryItem &item : items) {
                arr.append(RemoteProtocol::directoryItemToJson(item));
            }
            data.insert(QStringLiteral("items"), arr);
            // 附带当前节点类型与名称：客户端用于动作状态与远程标签标题，避免额外网络往返
            HFADMNode current;
            if (m_nodeService->getNode(nodeId, current)) {
                data.insert(QStringLiteral("currentType"), static_cast<int>(current.type));
                data.insert(QStringLiteral("currentName"), current.name);
            }
            return true;
        }
        // 递归搜索
        if (type == QLatin1String(RemoteProtocol::kReqSearch)) {
            const qint64 rootNodeId = request.value(QStringLiteral("rootNodeId")).toVariant().toLongLong();
            const QString keyword = request.value(QStringLiteral("keyword")).toString();
            QVector<DirectoryItem> items;
            QString err;
            if (!assembleSearchResults(m_nodeService, m_drawingService, rootNodeId, keyword,
                                       items, &err)) {
                message = err.isEmpty() ? QStringLiteral("搜索失败") : err;
                return false;
            }
            QJsonArray arr;
            for (const DirectoryItem &item : items) {
                arr.append(RemoteProtocol::directoryItemToJson(item));
            }
            data.insert(QStringLiteral("items"), arr);
            HFADMNode current;
            if (m_nodeService->getNode(rootNodeId, current)) {
                data.insert(QStringLiteral("currentType"), static_cast<int>(current.type));
                data.insert(QStringLiteral("currentName"), current.name);
            }
            return true;
        }
        // 单节点
        if (type == QLatin1String(RemoteProtocol::kReqGetNode)) {
            HFADMNode node;
            const qint64 nodeId = request.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            if (!m_nodeService->getNode(nodeId, node)) {
                message = m_nodeService->lastError();
                return false;
            }
            data.insert(QStringLiteral("node"), RemoteProtocol::nodeToJson(node));
            return true;
        }
        // 祖先路径（搜索定位）
        if (type == QLatin1String(RemoteProtocol::kReqGetPath)) {
            QString path;
            const qint64 nodeId = request.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            const qint64 stopAtId = request.value(QStringLiteral("stopAtId")).toVariant().toLongLong();
            if (!m_nodeService->getNodePath(nodeId, stopAtId, path)) {
                message = m_nodeService->lastError();
                return false;
            }
            data.insert(QStringLiteral("path"), path);
            return true;
        }
        // 新建部件
        if (type == QLatin1String(RemoteProtocol::kReqCreateComponent)) {
            const qint64 parentId = request.value(QStringLiteral("parentId")).toVariant().toLongLong();
            const QString name = request.value(QStringLiteral("name")).toString();
            const QString partNo = request.value(QStringLiteral("partNo")).toString();
            const int quantity = request.value(QStringLiteral("quantity")).toInt(1);
            if (!m_nodeService->createComponent(parentId, name, partNo, quantity)) {
                message = m_nodeService->lastError();
                return false;
            }
            return true;
        }
        // 新建零件
        if (type == QLatin1String(RemoteProtocol::kReqCreatePart)) {
            const qint64 parentId = request.value(QStringLiteral("parentId")).toVariant().toLongLong();
            const QString name = request.value(QStringLiteral("name")).toString();
            const QString partNo = request.value(QStringLiteral("partNo")).toString();
            const QString material = request.value(QStringLiteral("material")).toString();
            const int quantity = request.value(QStringLiteral("quantity")).toInt(1);
            qint64 newNodeId = 0;
            if (!m_nodeService->createPart(parentId, name, partNo, material, quantity, &newNodeId)) {
                message = m_nodeService->lastError();
                return false;
            }
            data.insert(QStringLiteral("nodeId"), newNodeId);
            return true;
        }
        // 重命名
        if (type == QLatin1String(RemoteProtocol::kReqRenameNode)) {
            const qint64 nodeId = request.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            if (!m_nodeService->renameNode(nodeId, request.value(QStringLiteral("newName")).toString())) {
                message = m_nodeService->lastError();
                return false;
            }
            return true;
        }
        // 改图号段
        if (type == QLatin1String(RemoteProtocol::kReqUpdatePartNo)) {
            const qint64 nodeId = request.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            if (!m_nodeService->updateNodePartNo(nodeId, request.value(QStringLiteral("newPartNo")).toString())) {
                message = m_nodeService->lastError();
                return false;
            }
            return true;
        }
        // 零件材质/数量
        if (type == QLatin1String(RemoteProtocol::kReqUpdatePartAttrs)) {
            const qint64 nodeId = request.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            const QString material = request.value(QStringLiteral("material")).toString();
            const int quantity = request.value(QStringLiteral("quantity")).toInt();
            if (!m_nodeService->updatePartAttributes(nodeId, material, quantity)) {
                message = m_nodeService->lastError();
                return false;
            }
            return true;
        }
        // 部件数量
        if (type == QLatin1String(RemoteProtocol::kReqUpdateComponentQty)) {
            const qint64 nodeId = request.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            if (!m_nodeService->updateComponentQuantity(nodeId, request.value(QStringLiteral("quantity")).toInt())) {
                message = m_nodeService->lastError();
                return false;
            }
            return true;
        }
        // 删除（物理删除：节点+子级+零件+图纸，不可恢复）
        if (type == QLatin1String(RemoteProtocol::kReqDeleteNode)) {
            const qint64 nodeId = request.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            if (!m_nodeService->deleteNode(nodeId)) {
                message = m_nodeService->lastError();
                return false;
            }
            return true;
        }
        // 移动（剪切粘贴）
        if (type == QLatin1String(RemoteProtocol::kReqMoveNode)) {
            const qint64 nodeId = request.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            const qint64 newParentId = request.value(QStringLiteral("newParentId")).toVariant().toLongLong();
            if (!m_nodeService->moveNode(nodeId, newParentId)) {
                message = m_nodeService->lastError();
                return false;
            }
            return true;
        }
        // 复制
        if (type == QLatin1String(RemoteProtocol::kReqCopyNode)) {
            const qint64 nodeId = request.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            const qint64 newParentId = request.value(QStringLiteral("newParentId")).toVariant().toLongLong();
            const QString newName = request.value(QStringLiteral("newName")).toString();
            const QString forcedPartNo = request.value(QStringLiteral("forcedPartNo")).toString();
            if (!m_nodeService->copyNode(nodeId, newParentId, newName, forcedPartNo)) {
                message = m_nodeService->lastError();
                return false;
            }
            return true;
        }
        // 图号段占用检查
        if (type == QLatin1String(RemoteProtocol::kReqIsPartNoOccupied)) {
            const auto nodeType = static_cast<NodeType>(request.value(QStringLiteral("type")).toInt());
            const QString partNo = request.value(QStringLiteral("partNo")).toString();
            const qint64 targetParentId = request.value(QStringLiteral("targetParentId")).toVariant().toLongLong();
            const qint64 excludeNodeId = request.value(QStringLiteral("excludeNodeId")).toVariant().toLongLong();
            data.insert(QStringLiteral("occupied"),
                        m_nodeService->isPartNoOccupied(nodeType, partNo, targetParentId, excludeNodeId));
            return true;
        }
        // 图号段格式校验
        if (type == QLatin1String(RemoteProtocol::kReqIsValidPartNo)) {
            const auto nodeType = static_cast<NodeType>(request.value(QStringLiteral("type")).toInt());
            data.insert(QStringLiteral("valid"),
                        m_nodeService->isValidPartNoFormat(nodeType, request.value(QStringLiteral("partNo")).toString()));
            return true;
        }
        // 完整图号
        if (type == QLatin1String(RemoteProtocol::kReqComputeFullPartNo)) {
            const qint64 nodeId = request.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            data.insert(QStringLiteral("full"), m_nodeService->computeFullPartNo(nodeId));
            return true;
        }
        // 零件属性
        if (type == QLatin1String(RemoteProtocol::kReqLoadPart)) {
            Part part;
            const qint64 nodeId = request.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            if (!m_nodeService->loadPart(nodeId, part)) {
                message = m_nodeService->lastError();
                return false;
            }
            data.insert(QStringLiteral("part"), RemoteProtocol::partToJson(part));
            return true;
        }
        // 部件属性
        if (type == QLatin1String(RemoteProtocol::kReqLoadComponent)) {
            Component component;
            const qint64 nodeId = request.value(QStringLiteral("nodeId")).toVariant().toLongLong();
            if (!m_nodeService->loadComponent(nodeId, component)) {
                message = m_nodeService->lastError();
                return false;
            }
            data.insert(QStringLiteral("component"), RemoteProtocol::componentToJson(component));
            return true;
        }
        // 导入 PDF（base64 传输，落临时文件后走本地导入）
        if (type == QLatin1String(RemoteProtocol::kReqImportPdf)) {
            const qint64 partNodeId = request.value(QStringLiteral("partNodeId")).toVariant().toLongLong();
            const QString fileName = request.value(QStringLiteral("fileName")).toString();
            QTemporaryDir dir;
            if (!dir.isValid()) {
                message = QStringLiteral("临时目录创建失败");
                return false;
            }
            const QString tmpPath = dir.filePath(fileName);
            QFile file(tmpPath);
            if (!file.open(QIODevice::WriteOnly)) {
                message = QStringLiteral("临时文件写入失败");
                return false;
            }
            file.write(QByteArray::fromBase64(request.value(QStringLiteral("data")).toString().toLatin1()));
            file.close();
            if (!m_drawingService->importPdf(partNodeId, tmpPath)) {
                message = m_drawingService->lastError();
                return false;
            }
            return true;
        }
        // 设为当前版本
        if (type == QLatin1String(RemoteProtocol::kReqSetCurrentDrawing)) {
            const qint64 partNodeId = request.value(QStringLiteral("partNodeId")).toVariant().toLongLong();
            const qint64 drawingId = request.value(QStringLiteral("drawingId")).toVariant().toLongLong();
            if (!m_drawingService->setCurrentDrawing(partNodeId, drawingId)) {
                message = m_drawingService->lastError();
                return false;
            }
            return true;
        }
        // 删除图纸（物理删除：记录 + 磁盘文件，无回收站）
        if (type == QLatin1String(RemoteProtocol::kReqDeleteDrawing)) {
            const qint64 drawingId = request.value(QStringLiteral("drawingId")).toVariant().toLongLong();
            if (!m_drawingService->removeDrawing(drawingId, DrawingService::DrawingRemovalMode::KeepVersions)) {
                message = m_drawingService->lastError();
                return false;
            }
            return true;
        }
        // 拉取图纸文件（base64）
        if (type == QLatin1String(RemoteProtocol::kReqGetDrawingFile)) {
            Drawing drawing;
            const qint64 drawingId = request.value(QStringLiteral("drawingId")).toVariant().toLongLong();
            if (!m_drawingService->getDrawing(drawingId, drawing)) {
                message = m_drawingService->lastError();
                return false;
            }
            const QString fullPath = DrawingService::resolveDrawingPath(
                m_drawingService->projectPath(), drawing.filePath);
            QFile file(fullPath);
            if (!file.open(QIODevice::ReadOnly)) {
                message = QStringLiteral("图纸文件读取失败：%1").arg(fullPath);
                return false;
            }
            data.insert(QStringLiteral("fileName"), drawing.fileName);
            data.insert(QStringLiteral("data"),
                        QString::fromLatin1(file.readAll().toBase64()));
            return true;
        }
        // 心跳
        if (type == QLatin1String(RemoteProtocol::kReqHeartbeat)) {
            return true;
        }
        message = QStringLiteral("未知请求类型：%1").arg(type);
        return false;
    }, &message);
}

#include "remoteserver.moc"
