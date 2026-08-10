#ifndef DROPIMPORTDIALOG_H
#define DROPIMPORTDIALOG_H

#include "model/hfdadnode.h"

#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;
class NodeService;
class DrawingService;

// 拖拽导入 PDF 图纸（本地项目）：
//   解析文件名（{完整图号}{版本字母}_{零件名}.pdf）→ 校验机型名与部件号 →
//   确认对话框（内嵌 PDF 预览）→ 零件存在直接导入；零件不存在自动创建
//   （名称取文件名零件名、图号取末段、材质 Default、数量 1）后再导入。
//   机型不匹配 / 部件号不存在 / 文件名无法解析 视为解析失败并跳过。
// 文件名规则与 DrawingService::importPdf 的命名约定一致；版本字母为展示信息，
// 实际版本由服务端按零件已有图纸自动取下一字母。

// 解析图纸文件名；返回 false 表示不符合命名规则（出参为尽力解析结果）
bool parseDrawingFileName(const QString &fileName, QString &fullPartNo,
                          QString &version, QString &partName);

// 按完整图号（机型名.部件段.零件段）校验机型名与部件号并反查目标零件；
// 机器名段不匹配或部件号不存在返回 false（由调用方判定解析失败）
bool matchPartByFullPartNo(NodeService *nodeService, const QString &machineName,
                           const QString &fullPartNo, HFADMNode &part);

// 拖拽导入主入口：解析 + 确认 + 逐文件导入。
// 返回 true 表示对话框已确认（results 输出逐文件结果，含成功/失败/跳过），false 表示用户取消。
bool resolveDropImport(QWidget *parent, NodeService *nodeService,
                       DrawingService *drawingService, const QString &machineName,
                       const QStringList &pdfPaths, QVector<QString> &results);

#endif // DROPIMPORTDIALOG_H
