#include "directoryassembler.h"
#include "tabmanager.h"
#include "model/component.h"
#include "model/part.h"
#include "service/nodeservice.h"
#include "service/drawingservice.h"

#include <QString>

namespace {

// 填充节点的数量（部件/零件）与零件「无图」标注
void fillItemAttributes(NodeService *nodeService, DrawingService *drawingService,
                        DirectoryItem &item)
{
    if (item.kind != DirectoryItem::Kind::Node || !nodeService) {
        return;
    }
    switch (item.node.type) {
    case NodeType::Part: {
        Part part;
        if (nodeService->loadPart(item.node.id, part)) {
            item.quantity = part.quantity;
        }
        QVector<Drawing> drawings;
        if (drawingService && drawingService->queryDrawings(item.node.id, drawings)
            && drawings.isEmpty()) {
            item.partWithoutDrawing = true;
        }
        break;
    }
    case NodeType::Component: {
        Component component;
        if (nodeService->loadComponent(item.node.id, component)) {
            item.quantity = component.quantity;
        }
        break;
    }
    default:
        break; // 机型：数量不适用（-1）
    }
}

} // namespace

bool assembleDirectoryItems(NodeService *nodeService, DrawingService *drawingService,
                            const TabManager::TabData *tab,
                            QVector<DirectoryItem> &items,
                            QString *errorMessage)
{
    items.clear();
    if (!tab) {
        return true;
    }

    if (tab->type != TabManager::TabType::Directory) {
        return true; // PDF 页无列表数据
    }

    QVector<HFADMNode> children;
    if (!nodeService || !nodeService->loadDirectory(tab->currentNodeId, children)) {
        if (errorMessage && nodeService) {
            *errorMessage = nodeService->lastError();
        }
        return false;
    }
    for (const HFADMNode &child : children) {
        DirectoryItem item;
        item.kind = DirectoryItem::Kind::Node;
        item.node = child;
        item.fullPartNo = nodeService->computeFullPartNo(child.id);
        fillItemAttributes(nodeService, drawingService, item);
        items.append(item);
    }

    // 当前节点是零件时，其目录展示图纸（含版本）
    HFADMNode current;
    if (nodeService->getNode(tab->currentNodeId, current)
        && current.type == NodeType::Part) {
        QVector<Drawing> drawings;
        if (drawingService && drawingService->queryDrawings(tab->currentNodeId, drawings)) {
            for (const Drawing &drawing : drawings) {
                DirectoryItem item;
                item.kind = DirectoryItem::Kind::Drawing;
                item.drawing = drawing;
                items.append(item);
            }
        }
    }
    return true;
}

bool assembleSearchResults(NodeService *nodeService, DrawingService *drawingService,
                           qint64 rootNodeId, const QString &keyword,
                           QVector<DirectoryItem> &items,
                           QString *errorMessage)
{
    Q_UNUSED(drawingService)
    items.clear();
    if (!nodeService) {
        return true;
    }

    QVector<HFADMNode> nodes;
    QVector<Drawing> drawings;
    if (!nodeService->searchRecursive(rootNodeId, keyword, nodes, drawings)) {
        if (errorMessage) {
            *errorMessage = nodeService->lastError();
        }
        return false;
    }

    for (const HFADMNode &node : nodes) {
        DirectoryItem item;
        item.kind = DirectoryItem::Kind::Node;
        item.node = node;
        item.fullPartNo = nodeService->computeFullPartNo(node.id);
        fillItemAttributes(nodeService, drawingService, item);
        QString path;
        nodeService->getNodePath(node.id, rootNodeId, path);
        item.pathHint = path;
        items.append(item);
    }

    for (const Drawing &drawing : drawings) {
        DirectoryItem item;
        item.kind = DirectoryItem::Kind::Drawing;
        item.drawing = drawing;
        QString path;
        if (drawing.partNodeId != 0) {
            nodeService->getNodePath(drawing.partNodeId, rootNodeId, path);
        }
        item.pathHint = path;
        items.append(item);
    }
    return true;
}
