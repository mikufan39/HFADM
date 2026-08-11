#include "remoteoperations.h"
#include "dialogs.h"
#include "model/component.h"
#include "model/part.h"
#include "service/remoteclient.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

namespace {

// 目标位置的图号前缀（继承机制镜像 clipboard.cpp）：部件=机型名.，零件=目标父完整图号.
QString remotePartNoPrefixFor(RemoteClient *client, const HFADMNode &node,
                              qint64 targetParentId)
{
    QString full;
    if (!client->computeFullPartNo(targetParentId, full, nullptr)) {
        return QString();
    }
    if (node.type == NodeType::Component) {
        const int dot = full.indexOf(QLatin1Char('.'));
        const QString machine = dot > 0 ? full.left(dot) : full;
        return machine.isEmpty() ? QString() : machine + QLatin1Char('.');
    }
    return full.isEmpty() ? QString() : full + QLatin1Char('.');
}

// 复制到目标位置时图号段是否冲突（镜像 clipboard.cpp）
bool remotePartNoConflicts(RemoteClient *client, const HFADMNode &node,
                           qint64 targetParentId, const QVector<HFADMNode> &allNodes,
                           QString *error)
{
    if (node.type != NodeType::Component && node.type != NodeType::Part) {
        return false;
    }
    if (client->isPartNoOccupied(node.type, node.partNo, targetParentId, 0, error)) {
        return true;
    }
    for (const HFADMNode &other : allNodes) {
        if (other.id == node.id) {
            continue;
        }
        if (other.type == node.type && other.partNo == node.partNo) {
            return true;
        }
    }
    return false;
}

// 复制：预检冲突 -> 弹窗循环 -> 按最终段号复制（镜像 clipboard.cpp 的 copyWithConflictResolution）
bool remoteCopyWithConflictResolution(QWidget *parent, RemoteClient *client,
                                      const NodeClipboard &clip, qint64 targetNodeId,
                                      QString *error)
{
    QVector<CopyConflictItem> conflicts;
    for (const HFADMNode &node : clip.nodes) {
        if (node.type != NodeType::Component && node.type != NodeType::Part) {
            continue; // 机型节点不参与图号冲突
        }
        if (remotePartNoConflicts(client, node, targetNodeId, clip.nodes, error)) {
            CopyConflictItem item;
            item.nodeId = node.id;
            item.type = node.type;
            item.name = node.name;
            if (!client->computeFullPartNo(node.id, item.originalFullPartNo, error)) {
                return false;
            }
            item.newPartNo = node.partNo;
            item.prefix = remotePartNoPrefixFor(client, node, targetNodeId);
            conflicts.append(item);
        }
    }

    while (!conflicts.isEmpty()) {
        if (!resolveCopyPartNoConflictDialog(parent, conflicts)) {
            if (error) {
                *error = QStringLiteral("已取消复制");
            }
            return false;
        }
        bool allOk = true;
        for (const CopyConflictItem &item : conflicts) {
            if (!client->isValidPartNoFormat(item.type, item.newPartNo, error)) {
                allOk = false;
                break;
            }
        }
        if (allOk) {
            for (const CopyConflictItem &item : conflicts) {
                if (client->isPartNoOccupied(item.type, item.newPartNo, targetNodeId, 0, error)) {
                    allOk = false;
                    break;
                }
            }
        }
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
    }

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
        if (!client->copyNode(node.id, targetNodeId, node.name, forcedPartNo, error)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool remoteLoadDirectory(RemoteClient *client, qint64 nodeId, const QString &searchText,
                         QVector<DirectoryItem> &items, QString *error)
{
    const QString keyword = searchText.trimmed();
    if (keyword.isEmpty()) {
        return client->listDir(nodeId, items, error);
    }
    return client->search(nodeId, keyword, items, error);
}

bool remoteRenameNode(QWidget *parent, RemoteClient *client, const HFADMNode &node,
                      QString *error)
{
    bool ok = false;
    const QString newName = QInputDialog::getText(
        parent, QStringLiteral("重命名"), QStringLiteral("新名称："),
        QLineEdit::Normal, node.name, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == node.name) {
        return true; // 取消或无变化视为无操作
    }
    if (!client->renameNode(node.id, newName.trimmed(), error)) {
        return false;
    }
    return true;
}

bool remoteDeleteNodes(QWidget *parent, RemoteClient *client,
                       const QVector<HFADMNode> &targets, QString *error)
{
    if (targets.isEmpty()) {
        return true;
    }
    const QString message = targets.size() == 1
        ? QStringLiteral("确定删除「%1」吗？将连同其全部子级、图纸一起删除，此操作不可恢复！")
              .arg(targets.first().name)
        : QStringLiteral("确定删除选中的 %1 项吗？将连同其全部子级、图纸一起删除，此操作不可恢复！")
              .arg(targets.size());
    const auto answer = QMessageBox::warning(parent, QStringLiteral("确认删除"),
                                             message, QMessageBox::Yes | QMessageBox::No,
                                             QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return true;
    }
    for (const HFADMNode &node : targets) {
        if (!client->deleteNode(node.id, error)) {
            return false;
        }
    }
    return true;
}

bool remoteShowProperties(QWidget *parent, RemoteClient *client, const HFADMNode &node,
                          QString *error)
{
    const bool isPart = node.type == NodeType::Part;
    const bool isComponent = node.type == NodeType::Component;
    const bool hasPartNo = node.type != NodeType::Aircraft;

    QString partMaterial;
    int partQuantity = 1;
    if (isPart) {
        Part part;
        if (!client->loadPart(node.id, part, error)) {
            return false;
        }
        partMaterial = part.material;
        partQuantity = part.quantity;
    }
    int componentQuantity = 1;
    if (isComponent) {
        Component component;
        if (!client->loadComponent(node.id, component, error)) {
            return false;
        }
        componentQuantity = component.quantity;
    }

    QString partNoPrefix;
    if (hasPartNo) {
        QString full;
        if (!client->computeFullPartNo(node.id, full, error)) {
            return false;
        }
        const QString partNo = node.partNo;
        if (!full.isEmpty() && !partNo.isEmpty() && full.endsWith(partNo)) {
            partNoPrefix = full.left(full.size() - partNo.size());
        } else if (!full.isEmpty()) {
            partNoPrefix = full + QStringLiteral(".");
        }
    }

    QString newName;
    QString newPartNo = node.partNo;
    QString newMaterial;
    int newQuantity = partQuantity;
    int newComponentQuantity = componentQuantity;
    QString newRemark = node.remark;
    if (!showNodePropertiesDialog(parent, node.name, nodeTypeDisplayName(node.type),
                                  node.createTime.toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                                  hasPartNo, partNoPrefix, node.partNo,
                                  isPart, partMaterial, partQuantity,
                                  isComponent, componentQuantity, node.remark,
                                  newName, newPartNo, newMaterial, newQuantity,
                                  newComponentQuantity, newRemark)) {
        return true; // 取消视为无操作
    }

    if (newName != node.name) {
        if (!client->renameNode(node.id, newName, error)) {
            return false;
        }
    }
    if (hasPartNo && newPartNo != node.partNo) {
        if (!client->updatePartNo(node.id, newPartNo, error)) {
            return false;
        }
    }
    if (isPart && (newMaterial != partMaterial || newQuantity != partQuantity)) {
        if (!client->updatePartAttributes(node.id, newMaterial, newQuantity, error)) {
            return false;
        }
    }
    if (isComponent && newComponentQuantity != componentQuantity) {
        if (!client->updateComponentQuantity(node.id, newComponentQuantity, error)) {
            return false;
        }
    }
    if (newRemark != node.remark) {
        if (!client->updateNodeRemark(node.id, newRemark, error)) {
            return false;
        }
    }
    return true;
}

bool remotePasteClipboard(QWidget *parent, RemoteClient *client, const NodeClipboard &clip,
                          qint64 targetNodeId, QString *error)
{
    if (!clip.valid()) {
        if (error) {
            *error = QStringLiteral("剪贴板为空");
        }
        return false;
    }
    if (clip.mode == NodeClipboard::Mode::Cut) {
        for (const HFADMNode &node : clip.nodes) {
            if (!client->moveNode(node.id, targetNodeId, error)) {
                return false;
            }
        }
        return true;
    }
    return remoteCopyWithConflictResolution(parent, client, clip, targetNodeId, error);
}

bool remoteImportPdf(QWidget *parent, RemoteClient *client, qint64 partNodeId,
                     QString *error)
{
    const QString filePath = QFileDialog::getOpenFileName(
        parent, QStringLiteral("选择 PDF 图纸"), QString(), QStringLiteral("PDF 文件 (*.pdf)"));
    if (filePath.isEmpty()) {
        return true; // 取消视为无操作
    }
    if (!client->importPdf(partNodeId, filePath, error)) {
        return false;
    }
    return true;
}

bool remoteSetCurrentDrawing(RemoteClient *client, qint64 partNodeId, qint64 drawingId,
                             QString *error)
{
    return client->setCurrentDrawing(partNodeId, drawingId, error);
}

bool remoteDeleteDrawing(QWidget *parent, RemoteClient *client, qint64 drawingId,
                         const QString &fileName, QString *error)
{
    const auto answer = QMessageBox::warning(
        parent, QStringLiteral("确认删除"),
        QStringLiteral("确定删除图纸「%1」吗？文件将一并删除，此操作不可恢复。").arg(fileName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return true;
    }
    if (!client->deleteDrawing(drawingId, error)) {
        return false;
    }
    return true;
}

bool remoteOpenDrawing(RemoteClient *client, const Drawing &drawing,
                       QString &tempFilePath, QString *error)
{
    return client->fetchDrawingFile(drawing, tempFilePath, error);
}
