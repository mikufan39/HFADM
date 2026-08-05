#ifndef REMOTECLIENT_H
#define REMOTECLIENT_H

#include "model/component.h"
#include "model/drawing.h"
#include "model/hfdadnode.h"
#include "model/part.h"
#include "service/clientcredentialstore.h"
#include "service/remoteprotocol.h"
#include "ui/nodetablemodel.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;
class QElapsedTimer;
class QTemporaryDir;

// 远程访问客户端（连接方）：
// 与 312 端口建立 TCP 连接，首次走配对（生成口令+密钥，服务端用户确认），
// 之后走认证（AES-GCM 挑战-响应）；成功后业务帧全程 AES-128-GCM 加密。
// 心跳保活 + 静默超时断线检测。同步 API 内部用 QEventLoop 等待响应（5s 超时）。
class RemoteClient : public QObject
{
    Q_OBJECT

public:
    explicit RemoteClient(QObject *parent = nullptr);
    ~RemoteClient() override;

    // 连接目标地址并完成配对/认证（阻塞等待，最多 5s）；成功返回 true
    bool connectTo(const QString &address, QString *error);
    void disconnectFrom();

    bool isConnected() const;
    QString projectName() const;
    QString projectPath() const;
    qint64 rootNodeId() const;
    QString peerAddress() const;
    RemoteProtocol::Permission permission() const;

    // ---- 目录 / 节点 ----
    bool listDir(qint64 nodeId, QVector<DirectoryItem> &items, QString *error);
    bool search(qint64 rootNodeId, const QString &keyword,
                QVector<DirectoryItem> &items, QString *error);
    bool getNode(qint64 nodeId, HFADMNode &node, QString *error);
    bool getPath(qint64 nodeId, qint64 stopAtId, QString &path, QString *error);

    // ---- 写操作 ----
    bool createComponent(qint64 parentId, const QString &name, const QString &partNo,
                         int quantity, QString *error);
    bool createPart(qint64 parentId, const QString &name, const QString &partNo,
                    const QString &material, int quantity, qint64 *newNodeId,
                    QString *error);
    bool renameNode(qint64 nodeId, const QString &newName, QString *error);
    bool updatePartNo(qint64 nodeId, const QString &newPartNo, QString *error);
    bool updatePartAttributes(qint64 nodeId, const QString &material, int quantity,
                              QString *error);
    bool updateComponentQuantity(qint64 nodeId, int quantity, QString *error);
    bool deleteNode(qint64 nodeId, QString *error);
    bool moveNode(qint64 nodeId, qint64 newParentId, QString *error);
    bool copyNode(qint64 nodeId, qint64 newParentId, const QString &newName,
                  const QString &forcedPartNo, QString *error);

    // ---- 图号 ----
    bool isPartNoOccupied(NodeType type, const QString &partNo, qint64 targetParentId,
                          qint64 excludeNodeId, QString *error);
    bool isValidPartNoFormat(NodeType type, const QString &partNo, QString *error);
    bool computeFullPartNo(qint64 nodeId, QString &full, QString *error);

    // ---- 属性 ----
    bool loadPart(qint64 nodeId, Part &part, QString *error);
    bool loadComponent(qint64 nodeId, Component &component, QString *error);

    // ---- 图纸 ----
    bool importPdf(qint64 partNodeId, const QString &sourceFilePath, QString *error);
    bool setCurrentDrawing(qint64 partNodeId, qint64 drawingId, QString *error);
    bool deleteDrawing(qint64 drawingId, QString *error);
    // 拉取图纸文件到本地临时目录，输出完整路径
    bool fetchDrawingFile(const Drawing &drawing, QString &tempFilePath, QString *error);

signals:
    // 断线（静默超时/对端关闭/网络错误）；reason 为提示文案
    void connectionLost(const QString &reason);
    // 内部：收到匹配的响应（供同步等待使用）
    void responseReady();
    // 配对流程开始：服务端需确认口令，UI 可提示用户
    void pairingStarted(const QString &pin, const QString &deviceName);

private:
    // 业务请求（加密）：成功填充 response=解密后的 data 对象
    bool sendRequest(const QString &req, const QJsonObject &params,
                     QJsonObject &response, QString *error);
    // 控制请求（明文，握手阶段）：成功填充 response=完整响应对象
    bool sendControlRequest(const QString &cmd, const QJsonObject &body,
                            QJsonObject &response, QString *error);
    // 发送一个加密业务帧但不等待响应（心跳用）
    void sendFireAndForgetRequest(const QString &req, const QJsonObject &params);

    // 配对流程：生成 uuid/pin/key，发送 ConnectRequest，成功保存凭证并建立会话
    bool doPair(const QString &host, QString *error);
    // 认证流程：挑战-响应，成功建立会话
    bool doAuth(const QString &host, const ClientCredentialStore::Credential &cred,
                QString *error);
    // 从握手成功响应体提取机型信息
    void applyHandshakeBody(const QJsonObject &body);

    QTcpSocket *m_socket = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    QTimer *m_silenceTimer = nullptr;
    QElapsedTimer *m_lastReceive = nullptr;
    QByteArray m_buffer;
    qint64 m_requestId = 0;
    qint64 m_pendingId = 0;
    QJsonObject m_pendingResponse;   // 完整响应对象（待调用方解析）
    QString m_projectName;
    QString m_projectPath;
    qint64 m_projectRootNodeId = 0;
    QString m_peerAddress;
    // 会话密钥与权限（认证/配对成功后设置）
    QByteArray m_sessionKey;
    RemoteProtocol::Permission m_permission = RemoteProtocol::Permission::ReadOnly;
    bool m_disconnecting = false; // 主动断开中：抑制 connectionLost 提示
};

#endif // REMOTECLIENT_H
