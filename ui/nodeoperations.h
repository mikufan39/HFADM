#ifndef NODEOPERATIONS_H
#define NODEOPERATIONS_H

#include "model/hfdadnode.h"

#include <QString>

class QWidget;
class NodeService;

// 重命名节点（含输入对话框）；取消或无变化返回 true（无操作）
bool renameNodeWithDialog(QWidget *parent, NodeService *service,
                          const HFADMNode &node, QString *errorMessage);

// 属性对话框并应用修改（名称 + 零件材质/数量）；取消或无修改返回 true
bool showNodePropertiesAndApply(QWidget *parent, NodeService *service,
                                const HFADMNode &node, QString *errorMessage);

#endif // NODEOPERATIONS_H
