#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include "model/hfdadnode.h"
#include "model/part.h"
#include "model/component.h"
#include "model/drawing.h"
#include "model/projectinfo.h"
#include "model/deletionplan.h"
#include "model/remotedevice.h"

#include <QObject>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager() override;

    // 连接管理
    bool openDatabase(const QString &dbPath);
    void closeDatabase();
    bool initializeDatabase();
    bool isOpen() const;
    QSqlDatabase database() const;
    QString lastError() const;

    // 项目路径约定
    static QString projectDatabaseFilename();
    static QString projectFilesDirectoryName();
    static QString composeDatabasePath(const QString &projectPath);
    static QString composeFilesPath(const QString &projectPath);

    // ---- 事务 ----
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();
    bool checkpointWal();   // 将 WAL 合并回主库，保证主库文件完整性（备份前调用）

    // ---- operation_log 表 ----
    bool addOperationLog(const QString &operation, qint64 targetId, int targetType);

    // ---- project 表 ----
    bool insertProject(const QString &name, const QString &version);
    bool updateProjectLastOpenTime();
    bool getProjectInfo(ProjectInfo &info) const;

    // ---- node 表 ----
    bool insertNode(qint64 parentId, const QString &name, NodeType type,
                    const QString &partNo = QString(), const QString &remark = QString());
    bool queryChildren(qint64 parentId, QVector<HFADMNode> &children) const;
    bool getNode(qint64 nodeId, HFADMNode &node) const;
    // 按图号段精确查找部件（部件段全机型唯一；供拖拽导入反查用）
    bool findComponentByPartNo(const QString &partNo, HFADMNode &node) const;
    // 按父节点 + 图号段精确查找零件（零件段同父唯一；供拖拽导入反查用）
    bool findPartByParentAndPartNo(qint64 parentId, const QString &partNo,
                                   HFADMNode &node) const;
    bool updateNodeName(qint64 nodeId, const QString &newName);
    bool updateNodePartNo(qint64 nodeId, const QString &newPartNo);
    // 更新节点备注（部件/零件）
    bool updateNodeRemark(qint64 nodeId, const QString &remark);
    bool updateNodeParent(qint64 nodeId, qint64 newParentId); // 移动节点
    // 图号段占用检查：部件段全机型唯一；零件段同一父节点下唯一
    // excludeNodeId 用于编辑时排除自身
    bool isPartNoTaken(NodeType type, const QString &partNo,
                       qint64 parentId, qint64 excludeNodeId) const;
    // 加载 rootNodeId 子树（含自身）全部节点及零件材质（deleted=0）；
    // 供搜索在内存中过滤（名称/图号/材质 + 拼音）
    bool loadSubtreeWithMaterial(qint64 rootNodeId, QVector<HFADMNode> &nodes,
                                 QVector<QString> &materials) const;
    // 导出BOM：一次性加载 rootNodeId 子树全部节点 + 零件材质 + 数量
    // （part.quantity / component.quantity 二选一，机型行无属性取兜底 1；
    // quantities 与 nodes 一一对应；机型/部件材质为空串）
    bool loadSubtreeForBom(qint64 rootNodeId, QVector<HFADMNode> &nodes,
                           QVector<QString> &materials, QVector<int> &quantities) const;
    // 递归搜索：rootNodeId 子树内零件所挂图纸（文件名模糊匹配，deleted=0）
    bool searchDrawingsRecursive(qint64 rootNodeId, const QString &keyword,
                                 QVector<Drawing> &drawings) const;
    // 统计 rootNodeId 整棵子树（含自身）下的零件数（type=Part 且未删除）与图纸数
    // （drawing 记录数，含全部版本，不过滤 is_current；口径与删除确认弹窗一致）
    bool countSubtreeStats(qint64 rootNodeId, int &partCount, int &drawingCount) const;
    // 将用户输入转为 LIKE 模式（转义 % _ \）
    static QString toLikePattern(const QString &keyword);

    // ---- 删除（物理删除，无回收站） ----
    // 收集删除计划：nodeId 子树全部节点（含自身，叶子优先）+ 各节点直接挂载的图纸，
    // 仅统计不删除任何数据；供删除确认弹窗与进度弹窗使用
    bool collectDeletionPlan(qint64 nodeId, DeletionPlan &plan) const;
    // 事务内递归物理删除 nodeId 子树全部记录（node/part/component/drawing），
    // 每删除一个节点回调 onNodeDeleted（可空，用于 UI 逐项汇报）；
    // filePathsToRemove 输出需清理的磁盘图纸文件相对路径（去重）
    bool deleteNodeTree(qint64 nodeId, QVector<QString> &filePathsToRemove,
                        const std::function<void(const HFADMNode &)> &onNodeDeleted = nullptr);

    qint64 lastInsertId() const;

    // ---- part 表 ----
    bool insertPart(qint64 nodeId, const QString &material, int quantity);
    bool queryPartByNodeId(qint64 nodeId, Part &part) const;
    bool updatePart(qint64 nodeId, const QString &material, int quantity);
    // 全部已使用材质（去重排序，空串排除）：新建零件材质输入框自动补全提示用
    QStringList fetchMaterialList() const;
    // 部件属性（component 表，与 part 表模式一致）
    bool insertComponent(qint64 nodeId, int quantity);
    bool queryComponentByNodeId(qint64 nodeId, Component &component) const;
    bool updateComponentQuantity(qint64 nodeId, int quantity);

    // ---- remote_device 表（远程访问已授权设备，按机型项目存储）----
    // 插入新设备（uuid 已存在返回 false）
    bool insertRemoteDevice(const RemoteDevice &device);
    // uuid 是否已授权
    bool remoteDeviceExists(const QString &uuid) const;
    // 按 uuid 查询设备（含密钥与权限）；不存在返回 false
    bool getRemoteDevice(const QString &uuid, RemoteDevice &device) const;
    // 全部已授权设备（设备管理界面用）
    bool listRemoteDevices(QVector<RemoteDevice> &devices) const;
    // 删除设备（撤销授权）
    bool deleteRemoteDevice(const QString &uuid);
    // 修改设备权限
    bool updateRemoteDevicePermission(const QString &uuid, RemoteProtocol::Permission permission);
    // 重命名设备
    bool renameRemoteDevice(const QString &uuid, const QString &newName);
    // 更新上次连接时间
    bool updateRemoteDeviceLastSeen(const QString &uuid, const QDateTime &time);

    // ---- remote_idempotency 表（写操作幂等去重，按设备 + 内容哈希键存储首次响应）----
    // 命中返回 true 并填充 payload（含 success/data/message）；未命中返回 false
    bool getIdempotencyResult(const QString &deviceUuid, const QString &idKey,
                              QJsonObject &payload) const;
    // 记录首次写操作的响应（重复键视为成功，幂等）；成功返回 true
    bool insertIdempotencyResult(const QString &deviceUuid, const QString &idKey,
                                 const QJsonObject &payload);
    // 清理早于 keepDays 天的记录；返回删除行数（失败返回 -1）
    int cleanupExpiredIdempotency(int keepDays);

private:
    bool createTables();
    bool executeSql(const QString &sql);
    bool applyPragmaSettings();
    // 旧库结构迁移：检查缺列并 ALTER TABLE 补列（幂等，打开项目时执行）
    void ensureSchemaMigration();

    QSqlDatabase m_database;
    mutable QString m_lastError;
    qint64 m_lastInsertId = 0;
};

#endif // DATABASEMANAGER_H
