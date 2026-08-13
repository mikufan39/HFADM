#ifndef REMOTEPARTEDITORDIALOG_H
#define REMOTEPARTEDITORDIALOG_H

#include <QString>

class QWidget;
class RemoteClient;
struct HFADMNode;

// 远程零件编辑对话框：远程标签下双击零件节点时打开
// 与本地零件编辑器对齐：修改零件属性（名称/图号本段/材质/数量）
// + 完整图纸区（图纸版本列表、预览/导出/删除、导入新图纸）
// 全部数据与操作经 RemoteClient 协议转发给服务端执行：
//   listDir(零件) 取图纸行、fetchDrawingFile 拉图纸文件、importPdf 上传、
//   deleteDrawing 删除图纸
// 返回 true 表示发生修改，调用方应刷新目录显示
bool showRemotePartEditorDialog(QWidget *parent, RemoteClient *client,
                                const HFADMNode &node, QString *errorMessage);

#endif // REMOTEPARTEDITORDIALOG_H
