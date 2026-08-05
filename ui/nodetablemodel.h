#ifndef NODETABLEMODEL_H
#define NODETABLEMODEL_H

#include "model/hfdadnode.h"
#include "model/drawing.h"

#include <QAbstractTableModel>
#include <QVector>

// 目录浏览项：产品结构节点或图纸（图纸挂在零件目录下展示）
struct DirectoryItem {
    enum class Kind {
        Node,     // 产品结构节点（机型/部件/零件）
        Drawing   // 图纸（PDF 文件及版本）
    };

    Kind kind = Kind::Node;
    HFADMNode node;
    Drawing drawing;
    QString fullPartNo;  // 节点完整图号（机型为名称；图纸行为空）
    QString pathHint; // 搜索模式下的祖先路径（如 "部件/子部件"），空表示无
    int quantity = -1;              // 数量（部件=component.quantity，零件=part.quantity）；-1 表示不适用（机型/图纸行）
    bool partWithoutDrawing = false; // 零件且图纸列表为空：名称后标注「（无图）」
};

// 目录浏览用的详情列表模型（开发规范 §9：QTableView + QAbstractTableModel）
// 列：名称 / 类型 / 图号 / 数量 / 修改时间（点击表头排序，名称列按拼音）
class NodeTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        ColName = 0,
        ColType,
        ColPartNo,
        ColQuantity,
        ColModified,
        ColCount
    };

    explicit NodeTableModel(QObject *parent = nullptr);

    void setItems(const QVector<DirectoryItem> &items);
    void setFilterText(const QString &text);

    DirectoryItem itemAt(int row) const;
    HFADMNode nodeAt(int row) const;      // 行为图纸时返回空节点
    Drawing drawingAt(int row) const;     // 行为节点时返回空图纸
    int filteredRowCount() const;

    // QAbstractTableModel 接口
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void sort(int column, Qt::SortOrder order) override;

private:
    void rebuildVisibleRows();
    void applySort();
    // 各列排序键（名称用原始名称，避免「（无图）」标注干扰拼音排序）
    static QString sortName(const DirectoryItem &item);
    static QString sortType(const DirectoryItem &item);
    static QString sortPartNo(const DirectoryItem &item);
    static int quantityValue(const DirectoryItem &item);      // -1 映射为最大值排后
    static QDateTime sortTime(const DirectoryItem &item);     // 无效时间映射为极小值排后

    QVector<DirectoryItem> m_items;   // 原始数据
    QVector<int> m_visibleRows;       // 过滤后可见的原始行索引
    QString m_filterText;
    int m_sortColumn = -1;            // -1 表示未排序
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};

#endif // NODETABLEMODEL_H
