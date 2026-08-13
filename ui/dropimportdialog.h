#ifndef DROPIMPORTDIALOG_H
#define DROPIMPORTDIALOG_H

#include "model/hfdadnode.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QWidget;
class NodeService;
class DrawingService;

// 拖拽导入 PDF 图纸（本地项目）：
//   解析文件名（{完整图号}{版本字母}_{零件名}.pdf）→ 校验机型名与部件号 →
//   确认对话框（内嵌 PDF 预览）：表格五列（文件名/图号/版本/零件名/导入），
//   列宽可调、支持排序；点「开始导入」后逐文件导入，导入列图标实时反映状态
//   （未选中=待导入、选中青色=成功、错误红色=失败），底部进度条 + 成功/失败计数，
//   完成后面板保持打开供观察，不再弹结果框。
//   零件存在直接导入；零件不存在自动创建（名称取文件名零件名、图号取末段、
//   材质 Default、数量 1）后再导入。文件名无法解析 / 机型未在打开的标签页中 /
//   部件号不存在 视为解析失败（红色未选中图标）并跳过。
// 文件名规则与 DrawingService::importPdf 的命名约定一致；版本字母为展示信息，
// 实际版本由服务端按零件已有图纸自动取下一字母。

// 已打开的本地项目标签（拖拽导入的目标池：文件机型段匹配任一标签机型即合法）
struct DropTargetProject {
    QString projectPath; // 项目目录（hfadm.db 所在目录）
    QString machineName; // 机型名（project.name，即图号第一段）
};

// 解析图纸文件名；返回 false 表示不符合命名规则（出参为尽力解析结果）
bool parseDrawingFileName(const QString &fileName, QString &fullPartNo,
                          QString &version, QString &partName);

// 按完整图号（机型名.部件段.零件段）校验机型名与部件号并反查目标零件；
// 机器名段不匹配或部件号不存在返回 false（由调用方判定解析失败）
bool matchPartByFullPartNo(NodeService *nodeService, const QString &machineName,
                           const QString &fullPartNo, HFADMNode &part);

// 拖拽导入主入口：解析 + 显示确认面板（导入在面板内点击「开始导入」执行）。
// openProjects 为所有已打开的本地项目标签（支持拖入的图纸导入任意已打开机型）；
// currentMachineName 为当前标签页机型（对话框标题"导入到%1"）；
// switchContext 用于把数据库上下文切换到指定项目（返回是否成功）；
// activeProjectPath 为进入时的激活项目，函数退出前（含取消）会恢复。
// 返回 true 表示面板已显示并关闭（无论是否执行导入），false 表示无法继续（服务不可用等）。
bool resolveDropImport(QWidget *parent, NodeService *nodeService,
                       DrawingService *drawingService,
                       const QVector<DropTargetProject> &openProjects,
                       const QString &currentMachineName,
                       const QStringList &pdfPaths,
                       const std::function<bool(const QString &projectPath)> &switchContext,
                       const QString &activeProjectPath);

#endif // DROPIMPORTDIALOG_H
