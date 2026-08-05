#include "clipboard.h"
#include "dialogs.h"
#include "service/nodeservice.h"

#include <QMessageBox>

namespace {

// 目标位置的图号前缀（继承机制）：部件=机型名.，零件=目标父完整图号.
QString partNoPrefixFor(NodeService *service, const HFADMNode &node, qint64 targetParentId)
{
    if (node.type == NodeType::Component) {
        const QString full = service->computeFullPartNo(targetParentId);
        const int dot = full.indexOf(QLatin1Char('.'));
        const QString machine = dot > 0 ? full.left(dot) : full;
        return machine.isEmpty() ? QString() : machine + QLatin1Char('.');
    }
    // Part：前缀 = 目标父完整图号 + "."
    const QString full = service->computeFullPartNo(targetParentId);
    return full.isEmpty() ? QString() : full + QLatin1Char('.');
}

// 该节点复制到目标位置时图号段是否冲突：目标位置已占用 或 本次集合内其他节点占用
bool partNoConflicts(NodeService *service, const HFADMNode &node, qint64 targetParentId,
                     const QVector<HFADMNode> &allNodes)
{
    if (node.type != NodeType::Component && node.type != NodeType::Part) {
        return false;
    }
    if (service->isPartNoOccupied(node.type, node.partNo, targetParentId, 0)) {
        return true;
    }
    for (const HFADMNode &other : allNodes) {
        if (other.id == node.id) {
            continue;
        }
        // 同一目标父下，同类型同段号即互斥
        if (other.type == node.type && other.partNo == node.partNo) {
            return true;
        }
    }
    return false;
}

// 复制：预检冲突 -> 弹窗循环（记住输入）-> 按最终段号复制
bool copyWithConflictResolution(QWidget *parent, const NodeClipboard &clip,
                                qint64 targetNodeId, NodeService *service,
                                QString *errorMessage)
{
    // 1) 预检：收集图号冲突项
    QVector<CopyConflictItem> conflicts;
    for (const HFADMNode &node : clip.nodes) {
        if (node.type != NodeType::Component && node.type != NodeType::Part) {
            continue; // 机型节点不参与图号冲突
        }
        if (partNoConflicts(service, node, targetNodeId, clip.nodes)) {
            CopyConflictItem item;
            item.nodeId = node.id;
            item.type = node.type;
            item.name = node.name;
            item.originalFullPartNo = service->computeFullPartNo(node.id);
            item.newPartNo = node.partNo;
            item.prefix = partNoPrefixFor(service, node, targetNodeId);
            conflicts.append(item);
        }
    }

    // 2) 弹窗循环：直到所有新图号格式合法且不再冲突（conflicts 保留用户输入）
    while (!conflicts.isEmpty()) {
        if (!resolveCopyPartNoConflictDialog(parent, conflicts)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("已取消复制");
            }
            return false;
        }

        bool allOk = true;
        // 格式校验
        for (const CopyConflictItem &item : conflicts) {
            if (!service->isValidPartNoFormat(item.type, item.newPartNo)) {
                allOk = false;
                break;
            }
        }
        // 唯一性校验：目标位置占用
        if (allOk) {
            for (const CopyConflictItem &item : conflicts) {
                if (service->isPartNoOccupied(item.type, item.newPartNo, targetNodeId, 0)) {
                    allOk = false;
                    break;
                }
            }
        }
        // 唯一性校验：集合内互斥
        if (allOk) {
            for (int i = 0; i < conflicts.size() && allOk; ++i) {
                for (int j = i + 1; j < conflicts.size(); ++j) {
                    if (conflicts[i].type == conflicts[j].type
                        && conflicts[i].newPartNo == conflicts[j].newPartNo) {
                        allOk = false;
                        break;
                    }
                }
            }
        }
        if (allOk) {
            break;
        }
        QMessageBox::warning(parent, QStringLiteral("图号无效"),
                             QStringLiteral("图号格式不正确或仍与目标位置冲突，请重新填写。"));
        // 继续循环：再次弹窗，用户上次填写的内容自动保留
    }

    // 3) 按最终段号复制（无冲突节点保留原段号）
    for (const HFADMNode &node : clip.nodes) {
        QString forcedPartNo;
        if (node.type == NodeType::Component || node.type == NodeType::Part) {
            for (const CopyConflictItem &item : conflicts) {
                if (item.nodeId == node.id) {
                    forcedPartNo = item.newPartNo;
                    break;
                }
            }
        }
        if (!service->copyNode(node.id, targetNodeId, node.name, forcedPartNo)) {
            if (errorMessage) {
                *errorMessage = service->lastError();
            }
            return false;
        }
    }
    return true;
}

} // namespace

bool pasteNodeClipboard(QWidget *parent, const NodeClipboard &clip, qint64 targetNodeId,
                        NodeService *nodeService, QString *errorMessage)
{
    if (!clip.valid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("剪贴板为空");
        }
        return false;
    }

    // 剪切：直接移动，不做图号冲突处理（系统规则校验由 moveNode 负责）
    if (clip.mode == NodeClipboard::Mode::Cut) {
        for (const HFADMNode &node : clip.nodes) {
            if (!nodeService->moveNode(node.id, targetNodeId)) {
                if (errorMessage) {
                    *errorMessage = nodeService->lastError();
                }
                return false;
            }
        }
        return true;
    }

    // 复制：图号冲突弹窗处理
    return copyWithConflictResolution(parent, clip, targetNodeId, nodeService, errorMessage);
}
