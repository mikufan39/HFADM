#include "bomexporter.h"

#include "service/xlsxwriter.h"
#include "service/nodeservice.h"
#include "service/drawingservice.h"
#include "model/hfdadnode.h"
#include "model/drawing.h"

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

    QVector<QVector<QString>> rows;
    rows.push_back({QStringLiteral("层级"), QStringLiteral("图号"), QStringLiteral("名称"),
                    QStringLiteral("类型"), QStringLiteral("数量"), QStringLiteral("材质"),
                    QStringLiteral("备注"), QStringLiteral("图纸张数"), QStringLiteral("当前版本")});

    QString aircraftName = machineName; // 机型名（DFS 到机型节点时取实际名称）
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

            const QString quantityText = (n.type == NodeType::Aircraft)
                ? QString() // 机型行数量显示空
                : QString::number(quantities.at(i));
            rows.push_back({QString::number(level), full, n.name,
                            nodeTypeDisplayName(n.type), quantityText, materials.at(i),
                            n.remark, QString::number(drawingCount), currentVersion});

            const auto childIt = children.constFind(n.id);
            if (childIt != children.constEnd()) {
                for (const int c : childIt.value()) {
                    dfs(c, level + 1, full);
                }
            }
        };
    dfs(rootIdx, 1, QString());

    // 层级(1)、数量(5)、图纸张数(8) 为数字列
    return XlsxWriter::write(filePath, rows, {1, 5, 8}, error);
}
