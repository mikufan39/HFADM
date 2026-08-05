#ifndef DIRECTORYASSEMBLER_H
#define DIRECTORYASSEMBLER_H

#include "ui/nodetablemodel.h"
#include "ui/tabmanager.h"

#include <QVector>

class NodeService;
class DrawingService;

// 装配目录/回收站页的展示项列表（产品结构节点 + 图纸混合行）
// 由 MainWindow 调用，将数据装配逻辑与界面事件处理解耦
bool assembleDirectoryItems(NodeService *nodeService, DrawingService *drawingService,
                            const TabManager::TabData *tab,
                            QVector<DirectoryItem> &items,
                            QString *errorMessage);

// 装配递归搜索结果：当前目录子树内名称/图纸名匹配项，带祖先路径定位
bool assembleSearchResults(NodeService *nodeService, DrawingService *drawingService,
                           qint64 rootNodeId, const QString &keyword,
                           QVector<DirectoryItem> &items,
                           QString *errorMessage);

#endif // DIRECTORYASSEMBLER_H
