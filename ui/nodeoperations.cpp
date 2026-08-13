#include <QCoreApplication>
#include "nodeoperations.h"
#include "dialogs.h"
#include "model/component.h"
#include "service/nodeservice.h"

#include <QInputDialog>
#include <QLineEdit>

bool renameNodeWithDialog(QWidget *parent, NodeService *service,
                          const HFADMNode &node, QString *errorMessage)
{
    bool ok = false;
    const QString newName = QInputDialog::getText(
        parent, QCoreApplication::translate("NodeOperations", "重命名"), QCoreApplication::translate("NodeOperations", "新名称："),
        QLineEdit::Normal, node.name, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == node.name) {
        return true; // 取消或无变化视为无操作
    }

    if (!service->renameNode(node.id, newName.trimmed())) {
        if (errorMessage) {
            *errorMessage = service->lastError();
        }
        return false;
    }
    return true;
}

bool showNodePropertiesAndApply(QWidget *parent, NodeService *service,
                                const HFADMNode &node, QString *errorMessage)
{
    const bool isPart = node.type == NodeType::Part;
    const bool isComponent = node.type == NodeType::Component;
    const bool hasPartNo = node.type != NodeType::Aircraft;

    QString partMaterial;
    int partQuantity = 1;
    if (isPart) {
        Part part;
        if (service->loadPart(node.id, part)) {
            partMaterial = part.material;
            partQuantity = part.quantity;
        }
    }
    int componentQuantity = 1;
    if (isComponent) {
        Component component;
        if (service->loadComponent(node.id, component)) {
            componentQuantity = component.quantity;
        }
    }

    // 图号前缀 = 完整图号去掉本段部分（如 "AHZ700.3000."）
    QString partNoPrefix;
    if (hasPartNo) {
        const QString full = service->computeFullPartNo(node.id);
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
        if (!service->renameNode(node.id, newName)) {
            if (errorMessage) {
                *errorMessage = service->lastError();
            }
            return false;
        }
    }
    if (hasPartNo && newPartNo != node.partNo) {
        if (!service->updateNodePartNo(node.id, newPartNo)) {
            if (errorMessage) {
                *errorMessage = service->lastError();
            }
            return false;
        }
    }
    if (isPart && (newMaterial != partMaterial || newQuantity != partQuantity)) {
        if (!service->updatePartAttributes(node.id, newMaterial, newQuantity)) {
            if (errorMessage) {
                *errorMessage = service->lastError();
            }
            return false;
        }
    }
    if (isComponent && newComponentQuantity != componentQuantity) {
        if (!service->updateComponentQuantity(node.id, newComponentQuantity)) {
            if (errorMessage) {
                *errorMessage = service->lastError();
            }
            return false;
        }
    }
    if (newRemark != node.remark) {
        if (!service->updateNodeRemark(node.id, newRemark)) {
            if (errorMessage) {
                *errorMessage = service->lastError();
            }
            return false;
        }
    }
    return true;
}
