#ifndef DELETIONPLAN_H
#define DELETIONPLAN_H

#include "model/drawing.h"
#include "model/hfdadnode.h"

#include <QString>
#include <QStringList>
#include <QVector>

// 删除计划：一次删除操作（节点及其全部子级）涉及的内容统计与明细，
// 用于删除确认弹窗的统计展示与删除进度弹窗的逐项汇报。
// 节点按后序排列（叶子优先），与数据库逐节点删除顺序保持一致。
struct DeletionPlan {
    // 单个待删除节点及其直接关联的图纸
    struct Node {
        qint64 nodeId = 0;
        QString name;        // 节点名
        NodeType type = NodeType::Component;
        QString fullPartNo;  // 完整图号（机型为名称；显示用，可为空）
        QVector<Drawing> drawings; // 该节点（零件）直接挂载的图纸记录
    };

    QVector<Node> nodes; // 全部待删除节点（含顶层与所有子级）

    // 图纸记录总数（所有节点图纸之和）
    int drawingCount() const
    {
        int count = 0;
        for (const Node &node : nodes) {
            count += node.drawings.size();
        }
        return count;
    }

    // 需清理的磁盘文件数（图纸相对路径去重，同一文件只删一次）
    int fileCount() const
    {
        QStringList seen;
        for (const Node &node : nodes) {
            for (const Drawing &drawing : node.drawings) {
                if (!drawing.filePath.isEmpty() && !seen.contains(drawing.filePath)) {
                    seen.append(drawing.filePath);
                }
            }
        }
        return seen.size();
    }
};

#endif // DELETIONPLAN_H
