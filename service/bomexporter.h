#ifndef BOMEXPORTER_H
#define BOMEXPORTER_H

#include <QString>
#include <QVector>

class NodeService;
class DrawingService;

// BOM 导出：把机型完整产品结构（机型→部件→零件，支持嵌套部件）组装为规范 BOM 表格并写出 .xlsx。
// 布局：标题行（{机型} 整机 BOM 表）→ 信息行（机型/导出时间/部件/零件/图纸统计）→ 表头行 →
//       数据行（序号/层级/图号/名称/类型/材质/数量/单位/图纸张数/当前版本/备注）→ 汇总行（合计）。
// 完整图号在内存中按树形 DFS 拼接（部件=机型名.段，不继承父部件段；零件=父完整图号.段）。
// 表格启用冻结窗格（标题/信息/表头常驻）与自动筛选；机型行数量=1 套、部件/零件=各自装配数量（件）。
class BomExporter
{
public:
    BomExporter(NodeService *nodeService, DrawingService *drawingService);

    // rootNodeId：机型根节点；machineName：机型名（DFS 到根节点时以实际名称为准）；
    // filePath：输出 .xlsx 路径。成功返回 true，失败置 error。
    bool exportBom(qint64 rootNodeId, const QString &machineName,
                   const QString &filePath, QString *error) const;

private:
    NodeService *m_nodeService;
    DrawingService *m_drawingService;
};

#endif // BOMEXPORTER_H
