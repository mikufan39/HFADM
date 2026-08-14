#ifndef BOMEXPORTER_H
#define BOMEXPORTER_H

#include <QString>
#include <QVector>

class NodeService;
class DrawingService;

// BOM 导出：把机型完整产品结构（机型→部件→零件，支持嵌套部件）组装为表格并写出 .xlsx。
// 列：层级 / 图号 / 名称 / 类型 / 数量 / 材质 / 备注 / 图纸张数 / 当前版本。
// 完整图号在内存中按树形 DFS 拼接（部件=机型名.本段，不继承父部件段；零件=父完整图号.本段）。
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
