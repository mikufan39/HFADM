#ifndef NODESERVICE_H
#define NODESERVICE_H

#include "model/hfdadnode.h"
#include "model/part.h"
#include "model/component.h"
#include "model/drawing.h"
#include "model/deletionplan.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class DatabaseManager;

class NodeService : public QObject
{
    Q_OBJECT

public:
    explicit NodeService(DatabaseManager *databaseManager, QObject *parent = nullptr);

    // 设置当前项目路径（用于解析图纸文件相对路径，删除图纸文件时使用）
    void setProjectPath(const QString &projectPath);
    QString projectPath() const;

    // 加载指定节点的子节点列表（deleted=0）
    bool loadDirectory(qint64 parentId, QVector<HFADMNode> &children);
    // 新建部件（type=Component），partNo 为图号本段（0-9999，全机型唯一）
    // quantity 为装配数量，联动创建 component 记录
    bool createComponent(qint64 parentId, const QString &name, const QString &partNo,
                         int quantity = 1);
    // 新建零件（type=Part，联动创建 part 记录），partNo 为图号本段（同父唯一）
    // 父节点必须是部件（机型/零件下禁止新建零件）；newNodeId 可选输出新零件节点 id
    bool createPart(qint64 parentId, const QString &name, const QString &partNo,
                    const QString &material = QString(), int quantity = 1,
                    qint64 *newNodeId = nullptr);
    // 重命名节点
    bool renameNode(qint64 nodeId, const QString &newName);
    // 更新图号本段（机型不可改；校验唯一性后写库）
    bool updateNodePartNo(qint64 nodeId, const QString &newPartNo);
    // 收集删除计划：nodeId 子树全部节点（含自身）+ 各节点图纸统计；
    // 供删除确认弹窗与进度弹窗使用（不删除任何数据）
    bool collectDeletionPlan(qint64 nodeId, DeletionPlan &plan) const;
    // 删除节点（物理删除，无回收站）：节点+全部子级+零件+图纸记录与磁盘文件，不可恢复
    // onNodeDeleted 每删除一个节点回调（UI 逐项汇报进度，可空）
    // onFileDeleted 每清理一个图纸磁盘文件回调（相对路径，可空）
    bool deleteNode(qint64 nodeId,
                    const std::function<void(const HFADMNode &)> &onNodeDeleted = nullptr,
                    const std::function<void(const QString &)> &onFileDeleted = nullptr);
    // 读取单个节点
    bool getNode(qint64 nodeId, HFADMNode &node) const;
    // 按图号段精确查找部件（部件段全机型唯一；供拖拽导入按文件名反查用）
    bool findComponentByPartNo(const QString &partNo, HFADMNode &node) const;
    // 按父节点 + 图号段精确查找零件（零件段同父唯一；供拖拽导入按文件名反查用）
    bool findPartByParentAndPartNo(qint64 parentId, const QString &partNo,
                                   HFADMNode &node) const;
    // 递归搜索：rootNodeId 子树内名称/图纸名模糊匹配（供目录内递归搜索）
    bool searchRecursive(qint64 rootNodeId, const QString &keyword,
                         QVector<HFADMNode> &nodes, QVector<Drawing> &drawings);
    // 计算 nodeId 到 stopAtId（均不含）之间的祖先路径，如 "部件/子部件"；空表示无中间层
    bool getNodePath(qint64 nodeId, qint64 stopAtId, QString &path) const;
    // 读取零件属性
    bool loadPart(qint64 nodeId, Part &part) const;
    // 更新零件属性（材质/数量）
    bool updatePartAttributes(qint64 nodeId, const QString &material, int quantity);
    // 读取部件属性（数量）
    bool loadComponent(qint64 nodeId, Component &component) const;
    // 更新部件属性（数量）
    bool updateComponentQuantity(qint64 nodeId, int quantity);
    // 移动节点（剪切粘贴：修改父节点）
    bool moveNode(qint64 nodeId, qint64 newParentId);
    // 复制节点（复制粘贴：创建副本及其属性）
    // forcedPartNo 非空时顶层节点改用该图号段（复制冲突弹窗解决后使用）；子树内部自动分配
    bool copyNode(qint64 nodeId, qint64 newParentId, const QString &newName,
                  const QString &forcedPartNo = QString());
    // 目标位置是否已占用该图号段：部件=全机型唯一，零件=指定父下唯一（excludeNodeId 除外）
    bool isPartNoOccupied(NodeType type, const QString &partNo, qint64 targetParentId,
                          qint64 excludeNodeId = 0) const;
    // 图号段格式校验（供复制冲突弹窗使用）：部件 0-9999 纯数字；零件非空且不含点号
    bool isValidPartNoFormat(NodeType type, const QString &partNo) const;
    // 计算完整图号：部件=机型名+本段；零件=父节点完整图号+本段；机型=名称
    QString computeFullPartNo(qint64 nodeId) const;
    QString computeFullPartNo(const HFADMNode &node) const;

    static QString typeDisplayName(NodeType type);
    QString lastError() const;

private:
    bool copySubtreeRecursive(qint64 sourceNodeId, qint64 newParentId,
                              const QString &overrideName, const QString &forcedPartNo);
    // 校验部件段：非空、纯数字、0-9999
    bool validateComponentPartNo(const QString &partNo) const;
    // 校验零件段：非空、不含点号
    bool validatePartPartNo(const QString &partNo) const;
    // 沿父链返回机型根名称（找不到返回空）
    QString aircraftNameOf(const HFADMNode &node) const;
    // 为部件分配空闲段号（全机型唯一）；preferred 被占则递增查找
    QString findFreeComponentPartNo(const QString &preferred) const;
    // 为零件分配空闲段号（同一父节点下唯一）
    QString findFreePartPartNo(qint64 parentId, const QString &preferred) const;

    DatabaseManager *m_databaseManager;
    QString m_projectPath;
    mutable QString m_lastError;
};

#endif // NODESERVICE_H
