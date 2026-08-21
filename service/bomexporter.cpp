#include "bomexporter.h"

#include "service/xlsxwriter.h"
#include "service/nodeservice.h"
#include "service/drawingservice.h"
#include "model/hfdadnode.h"
#include "model/drawing.h"

#include <QDateTime>
#include <QHash>

#include <algorithm>
#include <functional>

BomExporter::BomExporter(NodeService *nodeService, DrawingService *drawingService)
    : m_nodeService(nodeService)
    , m_drawingService(drawingService)
{
}

bool BomExporter::exportBom(qint64 rootNodeId, const QString &machineName,
                            const QString &filePath, QString *error) const
{
    if (!m_nodeService || !m_drawingService) {
        if (error) {
            *error = QStringLiteral("服务不可用");
        }
        return false;
    }

    QVector<HFADMNode> nodes;
    QVector<QString> materials;
    QVector<int> quantities;
    if (!m_nodeService->loadSubtreeForBom(rootNodeId, nodes, materials, quantities)) {
        if (error) {
            *error = m_nodeService->lastError();
        }
        return false;
    }
    if (nodes.isEmpty()) {
        if (error) {
            *error = QStringLiteral("机型数据为空");
        }
        return false;
    }

    // id→下标 与 父→子 映射；SQL 结果无序，兄弟按 (type, name) 排序（与 UI ORDER BY type,name 一致）
    QHash<qint64, int> idx;
    QHash<qint64, QVector<int>> children;
    for (int i = 0; i < nodes.size(); ++i) {
        idx.insert(nodes.at(i).id, i);
        children[nodes.at(i).parentId].append(i);
    }
    for (auto it = children.begin(); it != children.end(); ++it) {
        QVector<int> &kids = it.value();
        std::sort(kids.begin(), kids.end(), [&nodes](int a, int b) {
            if (nodes.at(a).type != nodes.at(b).type) {
                return nodes.at(a).type < nodes.at(b).type;
            }
            return nodes.at(a).name < nodes.at(b).name;
        });
    }

    const int rootIdx = idx.value(rootNodeId, -1);
    if (rootIdx < 0) {
        if (error) {
            *error = QStringLiteral("未找到机型根节点");
        }
        return false;
    }

    // ---- 按树形 DFS 收集数据行与统计（完整图号：部件=机型名.段、零件=父完整图号.段）----
    QString aircraftName = machineName; // 机型名（DFS 到机型节点时取实际名称）
    int componentCount = 0;             // 部件项数
    int partCount = 0;                  // 零件项数
    int drawingTotal = 0;               // 图纸总张数（含全部版本）
    QVector<QVector<QString>> dataRows;

    std::function<void(int, int, const QString &)> dfs =
        [&](int i, int level, const QString &parentFull) {
            const HFADMNode &n = nodes.at(i);
            QString full;
            if (n.type == NodeType::Aircraft) {
                full = n.name;
                aircraftName = n.name;
            } else if (n.type == NodeType::Component) {
                // 部件段全机型唯一：前缀恒为机型名（不继承父部件段）
                full = aircraftName + QLatin1Char('.') + n.partNo;
            } else {
                full = parentFull + QLatin1Char('.') + n.partNo;
            }

            int drawingCount = 0;
            QString currentVersion;
            if (n.type == NodeType::Part) {
                QVector<Drawing> drawings;
                if (m_drawingService->queryDrawings(n.id, drawings)) {
                    drawingCount = drawings.size();
                    for (const Drawing &d : drawings) {
                        if (d.isCurrent) {
                            currentVersion = d.version;
                            break;
                        }
                    }
                }
            }

            // 序号 / 层级 / 图号 / 名称 / 类型 / 材质 / 数量 / 单位 / 图纸张数 / 当前版本 / 备注
            dataRows.push_back({
                QString::number(dataRows.size() + 1),
                QString::number(level),
                full,
                n.name,
                nodeTypeDisplayName(n.type),
                materials.at(i),
                QString::number(quantities.at(i)),
                (n.type == NodeType::Aircraft) ? QStringLiteral("套") : QStringLiteral("件"),
                QString::number(drawingCount),
                currentVersion,
                n.remark
            });

            if (n.type == NodeType::Component) {
                ++componentCount;
            } else if (n.type == NodeType::Part) {
                ++partCount;
                drawingTotal += drawingCount;
            }

            const auto childIt = children.constFind(n.id);
            if (childIt != children.constEnd()) {
                for (const int c : childIt.value()) {
                    dfs(c, level + 1, full);
                }
            }
        };
    dfs(rootIdx, 1, QString());

    // ---- 组装规范 BOM 表格（标题 → 信息 → 表头 → 数据 → 汇总）----
    XlsxWriter::Sheet sheet;

    // 标题行：{机型} 整机 BOM 表
    sheet.rows.push_back({aircraftName + QStringLiteral(" 整机 BOM 表")});
    sheet.rowKinds.push_back(XlsxWriter::RowKind::Title);

    // 信息行：机型 / 导出时间 / 部件 / 零件 / 图纸统计
    const QString info = QStringLiteral("机型：%1    导出时间：%2    部件：%3 项    零件：%4 项    图纸：%5 张")
                             .arg(aircraftName,
                                  QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                                  QString::number(componentCount),
                                  QString::number(partCount),
                                  QString::number(drawingTotal));
    sheet.rows.push_back({info});
    sheet.rowKinds.push_back(XlsxWriter::RowKind::Info);

    // 表头行
    sheet.rows.push_back({QStringLiteral("序号"), QStringLiteral("层级"), QStringLiteral("图号"),
                          QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("材质"),
                          QStringLiteral("数量"), QStringLiteral("单位"), QStringLiteral("图纸张数"),
                          QStringLiteral("当前版本"), QStringLiteral("备注")});
    sheet.rowKinds.push_back(XlsxWriter::RowKind::Header);

    // 数据行
    for (const auto &row : dataRows) {
        sheet.rows.push_back(row);
        sheet.rowKinds.push_back(XlsxWriter::RowKind::Data);
    }

    // 汇总行
    sheet.rows.push_back({QStringLiteral("合计：部件 %1 项、零件 %2 项、图纸 %3 张")
                              .arg(QString::number(componentCount),
                                   QString::number(partCount),
                                   QString::number(drawingTotal))});
    sheet.rowKinds.push_back(XlsxWriter::RowKind::Total);

    // 数字列：序号(1)、层级(2)、数量(7)、图纸张数(9)
    sheet.numericCols = {1, 2, 7, 9};
    sheet.columnWidths = {6.0, 6.0, 26.0, 20.0, 8.0, 14.0, 7.0, 6.0, 10.0, 10.0, 22.0};
    sheet.freezeRows = 3; // 冻结标题 + 信息 + 表头
    sheet.autoFilter = true;

    return XlsxWriter::write(filePath, sheet, error);
}
