#ifndef REMOTEOPERATIONS_H
#define REMOTEOPERATIONS_H

#include "model/drawing.h"
#include "model/hfdadnode.h"
#include "ui/clipboard.h"
#include "ui/nodetablemodel.h"

#include <QString>
#include <QVector>

class QWidget;
class RemoteClient;

// 远程标签页的操作辅助：对话框复用本地组件，数据读写全部走 RemoteClient 协议
// 各函数返回 false 表示操作失败（error 输出原因）；返回 true 表示完成或用户取消（无操作）

// 加载远程目录（搜索框非空时走递归搜索）
bool remoteLoadDirectory(RemoteClient *client, qint64 nodeId, const QString &searchText,
                         QVector<DirectoryItem> &items, QString *error);

// 重命名（含输入对话框）
bool remoteRenameNode(QWidget *parent, RemoteClient *client, const HFADMNode &node,
                      QString *error);

// 删除多个节点（含确认对话框，递归移入回收站）
bool remoteDeleteNodes(QWidget *parent, RemoteClient *client,
                       const QVector<HFADMNode> &targets, QString *error);

// 属性对话框并应用修改（名称 + 图号 + 零件材质/数量 + 部件数量）
bool remoteShowProperties(QWidget *parent, RemoteClient *client, const HFADMNode &node,
                          QString *error);

// 粘贴（剪切=移动；复制=冲突弹窗后复制）
bool remotePasteClipboard(QWidget *parent, RemoteClient *client, const NodeClipboard &clip,
                          qint64 targetNodeId, QString *error);

// 选择 PDF 并导入到零件
bool remoteImportPdf(QWidget *parent, RemoteClient *client, qint64 partNodeId,
                     QString *error);

// 设为零件当前版本
bool remoteSetCurrentDrawing(RemoteClient *client, qint64 partNodeId, qint64 drawingId,
                             QString *error);

// 删除图纸（含确认对话框，软删除进回收站）
bool remoteDeleteDrawing(QWidget *parent, RemoteClient *client, qint64 drawingId,
                         const QString &fileName, QString *error);

// 拉取图纸文件到本地临时目录；tempFilePath 输出完整路径（供 PDF 标签打开）
bool remoteOpenDrawing(RemoteClient *client, const Drawing &drawing,
                       QString &tempFilePath, QString *error);

#endif // REMOTEOPERATIONS_H
