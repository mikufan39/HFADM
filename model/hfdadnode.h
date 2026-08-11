#ifndef HFADMNODE_H
#define HFADMNODE_H

#include <QDateTime>
#include <QString>

// 产品结构节点类型（与数据库 node.type 字段对应）
enum class NodeType {
    Aircraft = 1,   // 机型
    Component = 2,  // 部件
    Part = 3        // 零件
};

// 产品结构统一节点模型（开发规范 §10：禁止创建多个树模型）
// 仅保存数据与状态，不负责 UI 显示与数据库连接
struct HFADMNode {
    qint64 id = 0;
    qint64 parentId = 0;
    QString name;
    NodeType type = NodeType::Component;
    QString partNo;   // 图号本段编号（部件 0-9999 全机型唯一；零件同父唯一；机型为空）
    QString remark;   // 备注（部件/零件可填；机型不使用）
    QDateTime createTime;
    QDateTime updateTime;
    bool deleted = false;
};

// 节点类型的显示名称（领域映射，UI 与 Service 共用）
inline QString nodeTypeDisplayName(NodeType type)
{
    switch (type) {
    case NodeType::Aircraft:
        return QStringLiteral("机型");
    case NodeType::Component:
        return QStringLiteral("部件");
    case NodeType::Part:
        return QStringLiteral("零件");
    }
    return QStringLiteral("未知");
}

#endif // HFADMNODE_H
