#include "nodeservice.h"
#include "database/databasemanager.h"
#include "service/pinyin.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

// 搜索匹配：名称/图号/材质 + 拼音全拼/首字母（kwPinyin 为关键词转拼音后的结果）
bool matchesSearch(const HFADMNode &node, const QString &material,
                   const QString &kwLower, const QString &kwPinyin)
{
    if (node.name.contains(kwLower, Qt::CaseInsensitive)) {
        return true;
    }
    if (!node.partNo.isEmpty() && node.partNo.contains(kwLower)) {
        return true;
    }
    const QString namePy = Pinyin::toPinyin(node.name);
    if (namePy.contains(kwPinyin) || Pinyin::initials(node.name).contains(kwLower)) {
        return true;
    }
    if (!material.isEmpty()) {
        if (material.contains(kwLower, Qt::CaseInsensitive)) {
            return true;
        }
        const QString materialPy = Pinyin::toPinyin(material);
        if (materialPy.contains(kwPinyin) || Pinyin::initials(material).contains(kwLower)) {
            return true;
        }
    }
    return false;
}

} // namespace

NodeService::NodeService(DatabaseManager *databaseManager, QObject *parent)
    : QObject(parent)
    , m_databaseManager(databaseManager)
{
}

void NodeService::setProjectPath(const QString &projectPath)
{
    m_projectPath = projectPath;
}

QString NodeService::projectPath() const
{
    return m_projectPath;
}

bool NodeService::loadDirectory(qint64 parentId, QVector<HFADMNode> &children)
{
    if (!m_databaseManager) {
        m_lastError = QStringLiteral("数据库管理器为空");
        return false;
    }
    return m_databaseManager->queryChildren(parentId, children);
}

bool NodeService::createComponent(qint64 parentId, const QString &name, const QString &partNo,
                                  int quantity, const QString &remark)
{
    if (name.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("部件名称不能为空");
        qWarning() << "NodeService: 新建部件失败 - 名称为空";
        return false;
    }
    if (!validateComponentPartNo(partNo)) {
        qWarning() << "NodeService: 新建部件失败 - 图号非法" << partNo;
        return false;
    }
    if (m_databaseManager->isPartNoTaken(NodeType::Component, partNo.trimmed(), 0, 0)) {
        m_lastError = QStringLiteral("图号 %1 已被使用，部件图号需全机型唯一").arg(partNo.trimmed());
        qWarning() << "NodeService: 新建部件失败 - 图号占用" << partNo;
        return false;
    }

    if (!m_databaseManager->insertNode(parentId, name.trimmed(), NodeType::Component,
                                       partNo.trimmed(), remark)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    m_databaseManager->addOperationLog(QStringLiteral("CREATE COMPONENT"),
                                       m_databaseManager->lastInsertId(),
                                       static_cast<int>(NodeType::Component));

    // 联动创建部件属性记录（数量）
    if (!m_databaseManager->insertComponent(m_databaseManager->lastInsertId(), quantity)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    qInfo() << "NodeService: 新建部件成功" << name << "图号" << partNo
            << "数量" << quantity << "父节点:" << parentId;
    return true;
}

bool NodeService::createPart(qint64 parentId, const QString &name, const QString &partNo,
                             const QString &material, int quantity, qint64 *newNodeId,
                             const QString &remark)
{
    if (newNodeId) {
        *newNodeId = 0;
    }
    if (name.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("零件名称不能为空");
        qWarning() << "NodeService: 新建零件失败 - 名称为空";
        return false;
    }
    if (quantity < 1) {
        m_lastError = QStringLiteral("数量必须大于等于 1");
        qWarning() << "NodeService: 新建零件失败 - 数量非法" << quantity;
        return false;
    }
    // 限制：零件只能创建在部件节点下（机型子节点仅允许部件；零件为叶子节点）
    {
        HFADMNode parent;
        if (parentId == 0 || !m_databaseManager->getNode(parentId, parent)
            || parent.type != NodeType::Component) {
            m_lastError = QStringLiteral("零件只能创建在部件节点下");
            qWarning() << "NodeService: 新建零件失败 - 父节点不是部件" << parentId;
            return false;
        }
    }
    if (!validatePartPartNo(partNo)) {
        qWarning() << "NodeService: 新建零件失败 - 图号非法" << partNo;
        return false;
    }
    if (m_databaseManager->isPartNoTaken(NodeType::Part, partNo.trimmed(), parentId, 0)) {
        m_lastError = QStringLiteral("图号 %1 已被同一父节点下的零件使用").arg(partNo.trimmed());
        qWarning() << "NodeService: 新建零件失败 - 图号占用" << partNo;
        return false;
    }

    if (!m_databaseManager->beginTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    // 先建 node（type=Part），再建 part 记录，保证一致性
    if (!m_databaseManager->insertNode(parentId, name.trimmed(), NodeType::Part,
                                       partNo.trimmed(), remark)) {
        m_databaseManager->rollbackTransaction();
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    const qint64 nodeId = m_databaseManager->lastInsertId();
    if (!m_databaseManager->insertPart(nodeId, material, quantity)) {
        m_databaseManager->rollbackTransaction();
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    if (!m_databaseManager->commitTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    if (newNodeId) {
        *newNodeId = nodeId;
    }
    m_databaseManager->addOperationLog(QStringLiteral("CREATE PART"),
                                       nodeId, static_cast<int>(NodeType::Part));

    qInfo() << "NodeService: 新建零件成功" << name << "父节点:" << parentId;
    return true;
}

bool NodeService::renameNode(qint64 nodeId, const QString &newName)
{
    if (newName.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("名称不能为空");
        qWarning() << "NodeService: 重命名失败 - 名称为空";
        return false;
    }

    if (!m_databaseManager->updateNodeName(nodeId, newName.trimmed())) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    m_databaseManager->addOperationLog(QStringLiteral("RENAME NODE"), nodeId, 0);

    qInfo() << "NodeService: 重命名成功" << nodeId << newName;
    return true;
}

bool NodeService::collectDeletionPlan(qint64 nodeId, DeletionPlan &plan) const
{
    plan.nodes.clear();
    if (!m_databaseManager->collectDeletionPlan(nodeId, plan)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    // 填充完整图号（确认弹窗与进度日志显示用）
    for (DeletionPlan::Node &item : plan.nodes) {
        item.fullPartNo = computeFullPartNo(item.nodeId);
    }
    return true;
}

bool NodeService::deleteNode(qint64 nodeId,
                             const std::function<void(const HFADMNode &)> &onNodeDeleted,
                             const std::function<void(const QString &)> &onFileDeleted)
{
    QVector<QString> filePaths;
    if (!m_databaseManager->deleteNodeTree(nodeId, filePaths, onNodeDeleted)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    // 记录删除后清理磁盘图纸文件（数据库记录已删，文件删除失败不影响数据一致性）
    int failedFiles = 0;
    for (const QString &filePath : filePaths) {
        const QString fullPath = QFileInfo(filePath).isAbsolute()
            ? filePath
            : QDir(m_projectPath).filePath(filePath);
        if (QFile::exists(fullPath) && !QFile::remove(fullPath)) {
            ++failedFiles;
            qWarning() << "NodeService: 删除图纸文件失败" << fullPath;
        }
        if (onFileDeleted) {
            onFileDeleted(filePath);
        }
    }

    m_databaseManager->addOperationLog(QStringLiteral("DELETE NODE"), nodeId, 0);
    qInfo() << "NodeService: 物理删除节点成功" << nodeId
            << "清理文件" << filePaths.size() - failedFiles << "/" << filePaths.size();
    return true;
}

bool NodeService::getNode(qint64 nodeId, HFADMNode &node) const
{
    if (!m_databaseManager) {
        m_lastError = QStringLiteral("数据库管理器为空");
        return false;
    }
    return m_databaseManager->getNode(nodeId, node);
}

bool NodeService::countSubtreeStats(qint64 rootNodeId, int &partCount,
                                    int &drawingCount) const
{
    if (!m_databaseManager) {
        m_lastError = QStringLiteral("数据库管理器为空");
        return false;
    }
    return m_databaseManager->countSubtreeStats(rootNodeId, partCount, drawingCount);
}

bool NodeService::loadSubtreeForBom(qint64 rootNodeId, QVector<HFADMNode> &nodes,
                                    QVector<QString> &materials,
                                    QVector<int> &quantities) const
{
    if (!m_databaseManager) {
        m_lastError = QStringLiteral("数据库管理器为空");
        return false;
    }
    return m_databaseManager->loadSubtreeForBom(rootNodeId, nodes, materials, quantities);
}

bool NodeService::findComponentByPartNo(const QString &partNo, HFADMNode &node) const
{
    if (!m_databaseManager) {
        m_lastError = QStringLiteral("数据库管理器为空");
        return false;
    }
    return m_databaseManager->findComponentByPartNo(partNo, node);
}

bool NodeService::findPartByParentAndPartNo(qint64 parentId, const QString &partNo,
                                            HFADMNode &node) const
{
    if (!m_databaseManager) {
        m_lastError = QStringLiteral("数据库管理器为空");
        return false;
    }
    return m_databaseManager->findPartByParentAndPartNo(parentId, partNo, node);
}

bool NodeService::searchRecursive(qint64 rootNodeId, const QString &keyword,
                                  QVector<HFADMNode> &nodes, QVector<Drawing> &drawings)
{
    if (!m_databaseManager) {
        m_lastError = QStringLiteral("数据库管理器为空");
        return false;
    }
    // 加载子树全部节点与材质，在内存中按五字段 + 拼音过滤（资源管理器式即时搜索）
    QVector<HFADMNode> allNodes;
    QVector<QString> materials;
    if (!m_databaseManager->loadSubtreeWithMaterial(rootNodeId, allNodes, materials)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    const QString kw = keyword.trimmed();
    const QString kwLower = kw.toLower();
    const QString kwPinyin = Pinyin::toPinyin(kwLower);
    nodes.clear();
    if (kw.isEmpty()) {
        nodes = allNodes;
    } else {
        nodes.reserve(allNodes.size());
        for (int i = 0; i < allNodes.size(); ++i) {
            const HFADMNode &node = allNodes.at(i);
            if (matchesSearch(node, materials.at(i), kwLower, kwPinyin)) {
                nodes.append(node);
            }
        }
    }

    // 需求：搜索仅匹配节点（部件/零件），不匹配节点下的图纸
    drawings.clear();
    return true;
}

bool NodeService::getNodePath(qint64 nodeId, qint64 stopAtId, QString &path) const
{
    path.clear();
    if (!m_databaseManager) {
        m_lastError = QStringLiteral("数据库管理器为空");
        return false;
    }

    // 收集 nodeId 父级到 stopAtId 之间的名称链（均不含），如 root>A>B>C → "B"
    HFADMNode start;
    if (!m_databaseManager->getNode(nodeId, start)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    QStringList segments;
    qint64 cursor = start.parentId;
    int guard = 0;
    while (cursor != 0 && cursor != stopAtId && guard++ < 10000) {
        HFADMNode current;
        if (!m_databaseManager->getNode(cursor, current)) {
            m_lastError = m_databaseManager->lastError();
            return false;
        }
        segments.prepend(current.name);
        cursor = current.parentId;
    }
    path = segments.join(QStringLiteral("/"));
    return true;
}

bool NodeService::getNodeChain(qint64 nodeId, QVector<HFADMNode> &chain) const
{
    chain.clear();
    if (!m_databaseManager) {
        m_lastError = QStringLiteral("数据库管理器为空");
        return false;
    }
    // 沿父链从 nodeId 一路收集到根（parentId=0 的机型节点），prepend 保证根在前
    qint64 cursor = nodeId;
    int guard = 0;
    while (cursor != 0 && guard++ < 10000) {
        HFADMNode current;
        if (!m_databaseManager->getNode(cursor, current)) {
            m_lastError = m_databaseManager->lastError();
            return false;
        }
        chain.prepend(current);
        cursor = current.parentId;
    }
    if (chain.isEmpty()) {
        m_lastError = QStringLiteral("节点不存在");
        return false;
    }
    return true;
}

bool NodeService::resolvePath(qint64 rootNodeId, const QStringList &segments,
                              qint64 &resultNodeId, QString *error) const
{
    resultNodeId = 0;
    if (!m_databaseManager) {
        m_lastError = QStringLiteral("数据库管理器为空");
        if (error) {
            *error = m_lastError;
        }
        return false;
    }
    HFADMNode root;
    if (!m_databaseManager->getNode(rootNodeId, root)) {
        m_lastError = m_databaseManager->lastError();
        if (error) {
            *error = m_lastError;
        }
        return false;
    }
    qint64 cursor = rootNodeId;
    for (const QString &raw : segments) {
        const QString seg = raw.trimmed();
        if (seg.isEmpty()) {
            continue;
        }
        // 首段与机型根同名：视为根段，直接跳过（路径常以机型名开头，如 机型A/部件1）
        if (cursor == rootNodeId && root.name.compare(seg, Qt::CaseInsensitive) == 0) {
            continue;
        }
        QVector<HFADMNode> children;
        if (!m_databaseManager->queryChildren(cursor, children)) {
            m_lastError = m_databaseManager->lastError();
            if (error) {
                *error = m_lastError;
            }
            return false;
        }
        qint64 match = 0;
        for (const HFADMNode &child : children) {
            if (child.name.compare(seg, Qt::CaseInsensitive) == 0) {
                match = child.id;
                break; // 重名取第一个
            }
        }
        if (match == 0) {
            const QString msg = QStringLiteral("找不到目录「%1」").arg(seg);
            m_lastError = msg;
            if (error) {
                *error = msg;
            }
            return false;
        }
        cursor = match;
    }
    resultNodeId = cursor;
    return true;
}

bool NodeService::loadPart(qint64 nodeId, Part &part) const
{
    if (!m_databaseManager) {
        m_lastError = QStringLiteral("数据库管理器为空");
        return false;
    }
    return m_databaseManager->queryPartByNodeId(nodeId, part);
}

bool NodeService::updatePartAttributes(qint64 nodeId, const QString &material, int quantity)
{
    if (quantity < 1) {
        m_lastError = QStringLiteral("数量必须大于等于 1");
        qWarning() << "NodeService: 更新零件属性失败 - 数量非法" << quantity;
        return false;
    }

    if (!m_databaseManager->updatePart(nodeId, material, quantity)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    qInfo() << "NodeService: 更新零件属性成功" << nodeId;
    return true;
}

QStringList NodeService::fetchMaterialList() const
{
    return m_databaseManager->fetchMaterialList();
}

bool NodeService::loadComponent(qint64 nodeId, Component &component) const
{
    if (!m_databaseManager->queryComponentByNodeId(nodeId, component)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    return true;
}

bool NodeService::updateComponentQuantity(qint64 nodeId, int quantity)
{
    if (quantity < 1) {
        m_lastError = QStringLiteral("数量必须大于等于 1");
        qWarning() << "NodeService: 更新部件数量失败 - 数量非法" << quantity;
        return false;
    }

    if (!m_databaseManager->updateComponentQuantity(nodeId, quantity)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    qInfo() << "NodeService: 更新部件数量成功" << nodeId << quantity;
    return true;
}

bool NodeService::moveNode(qint64 nodeId, qint64 newParentId)
{
    if (nodeId == newParentId) {
        m_lastError = QStringLiteral("不能移动到自己下面");
        return false;
    }

    // 防止移动到自己的子树内形成环：沿 newParentId 向上查父链
    qint64 cursor = newParentId;
    while (cursor != 0) {
        if (cursor == nodeId) {
            m_lastError = QStringLiteral("不能移动到自己的子节点下");
            return false;
        }
        HFADMNode parent;
        if (!m_databaseManager->getNode(cursor, parent)) {
            m_lastError = m_databaseManager->lastError();
            return false;
        }
        cursor = parent.parentId;
    }

    // 零件移动后图号前缀跟随新父节点，需校验新父节点下零件段不冲突
    HFADMNode moving;
    if (!m_databaseManager->getNode(nodeId, moving)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    if (moving.type == NodeType::Part) {
        // 零件只能挂部件下（不能移动到机型/零件下）
        HFADMNode target;
        if (newParentId == 0 || !m_databaseManager->getNode(newParentId, target)
            || target.type != NodeType::Component) {
            m_lastError = QStringLiteral("零件只能移动到部件节点下");
            return false;
        }
        if (!moving.partNo.isEmpty()
            && m_databaseManager->isPartNoTaken(NodeType::Part, moving.partNo,
                                                newParentId, nodeId)) {
            m_lastError = QStringLiteral("目标位置下已存在图号 %1 的零件，移动后图号会冲突")
                              .arg(moving.partNo);
            return false;
        }
    }

    if (!m_databaseManager->updateNodeParent(nodeId, newParentId)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    m_databaseManager->addOperationLog(QStringLiteral("MOVE NODE"), nodeId, 0);

    qInfo() << "NodeService: 移动节点成功" << nodeId << "->" << newParentId;
    return true;
}

bool NodeService::copyNode(qint64 nodeId, qint64 newParentId, const QString &newName,
                           const QString &forcedPartNo)
{
    // 深拷贝子树（含 part 属性；图纸记录不复制，文件共享留待重新导入）
    HFADMNode source;
    if (!m_databaseManager->getNode(nodeId, source)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    if (!m_databaseManager->beginTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    if (!copySubtreeRecursive(nodeId, newParentId, newName, forcedPartNo)) {
        m_databaseManager->rollbackTransaction();
        return false;
    }

    if (!m_databaseManager->commitTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    m_databaseManager->addOperationLog(QStringLiteral("COPY NODE"), nodeId, 0);

    qInfo() << "NodeService: 复制节点成功" << nodeId << "->" << newParentId;
    return true;
}

bool NodeService::copySubtreeRecursive(qint64 sourceNodeId, qint64 newParentId,
                                       const QString &overrideName,
                                       const QString &forcedPartNo)
{
    HFADMNode source;
    if (!m_databaseManager->getNode(sourceNodeId, source)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    const QString targetName = overrideName.isEmpty() ? source.name : overrideName;

    // 顶层节点可用调用方指定的图号段（复制冲突弹窗解决后）；子树内部自动分配空闲段号
    QString targetPartNo;
    if (!forcedPartNo.isEmpty()) {
        targetPartNo = forcedPartNo.trimmed();
    } else if (source.type == NodeType::Component) {
        targetPartNo = findFreeComponentPartNo(source.partNo);
    } else if (source.type == NodeType::Part) {
        targetPartNo = findFreePartPartNo(newParentId, source.partNo);
    }

    if (!m_databaseManager->insertNode(newParentId, targetName, source.type, targetPartNo,
                                       source.remark)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    const qint64 newId = m_databaseManager->lastInsertId();

    if (source.type == NodeType::Part) {
        Part part;
        if (m_databaseManager->queryPartByNodeId(sourceNodeId, part)) {
            if (!m_databaseManager->insertPart(newId, part.material, part.quantity)) {
                m_lastError = m_databaseManager->lastError();
                return false;
            }
        }
    } else if (source.type == NodeType::Component) {
        Component component;
        if (m_databaseManager->queryComponentByNodeId(sourceNodeId, component)) {
            if (!m_databaseManager->insertComponent(newId, component.quantity)) {
                m_lastError = m_databaseManager->lastError();
                return false;
            }
        }
    }

    // 递归复制子节点（不含已删除项）
    QVector<HFADMNode> children;
    if (m_databaseManager->queryChildren(sourceNodeId, children)) {
        for (const HFADMNode &child : children) {
            if (!copySubtreeRecursive(child.id, newId, QString(), QString())) {
                return false;
            }
        }
    }
    return true;
}

bool NodeService::isPartNoOccupied(NodeType type, const QString &partNo,
                                   qint64 targetParentId, qint64 excludeNodeId) const
{
    const QString p = partNo.trimmed();
    if (p.isEmpty()) {
        return false;
    }
    if (type == NodeType::Component) {
        // 部件段全机型唯一（parentId 无关）
        return m_databaseManager->isPartNoTaken(NodeType::Component, p, 0, excludeNodeId);
    }
    if (type == NodeType::Part) {
        return m_databaseManager->isPartNoTaken(NodeType::Part, p, targetParentId, excludeNodeId);
    }
    return false;
}

bool NodeService::isValidPartNoFormat(NodeType type, const QString &partNo) const
{
    const QString p = partNo.trimmed();
    if (p.isEmpty()) {
        return false;
    }
    if (type == NodeType::Component) {
        bool ok = false;
        const int value = p.toInt(&ok);
        return ok && value >= 0 && value <= 9999;
    }
    // 零件段：非空、不含点号
    return !p.contains(QLatin1Char('.'));
}

// ---------- 图号 ----------

bool NodeService::updateNodePartNo(qint64 nodeId, const QString &newPartNo)
{
    HFADMNode node;
    if (!m_databaseManager->getNode(nodeId, node)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    if (node.type == NodeType::Aircraft) {
        m_lastError = QStringLiteral("机型节点不设置图号");
        return false;
    }

    const QString partNo = newPartNo.trimmed();
    if (node.type == NodeType::Component) {
        if (!validateComponentPartNo(partNo)) {
            return false;
        }
        if (m_databaseManager->isPartNoTaken(NodeType::Component, partNo, 0, nodeId)) {
            m_lastError = QStringLiteral("图号 %1 已被使用，部件图号需全机型唯一").arg(partNo);
            return false;
        }
    } else {
        if (!validatePartPartNo(partNo)) {
            return false;
        }
        if (m_databaseManager->isPartNoTaken(NodeType::Part, partNo, node.parentId, nodeId)) {
            m_lastError = QStringLiteral("图号 %1 已被同一父节点下的零件使用").arg(partNo);
            return false;
        }
    }

    if (!m_databaseManager->updateNodePartNo(nodeId, partNo)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    qInfo() << "NodeService: 更新图号成功" << nodeId << partNo;
    return true;
}

bool NodeService::updateNodeRemark(qint64 nodeId, const QString &remark)
{
    if (!m_databaseManager) {
        m_lastError = QStringLiteral("数据库管理器为空");
        return false;
    }
    if (!m_databaseManager->updateNodeRemark(nodeId, remark)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    qInfo() << "NodeService: 更新节点备注成功" << nodeId;
    return true;
}

QString NodeService::computeFullPartNo(qint64 nodeId) const
{
    HFADMNode node;
    if (!m_databaseManager || !m_databaseManager->getNode(nodeId, node)) {
        return QString();
    }
    return computeFullPartNo(node);
}

QString NodeService::computeFullPartNo(const HFADMNode &node) const
{
    switch (node.type) {
    case NodeType::Aircraft:
        return node.name;
    case NodeType::Component: {
        if (node.partNo.isEmpty()) {
            return QString();
        }
        const QString aircraft = aircraftNameOf(node);
        return aircraft.isEmpty() ? QString() : aircraft + QStringLiteral(".") + node.partNo;
    }
    case NodeType::Part: {
        if (node.partNo.isEmpty()) {
            return QString();
        }
        HFADMNode parent;
        if (node.parentId == 0 || !m_databaseManager->getNode(node.parentId, parent)) {
            return QString();
        }
        const QString parentFull = computeFullPartNo(parent);
        return parentFull.isEmpty() ? QString()
                                    : parentFull + QStringLiteral(".") + node.partNo;
    }
    }
    return QString();
}

bool NodeService::validateComponentPartNo(const QString &partNo) const
{
    const QString p = partNo.trimmed();
    if (p.isEmpty()) {
        m_lastError = QStringLiteral("部件图号不能为空");
        return false;
    }
    bool allDigits = true;
    for (const QChar c : p) {
        if (!c.isDigit()) {
            allDigits = false;
            break;
        }
    }
    if (!allDigits || p.size() > 4) {
        m_lastError = QStringLiteral("部件图号必须为 0-9999 的数字");
        return false;
    }
    bool ok = false;
    const int v = p.toInt(&ok);
    if (!ok || v < 0 || v > 9999) {
        m_lastError = QStringLiteral("部件图号必须为 0-9999 的数字");
        return false;
    }
    return true;
}

bool NodeService::validatePartPartNo(const QString &partNo) const
{
    const QString p = partNo.trimmed();
    if (p.isEmpty()) {
        m_lastError = QStringLiteral("零件图号不能为空");
        return false;
    }
    if (p.contains(QLatin1Char('.')) || p.contains(QLatin1Char(' '))) {
        m_lastError = QStringLiteral("零件图号不能包含点号或空格");
        return false;
    }
    return true;
}

QString NodeService::aircraftNameOf(const HFADMNode &node) const
{
    qint64 cursor = node.parentId;
    int guard = 0;
    while (cursor != 0 && guard++ < 10000) {
        HFADMNode current;
        if (!m_databaseManager->getNode(cursor, current)) {
            break;
        }
        if (current.type == NodeType::Aircraft) {
            return current.name;
        }
        cursor = current.parentId;
    }
    return QString();
}

QString NodeService::findFreeComponentPartNo(const QString &preferred) const
{
    if (!preferred.isEmpty() && !m_databaseManager->isPartNoTaken(NodeType::Component,
                                                                  preferred, 0, 0)) {
        return preferred;
    }
    // 从 1 开始查找空闲段号（避开 0 与已占用值）
    for (int i = 1; i <= 9999; ++i) {
        const QString candidate = QString::number(i);
        if (!m_databaseManager->isPartNoTaken(NodeType::Component, candidate, 0, 0)) {
            return candidate;
        }
    }
    return QString();
}

QString NodeService::findFreePartPartNo(qint64 parentId, const QString &preferred) const
{
    if (!preferred.isEmpty() && !m_databaseManager->isPartNoTaken(NodeType::Part,
                                                                  preferred, parentId, 0)) {
        return preferred;
    }
    // 在 preferred 基础上递增查找；避免与父节点图号形式混淆，从 01 起查找
    bool ok = false;
    int base = preferred.toInt(&ok);
    if (!ok || base < 1) {
        base = 1;
    }
    for (int i = base; i < base + 10000; ++i) {
        const QString candidate = QString::number(i).rightJustified(2, QLatin1Char('0'));
        if (!m_databaseManager->isPartNoTaken(NodeType::Part, candidate, parentId, 0)) {
            return candidate;
        }
    }
    return QString();
}

QString NodeService::typeDisplayName(NodeType type)
{
    return nodeTypeDisplayName(type);
}

QString NodeService::lastError() const
{
    return m_lastError;
}
