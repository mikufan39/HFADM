#ifndef DIALOGS_H
#define DIALOGS_H

#include "model/hfdadnode.h"

#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;

// 复制图号冲突项：一行对应一个待复制节点
struct CopyConflictItem {
    qint64 nodeId = 0;             // 源节点 id（映射回复制操作）
    NodeType type = NodeType::Component;
    QString name;                  // 复制的名称
    QString originalFullPartNo;    // 原完整图号
    QString newPartNo;             // 用户填写的新图号本段（初始为原段号）
    QString prefix;                // 新图号自动前缀（继承机制：部件=机型名.，零件=目标父完整图号.）
};

// 复制图号冲突对话框：三列（名称 / 原图号 / 新图号）
// 新图号列 = 自动前缀 + 可编辑的图号本段；用户输入实时回写 items（循环弹窗时保留上次输入）
// 返回 true 表示用户点击确定（items 已更新）；false 表示取消
bool resolveCopyPartNoConflictDialog(QWidget *parent,
                                     QVector<CopyConflictItem> &items);

// 新建零件对话框（名称/图号/材质+数量/备注 + 可选图纸 PDF）
// fullPartNoPrefix 为完整图号前缀（如 "AHZ700.3000."，只读展示"机型.部件."，零件号由用户填写）；
// materialList 为已有材质列表（去重），材质输入框自动补全提示用；
// pdfFilePath 出参：用户选择的图纸 PDF 路径，空表示不导入
bool showNewPartDialog(QWidget *parent, QString &name, QString &partNo,
                       const QString &fullPartNoPrefix, QString &material, int &quantity,
                       QString &pdfFilePath, QString &remark,
                       const QStringList &materialList = QStringList());

// 新建项目对话框：选择保存目录 + 输入机型名称
bool showNewProjectDialog(QWidget *parent, QString &parentDir, QString &projectName);

// 新建部件对话框（名称/图号/数量/备注），fullPartNoPrefix 如 "AHZ700."
bool showNewComponentDialog(QWidget *parent, QString &name, QString &partNo,
                            const QString &fullPartNoPrefix, int &quantity, QString &remark);

// 打开项目对话框：选择项目目录
bool showOpenProjectDialog(QWidget *parent, QString &projectPath);

// 备份目标目录选择
bool showBackupTargetDialog(QWidget *parent, QString &targetDir);

// 关于对话框
void showAboutDialog(QWidget *parent);

// 节点属性对话框：基础信息（名称/类型/创建时间）+ 图号（部件/零件可编辑本段）
// + 零件专属属性（材质/数量）+ 部件专属属性（数量）+ 备注（部件/零件可编辑，机型不显示）
// hasPartNo 为 false 时（机型）不显示图号编辑；返回 true 时通过出参输出修改结果
bool showNodePropertiesDialog(QWidget *parent,
                              const QString &nodeName,
                              const QString &typeName,
                              const QString &createTimeText,
                              bool hasPartNo,
                              const QString &fullPartNoPrefix,
                              const QString &currentPartNo,
                              bool isPart,
                              const QString &partMaterial,
                              int partQuantity,
                              bool isComponent,
                              int componentQuantity,
                              const QString &currentRemark,
                              QString &newName,
                              QString &newPartNo,
                              QString &newMaterial,
                              int &newQuantity,
                              int &newComponentQuantity,
                              QString &newRemark);

#endif // DIALOGS_H
