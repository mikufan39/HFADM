#include "nodetablemodel.h"

#include <QCollator>
#include <QDate>
#include <QFont>
#include <QTime>

#include <algorithm>
#include <limits>

NodeTableModel::NodeTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void NodeTableModel::setItems(const QVector<DirectoryItem> &items)
{
    beginResetModel();
    m_items = items;
    m_filterText.clear();
    rebuildVisibleRows();
    applySort();
    endResetModel();
}

void NodeTableModel::setFilterText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_filterText) {
        return;
    }
    beginResetModel();
    m_filterText = trimmed;
    rebuildVisibleRows();
    applySort();
    endResetModel();
}

DirectoryItem NodeTableModel::itemAt(int row) const
{
    if (row < 0 || row >= m_visibleRows.size()) {
        return DirectoryItem();
    }
    return m_items.at(m_visibleRows.at(row));
}

HFADMNode NodeTableModel::nodeAt(int row) const
{
    const DirectoryItem item = itemAt(row);
    return item.kind == DirectoryItem::Kind::Node ? item.node : HFADMNode();
}

Drawing NodeTableModel::drawingAt(int row) const
{
    const DirectoryItem item = itemAt(row);
    return item.kind == DirectoryItem::Kind::Drawing ? item.drawing : Drawing();
}

int NodeTableModel::filteredRowCount() const
{
    return m_visibleRows.size();
}

int NodeTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_visibleRows.size();
}

int NodeTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return ColCount;
}

QVariant NodeTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleRows.size()) {
        return QVariant();
    }

    const DirectoryItem &item = m_items.at(m_visibleRows.at(index.row()));

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColName: {
            QString name = item.kind == DirectoryItem::Kind::Node
                ? item.node.name
                : item.drawing.fileName;
            // 零件且无任何图纸：名称后标注「（无图）」
            if (item.kind == DirectoryItem::Kind::Node
                && item.node.type == NodeType::Part && item.partWithoutDrawing) {
                name += QStringLiteral("（无图）");
            }
            if (!item.pathHint.isEmpty()) {
                name = item.pathHint + QStringLiteral("/") + name;
            }
            return name;
        }
        case ColType: {
            if (item.kind == DirectoryItem::Kind::Drawing) {
                return QStringLiteral("图纸");
            }
            QString typeName = nodeTypeDisplayName(item.node.type);
            if (item.node.type == NodeType::Part) {
                typeName += QStringLiteral("（点击进入查看图纸）");
            }
            return typeName;
        }
        case ColPartNo: {
            if (item.kind != DirectoryItem::Kind::Node) {
                return QVariant();
            }
            if (item.node.type == NodeType::Aircraft) {
                return QStringLiteral("—");
            }
            return item.fullPartNo.isEmpty() ? QVariant() : item.fullPartNo;
        }
        case ColQuantity: {
            if (item.kind != DirectoryItem::Kind::Node || item.quantity <= 0) {
                return QStringLiteral("—");
            }
            return QString::number(item.quantity);
        }
        case ColModified: {
            const QDateTime time = item.kind == DirectoryItem::Kind::Node
                ? (item.node.updateTime.isValid() && item.node.updateTime > item.node.createTime
                       ? item.node.updateTime : item.node.createTime)
                : item.drawing.createTime;
            return time.toString(QStringLiteral("yyyy-MM-dd"));
        }
        default:
            return QVariant();
        }
    }

    if (role == Qt::TextAlignmentRole && index.column() == ColType) {
        return QVariant::fromValue(Qt::AlignCenter);
    }

    if (role == Qt::FontRole && index.column() == ColName) {
        if (item.kind == DirectoryItem::Kind::Drawing && item.drawing.isCurrent) {
            QFont font;
            font.setBold(true);
            return font;
        }
    }

    return QVariant();
}

QVariant NodeTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole) {
        return QVariant();
    }
    if (orientation == Qt::Horizontal) {
        switch (section) {
        case ColName:
            return QStringLiteral("名称");
        case ColType:
            return QStringLiteral("类型");
        case ColPartNo:
            return QStringLiteral("图号");
        case ColQuantity:
            return QStringLiteral("数量");
        case ColModified:
            return QStringLiteral("修改时间");
        default:
            return QVariant();
        }
    }
    return QVariant();
}

void NodeTableModel::sort(int column, Qt::SortOrder order)
{
    m_sortColumn = column;
    m_sortOrder = order;
    if (m_visibleRows.size() <= 1) {
        return;
    }

    emit layoutAboutToBeChanged();
    applySort();
    emit layoutChanged();
}

void NodeTableModel::applySort()
{
    if (m_sortColumn < 0 || m_sortColumn >= ColCount) {
        return;
    }

    QCollator collator(QLocale::Chinese);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true); // 图号中的数字段按数值比较（9 < 10）

    std::stable_sort(m_visibleRows.begin(), m_visibleRows.end(),
                     [this, collator](int a, int b) {
                         const DirectoryItem &ia = m_items.at(a);
                         const DirectoryItem &ib = m_items.at(b);
                         int cmp = 0;
                         switch (m_sortColumn) {
                         case ColName:
                             cmp = collator.compare(sortName(ia), sortName(ib));
                             break;
                         case ColType:
                             cmp = collator.compare(sortType(ia), sortType(ib));
                             break;
                         case ColPartNo:
                             cmp = collator.compare(sortPartNo(ia), sortPartNo(ib));
                             break;
                         case ColQuantity:
                             cmp = quantityValue(ia) - quantityValue(ib);
                             break;
                         case ColModified:
                             cmp = sortTime(ia) < sortTime(ib) ? -1
                                 : (sortTime(ia) > sortTime(ib) ? 1 : 0);
                             break;
                         default:
                             break;
                         }
                         return m_sortOrder == Qt::AscendingOrder ? cmp < 0 : cmp > 0;
                     });
}

QString NodeTableModel::sortName(const DirectoryItem &item)
{
    // 排序用原始名称：避免「（无图）」标注把同名零件拆分到不同分组
    return item.kind == DirectoryItem::Kind::Node ? item.node.name
                                                  : item.drawing.fileName;
}

QString NodeTableModel::sortType(const DirectoryItem &item)
{
    if (item.kind == DirectoryItem::Kind::Drawing) {
        return QStringLiteral("图纸");
    }
    return nodeTypeDisplayName(item.node.type);
}

QString NodeTableModel::sortPartNo(const DirectoryItem &item)
{
    if (item.kind != DirectoryItem::Kind::Node || item.node.type == NodeType::Aircraft) {
        return QString();
    }
    return item.fullPartNo;
}

int NodeTableModel::quantityValue(const DirectoryItem &item)
{
    // -1（不适用：机型/图纸行）映射为最大值，升序时排在最后
    return item.quantity > 0 ? item.quantity : std::numeric_limits<int>::max();
}

QDateTime NodeTableModel::sortTime(const DirectoryItem &item)
{
    const QDateTime time = item.kind == DirectoryItem::Kind::Node
        ? (item.node.updateTime.isValid() && item.node.updateTime > item.node.createTime
               ? item.node.updateTime : item.node.createTime)
        : item.drawing.createTime;
    return time.isValid() ? time : QDateTime(QDate(1900, 1, 1), QTime(0, 0));
}

void NodeTableModel::rebuildVisibleRows()
{
    m_visibleRows.clear();
    for (int i = 0; i < m_items.size(); ++i) {
        const DirectoryItem &item = m_items.at(i);
        const QString name = item.kind == DirectoryItem::Kind::Node
            ? item.node.name : item.drawing.fileName;
        if (m_filterText.isEmpty()
            || name.contains(m_filterText, Qt::CaseInsensitive)) {
            m_visibleRows.append(i);
        }
    }
}
