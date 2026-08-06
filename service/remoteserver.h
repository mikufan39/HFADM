#ifndef REMOTESERVER_H
#define REMOTESERVER_H

#include "model/remotedevice.h"
#include "service/remoteprotocol.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class ProjectService;
class NodeService;
class DrawingService;
class QTcpServer;
class RemoteConnection;
class QDialog;

// 远程访问服务端（开放方）：
// 监听 312 端口（仅 IPv4），一次只开放一个机型（绑定 projectPath），
// 所有读写由本机 Service 层执行；处理请求前切换到绑定项目上下文，处理完恢复。
// 连接流程：首次配对(connect，用户在服务端弹窗确认口令并选权限) 或
//          二次认证(auth/authResp，AES-GCM 挑战-响应)；成功后业务帧全程加密。
class RemoteServer : public QObject
{
    Q_OBJECT

public:
    // 协议类型别名（RemoteProtocol 命名空间内类型在本类内简写）
    using Permission = RemoteProtocol::Permission;
    using PairingRequest = RemoteProtocol::PairingRequest;
    using PairingResult = RemoteProtocol::PairingResult;

    explicit RemoteServer(ProjectService *projectService, NodeService *nodeService,
                          DrawingService *drawingService, QObject *parent = nullptr);
    ~RemoteServer() override;

    // 开始开放远程访问（绑定当前机型）；端口被占用等失败时返回 false 并输出 error
    bool start(const QString &projectPath, const QString &projectName, QString *error);
    // 停止开放：关闭监听 + 断开全部客户端连接
    void stop();
    bool isRunning() const;
    QString projectPath() const;
    QString projectName() const;
    qint64 projectRootNodeId() const;
    int connectionCount() const;
    // 本机局域网 IPv4 地址列表（仅私有网段，不含回环）
    static QStringList localAddresses();

    // ---- 设备管理（在绑定项目上下文下操作 remote_device 表）----
    bool listAuthorizedDevices(QVector<RemoteDevice> &devices);
    bool deleteAuthorizedDevice(const QString &uuid);
    bool updateDevicePermission(const QString &uuid, Permission permission);
    bool renameAuthorizedDevice(const QString &uuid, const QString &newName);

    // ---- 配对确认解析器 ----
    // 收到 ConnectRequest 时同步调用，返回是否允许及权限级别。
    // 默认实现：拒绝。MainWindow 设置为弹出配对确认对话框；测试可设为自动批准。
    void setPairingResolver(std::function<PairingResult(const PairingRequest &)> resolver);
    PairingResult resolvePairing(const PairingRequest &req);

    // 注册/注销当前挂起的配对确认弹窗（供客户端取消时外部关闭）
    void setActivePairingDialog(QDialog *dialog);
    // 客户端取消配对：关闭挂起的确认弹窗并提示（address 为取消方来源 IP）
    void cancelActivePairing(const QString &address);

signals:
    void stateChanged(bool running);
    void connectionCountChanged(int count);
    void clientConnected(const QString &address);
    void clientDisconnected(const QString &address);
    // 有新设备配对成功（供 UI 刷新设备列表）
    void deviceListChanged();
    // 客户端取消了配对（弹窗被外部关闭），UI 可在状态栏提示
    void pairingCancelled(const QString &address);

private:
    friend class RemoteConnection;
    // 配对确认进行中标志：同一时刻只处理一个配对弹窗，
    // 期间其他客户端发来的配对请求被暂时拒绝（不加入黑名单）
    bool isPairingPending() const { return m_pairingPending; }
    void setPairingPending(bool pending) { m_pairingPending = pending; }
    // 处理一个业务请求：成功返回 true 并填充 data，失败返回 false 并输出 message
    bool handleRequest(const QJsonObject &request, QJsonObject &data, QString &message);
    // 在绑定项目上下文下执行业务函数（处理前切换、处理后恢复当前激活项目）
    template <typename Fn>
    bool withProjectContext(Fn &&fn, QString *message);
    // 新客户端接入
    void onNewConnection();
    // 移除连接（断开清理，触发连接数变化信号）
    void removeConnection(RemoteConnection *conn);
    // 在绑定项目上下文下查询设备（供 RemoteConnection 认证用）
    bool findDevice(const QString &uuid, RemoteDevice &device);
    bool saveNewDevice(const RemoteDevice &device);
    bool touchDeviceLastSeen(const QString &uuid);
    // 构造握手成功响应体（含机型信息）
    QJsonObject buildHandshakeBody(Permission permission) const;

    ProjectService *m_projectService = nullptr;
    NodeService *m_nodeService = nullptr;
    DrawingService *m_drawingService = nullptr;
    QTcpServer *m_server = nullptr;
    QList<RemoteConnection *> m_connections;
    QString m_boundProjectPath;
    QString m_boundProjectName;
    qint64 m_boundRootNodeId = 0; // 绑定机型的根节点 id（握手成功响应返回给客户端）
    std::function<PairingResult(const PairingRequest &)> m_pairingResolver;
    bool m_pairingPending = false; // 配对确认弹窗进行中（并发配对请求暂时拒绝）
    QDialog *m_activePairingDialog = nullptr; // 当前挂起的配对确认弹窗（客户端取消时关闭）
};

#endif // REMOTESERVER_H
