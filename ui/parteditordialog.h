#ifndef PARTEDITORDIALOG_H
#define PARTEDITORDIALOG_H

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;
class QSpinBox;
class QListWidget;
class NodeService;
class DrawingService;

// 零件编辑对话框：双击零件节点时打开
// 支持：修改零件属性（名称/图号本段/材质/数量，与创建时一致）
//       图纸导入/更新（无图纸时"导入新图纸"，有图纸时"更新图纸"，版本字母自动递增 A/B/C）
// 返回 true 表示发生修改，调用方应刷新目录显示
bool showPartEditorDialog(QWidget *parent, NodeService *nodeService,
                          DrawingService *drawingService, qint64 partNodeId,
                          QString *errorMessage);

#endif // PARTEDITORDIALOG_H
