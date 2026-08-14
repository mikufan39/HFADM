#include "databasemanager.h"

#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QDebug>

namespace {
constexpr const char *kConnectionName = "HFADM_CONNECTION";
}

namespace {

// 后序递归收集删除计划（子先父后），返回 plan.nodes 为叶子优先顺序；
// 子节点查询不过滤 deleted（旧版本软删除残留一并收集，保证删除彻底）
bool collectDeletionRecursive(const DatabaseManager *db, qint64 nodeId, DeletionPlan &plan,
                              int &guard, QString *error)
{
    if (guard++ > 100000) {
        if (error) {
            *error = QStringLiteral("节点树过深或存在循环引用");
        }
        return false;
    }

    HFADMNode node;
    if (!db->getNode(nodeId, node)) {
        if (error) {
            *error = db->lastError();
        }
        return false;
    }

    // 1. 先收集全部子节点（后序递归）
    {
        QSqlQuery query(db->database());
        query.prepare(QStringLiteral(
            "SELECT id, parent_id, name, type, part_no, create_time, update_time, deleted "
            "FROM node WHERE parent_id = ?;"));
        query.addBindValue(nodeId);
        if (!query.exec()) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
        QVector<qint64> childIds;
        while (query.next()) {
            childIds.append(query.value(0).toLongLong());
        }
        for (qint64 childId : childIds) {
            if (!collectDeletionRecursive(db, childId, plan, guard, error)) {
                return false;
            }
        }
    }

    // 2. 再收集自身（含零件挂载的全部图纸，含旧软删残留）
    DeletionPlan::Node item;
    item.nodeId = node.id;
    item.name = node.name;
    item.type = node.type;
    if (node.type == NodeType::Part) {
        QSqlQuery query(db->database());
        query.prepare(QStringLiteral(
            "SELECT d.id, d.part_id, p.node_id, d.file_name, d.file_path, d.version, "
            "       d.is_current, d.create_time, d.deleted "
            "FROM drawing d JOIN part p ON d.part_id = p.id "
            "WHERE p.node_id = ?;"));
        query.addBindValue(nodeId);
        if (!query.exec()) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
        while (query.next()) {
            Drawing drawing;
            drawing.id = query.value(0).toLongLong();
            drawing.partId = query.value(1).toLongLong();
            drawing.partNodeId = query.value(2).toLongLong();
            drawing.fileName = query.value(3).toString();
            drawing.filePath = query.value(4).toString();
            drawing.version = query.value(5).toString();
            drawing.isCurrent = query.value(6).toInt() != 0;
            drawing.createTime = query.value(7).toDateTime();
            drawing.deleted = query.value(8).toInt() != 0;
            item.drawings.append(drawing);
        }
    }
    plan.nodes.append(item);
    return true;
}

} // namespace

DatabaseManager::DatabaseManager(QObject *parent)
    : QObject(parent)
{
}

DatabaseManager::~DatabaseManager()
{
    closeDatabase();
}

QString DatabaseManager::projectDatabaseFilename()
{
    return QStringLiteral("hfadm.db");
}

QString DatabaseManager::projectFilesDirectoryName()
{
    return QStringLiteral("files");
}

QString DatabaseManager::composeDatabasePath(const QString &projectPath)
{
    return QDir(projectPath).filePath(projectDatabaseFilename());
}

QString DatabaseManager::composeFilesPath(const QString &projectPath)
{
    return QDir(projectPath).filePath(projectFilesDirectoryName());
}

bool DatabaseManager::openDatabase(const QString &dbPath)
{
    closeDatabase();

    if (QSqlDatabase::contains(kConnectionName)) {
        QSqlDatabase::removeDatabase(kConnectionName);
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QString::fromLatin1(kConnectionName));
    m_database.setDatabaseName(dbPath);

    if (!m_database.open()) {
        m_lastError = m_database.lastError().text();
        qWarning() << "DatabaseManager: 打开数据库失败" << dbPath << m_lastError;
        return false;
    }

    if (!applyPragmaSettings()) {
        closeDatabase();
        return false;
    }

    return true;
}

void DatabaseManager::closeDatabase()
{
    if (m_database.isValid()) {
        const QString connectionName = m_database.connectionName();
        if (m_database.isOpen()) {
            m_database.close();
        }
        m_database = QSqlDatabase();
        if (QSqlDatabase::contains(connectionName)) {
            QSqlDatabase::removeDatabase(connectionName);
        }
    }
}

bool DatabaseManager::initializeDatabase()
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        qWarning() << "DatabaseManager: initializeDatabase 失败，数据库未打开";
        return false;
    }
    return createTables();
}

bool DatabaseManager::isOpen() const
{
    return m_database.isValid() && m_database.isOpen();
}

QSqlDatabase DatabaseManager::database() const
{
    return m_database;
}

QString DatabaseManager::lastError() const
{
    return m_lastError;
}

qint64 DatabaseManager::lastInsertId() const
{
    return m_lastInsertId;
}

bool DatabaseManager::applyPragmaSettings()
{
    if (!executeSql(QStringLiteral("PRAGMA foreign_keys = ON;"))) {
        return false;
    }
    if (!executeSql(QStringLiteral("PRAGMA journal_mode = WAL;"))) {
        return false;
    }
    return true;
}

// ---- 事务 ----

bool DatabaseManager::beginTransaction()
{
    if (!executeSql(QStringLiteral("BEGIN TRANSACTION;"))) {
        return false;
    }
    return true;
}

bool DatabaseManager::commitTransaction()
{
    if (!executeSql(QStringLiteral("COMMIT;"))) {
        return false;
    }
    return true;
}

bool DatabaseManager::rollbackTransaction()
{
    if (!executeSql(QStringLiteral("ROLLBACK;"))) {
        return false;
    }
    return true;
}

bool DatabaseManager::checkpointWal()
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }
    if (!executeSql(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE);"))) {
        return false;
    }
    return true;
}

// ---- operation_log 表 ----

bool DatabaseManager::addOperationLog(const QString &operation, qint64 targetId, int targetType)
{
    if (!isOpen()) {
        return false; // 日志写入失败不影响主流程
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO operation_log (operation, target_id, target_type, create_time) "
        "VALUES (?, ?, ?, ?);"));
    query.addBindValue(operation);
    query.addBindValue(targetId);
    query.addBindValue(targetType);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));

    if (!query.exec()) {
        qWarning() << "DatabaseManager: 写入操作日志失败" << query.lastError().text();
        return false;
    }
    return true;
}

// ---- project 表 ----

bool DatabaseManager::insertProject(const QString &name, const QString &version)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO project (name, version, create_time, update_time, last_open_time) "
        "VALUES (?, ?, ?, ?, ?);"));
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    query.addBindValue(name);
    query.addBindValue(version);
    query.addBindValue(now);
    query.addBindValue(now);
    query.addBindValue(now);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 插入项目记录失败" << m_lastError;
        return false;
    }

    m_lastInsertId = query.lastInsertId().toLongLong();
    return true;
}

bool DatabaseManager::updateProjectLastOpenTime()
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE project SET last_open_time = ? WHERE id = 1;"));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 更新最后打开时间失败" << m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::getProjectInfo(ProjectInfo &info) const
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, name, version, create_time, update_time, last_open_time "
        "FROM project ORDER BY id LIMIT 1;"));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 读取项目信息失败" << m_lastError;
        return false;
    }

    if (!query.next()) {
        m_lastError = QStringLiteral("项目信息不存在");
        return false;
    }

    info.id = query.value(0).toLongLong();
    info.name = query.value(1).toString();
    info.version = query.value(2).toString();
    info.createTime = query.value(3).toDateTime();
    info.updateTime = query.value(4).toDateTime();
    info.lastOpenTime = query.value(5).toDateTime();
    return true;
}

// ---- node 表 ----

bool DatabaseManager::insertNode(qint64 parentId, const QString &name, NodeType type,
                                 const QString &partNo, const QString &remark)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO node (parent_id, name, type, part_no, remark, create_time, update_time, deleted) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 0);"));
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    // 根节点（parentId==0）以 NULL 存储，满足 parent_id 外键约束
    query.addBindValue(parentId == 0 ? QVariant() : QVariant(parentId));
    query.addBindValue(name);
    query.addBindValue(static_cast<int>(type));
    query.addBindValue(partNo.trimmed().isEmpty() ? QVariant() : QVariant(partNo.trimmed()));
    query.addBindValue(remark.trimmed().isEmpty() ? QVariant() : QVariant(remark.trimmed()));
    query.addBindValue(now);
    query.addBindValue(now);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 插入节点失败" << m_lastError;
        return false;
    }

    m_lastInsertId = query.lastInsertId().toLongLong();
    return true;
}

bool DatabaseManager::queryChildren(qint64 parentId, QVector<HFADMNode> &children) const
{
    children.clear();
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    if (parentId == 0) {
        // 根层：parent_id IS NULL
        query.prepare(QStringLiteral(
            "SELECT id, parent_id, name, type, part_no, remark, create_time, update_time, deleted "
            "FROM node WHERE parent_id IS NULL AND deleted = 0 "
            "ORDER BY type, name;"));
    } else {
        query.prepare(QStringLiteral(
            "SELECT id, parent_id, name, type, part_no, remark, create_time, update_time, deleted "
            "FROM node WHERE parent_id = ? AND deleted = 0 "
            "ORDER BY type, name;"));
        query.addBindValue(parentId);
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 查询子节点失败" << m_lastError;
        return false;
    }

    while (query.next()) {
        HFADMNode node;
        node.id = query.value(0).toLongLong();
        node.parentId = query.value(1).toLongLong(); // NULL -> 0
        node.name = query.value(2).toString();
        node.type = static_cast<NodeType>(query.value(3).toInt());
        node.partNo = query.value(4).toString();
        node.remark = query.value(5).toString();
        node.createTime = query.value(6).toDateTime();
        node.updateTime = query.value(7).toDateTime();
        node.deleted = query.value(8).toInt() != 0;
        children.append(node);
    }
    return true;
}

bool DatabaseManager::getNode(qint64 nodeId, HFADMNode &node) const
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, parent_id, name, type, part_no, remark, create_time, update_time, deleted "
        "FROM node WHERE id = ?;"));
    query.addBindValue(nodeId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 读取节点失败" << m_lastError;
        return false;
    }

    if (!query.next()) {
        m_lastError = QStringLiteral("节点不存在（id=%1）").arg(nodeId);
        return false;
    }

    node.id = query.value(0).toLongLong();
    node.parentId = query.value(1).toLongLong();
    node.name = query.value(2).toString();
    node.type = static_cast<NodeType>(query.value(3).toInt());
    node.partNo = query.value(4).toString();
    node.remark = query.value(5).toString();
    node.createTime = query.value(6).toDateTime();
    node.updateTime = query.value(7).toDateTime();
    node.deleted = query.value(8).toInt() != 0;
    return true;
}

bool DatabaseManager::findComponentByPartNo(const QString &partNo, HFADMNode &node) const
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, parent_id, name, type, part_no, remark, create_time, update_time, deleted "
        "FROM node WHERE type = ? AND part_no = ? AND deleted = 0 LIMIT 1;"));
    query.addBindValue(static_cast<int>(NodeType::Component));
    query.addBindValue(partNo.trimmed());

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 按图号查找部件失败" << m_lastError;
        return false;
    }
    if (!query.next()) {
        return false; // 未命中：调用方按零件名搜索兜底
    }

    node.id = query.value(0).toLongLong();
    node.parentId = query.value(1).toLongLong();
    node.name = query.value(2).toString();
    node.type = static_cast<NodeType>(query.value(3).toInt());
    node.partNo = query.value(4).toString();
    node.remark = query.value(5).toString();
    node.createTime = query.value(6).toDateTime();
    node.updateTime = query.value(7).toDateTime();
    node.deleted = query.value(8).toInt() != 0;
    return true;
}

bool DatabaseManager::findPartByParentAndPartNo(qint64 parentId, const QString &partNo,
                                                HFADMNode &node) const
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, parent_id, name, type, part_no, remark, create_time, update_time, deleted "
        "FROM node WHERE parent_id = ? AND type = ? AND part_no = ? AND deleted = 0 LIMIT 1;"));
    query.addBindValue(parentId);
    query.addBindValue(static_cast<int>(NodeType::Part));
    query.addBindValue(partNo.trimmed());

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 按父节点查找零件失败" << m_lastError;
        return false;
    }
    if (!query.next()) {
        return false;
    }

    node.id = query.value(0).toLongLong();
    node.parentId = query.value(1).toLongLong();
    node.name = query.value(2).toString();
    node.type = static_cast<NodeType>(query.value(3).toInt());
    node.partNo = query.value(4).toString();
    node.remark = query.value(5).toString();
    node.createTime = query.value(6).toDateTime();
    node.updateTime = query.value(7).toDateTime();
    node.deleted = query.value(8).toInt() != 0;
    return true;
}

bool DatabaseManager::updateNodeRemark(qint64 nodeId, const QString &remark)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE node SET remark = ?, update_time = ? WHERE id = ?;"));
    query.addBindValue(remark.trimmed());
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(nodeId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 更新节点备注失败" << m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::updateNodeName(qint64 nodeId, const QString &newName)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE node SET name = ?, update_time = ? WHERE id = ?;"));
    query.addBindValue(newName);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(nodeId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 重命名节点失败" << m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::updateNodePartNo(qint64 nodeId, const QString &newPartNo)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE node SET part_no = ?, update_time = ? WHERE id = ?;"));
    query.addBindValue(newPartNo.trimmed().isEmpty() ? QVariant() : QVariant(newPartNo.trimmed()));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(nodeId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 更新图号失败" << m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::isPartNoTaken(NodeType type, const QString &partNo,
                                    qint64 parentId, qint64 excludeNodeId) const
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    if (type == NodeType::Component) {
        // 部件段：全机型唯一
        query.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM node "
            "WHERE type = 2 AND part_no = ? AND deleted = 0 AND id != ?;"));
        query.addBindValue(partNo);
    } else {
        // 零件段：同一父节点下唯一
        query.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM node "
            "WHERE type = 3 AND parent_id = ? AND part_no = ? AND deleted = 0 AND id != ?;"));
        query.addBindValue(parentId);
        query.addBindValue(partNo);
    }
    query.addBindValue(excludeNodeId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 图号占用检查失败" << m_lastError;
        return false;
    }

    if (query.next()) {
        return query.value(0).toInt() > 0;
    }
    return false;
}

bool DatabaseManager::updateNodeParent(qint64 nodeId, qint64 newParentId)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE node SET parent_id = ?, update_time = ? WHERE id = ?;"));
    // 根层（newParentId==0）以 NULL 存储
    query.addBindValue(newParentId == 0 ? QVariant() : QVariant(newParentId));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(nodeId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 移动节点失败" << m_lastError;
        return false;
    }
    return true;
}

// ---- 递归搜索 ----

QString DatabaseManager::toLikePattern(const QString &keyword)
{
    QString escaped;
    escaped.reserve(keyword.size() * 2);
    for (const QChar ch : keyword) {
        if (ch == QLatin1Char('%') || ch == QLatin1Char('_') || ch == QLatin1Char('\\')) {
            escaped.append(QLatin1Char('\\'));
        }
        escaped.append(ch);
    }
    return QStringLiteral("%%1%").arg(escaped);
}

bool DatabaseManager::loadSubtreeWithMaterial(qint64 rootNodeId, QVector<HFADMNode> &nodes,
                                              QVector<QString> &materials) const
{
    nodes.clear();
    materials.clear();
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "WITH RECURSIVE subtree(id) AS ("
        "  SELECT id FROM node WHERE id = ?"
        "  UNION ALL"
        "  SELECT n.id FROM node n JOIN subtree s ON n.parent_id = s.id"
        ") "
        "SELECT n.id, n.parent_id, n.name, n.type, n.part_no, n.remark, n.create_time, n.update_time, n.deleted, "
        "part.material "
        "FROM node n LEFT JOIN part ON part.node_id = n.id "
        "WHERE n.id IN (SELECT id FROM subtree) AND n.deleted = 0;"));
    query.addBindValue(rootNodeId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 加载搜索子树失败" << m_lastError;
        return false;
    }

    while (query.next()) {
        HFADMNode node;
        node.id = query.value(0).toLongLong();
        node.parentId = query.value(1).toLongLong();
        node.name = query.value(2).toString();
        node.type = static_cast<NodeType>(query.value(3).toInt());
        node.partNo = query.value(4).toString();
        node.remark = query.value(5).toString();
        node.createTime = query.value(6).toDateTime();
        node.updateTime = query.value(7).toDateTime();
        node.deleted = query.value(8).toInt() != 0;
        nodes.append(node);
        materials.append(query.value(9).toString());
    }
    return true;
}

bool DatabaseManager::loadSubtreeForBom(qint64 rootNodeId, QVector<HFADMNode> &nodes,
                                        QVector<QString> &materials,
                                        QVector<int> &quantities) const
{
    nodes.clear();
    materials.clear();
    quantities.clear();
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "WITH RECURSIVE subtree(id) AS ("
        "  SELECT id FROM node WHERE id = ?"
        "  UNION ALL"
        "  SELECT n.id FROM node n JOIN subtree s ON n.parent_id = s.id"
        ") "
        "SELECT n.id, n.parent_id, n.name, n.type, n.part_no, n.remark, n.create_time, n.update_time, n.deleted, "
        "part.material, COALESCE(part.quantity, component.quantity, 1) "
        "FROM node n "
        "LEFT JOIN part ON part.node_id = n.id "
        "LEFT JOIN component ON component.node_id = n.id "
        "WHERE n.id IN (SELECT id FROM subtree) AND n.deleted = 0;"));
    query.addBindValue(rootNodeId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 加载BOM子树失败" << m_lastError;
        return false;
    }

    while (query.next()) {
        HFADMNode node;
        node.id = query.value(0).toLongLong();
        node.parentId = query.value(1).toLongLong();
        node.name = query.value(2).toString();
        node.type = static_cast<NodeType>(query.value(3).toInt());
        node.partNo = query.value(4).toString();
        node.remark = query.value(5).toString();
        node.createTime = query.value(6).toDateTime();
        node.updateTime = query.value(7).toDateTime();
        node.deleted = query.value(8).toInt() != 0;
        nodes.append(node);
        materials.append(query.value(9).toString());
        quantities.append(query.value(10).toInt());
    }
    return true;
}

bool DatabaseManager::searchDrawingsRecursive(qint64 rootNodeId, const QString &keyword,
                                              QVector<Drawing> &drawings) const
{
    drawings.clear();
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "WITH RECURSIVE subtree(id) AS ("
        "  SELECT id FROM node WHERE id = ?"
        "  UNION ALL"
        "  SELECT n.id FROM node n JOIN subtree s ON n.parent_id = s.id"
        ") "
        "SELECT d.id, d.part_id, p.node_id, d.file_name, d.file_path, d.version, "
        "       d.is_current, d.create_time, d.deleted "
        "FROM drawing d JOIN part p ON d.part_id = p.id "
        "WHERE p.node_id IN (SELECT id FROM subtree) AND d.deleted = 0 "
        "AND d.file_name LIKE ? ESCAPE '\\';"));
    query.addBindValue(rootNodeId);
    query.addBindValue(toLikePattern(keyword));

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 递归搜索图纸失败" << m_lastError;
        return false;
    }

    while (query.next()) {
        Drawing drawing;
        drawing.id = query.value(0).toLongLong();
        drawing.partId = query.value(1).toLongLong();
        drawing.partNodeId = query.value(2).toLongLong();
        drawing.fileName = query.value(3).toString();
        drawing.filePath = query.value(4).toString();
        drawing.version = query.value(5).toString();
        drawing.isCurrent = query.value(6).toInt() != 0;
        drawing.createTime = query.value(7).toDateTime();
        drawing.deleted = query.value(8).toInt() != 0;
        drawings.append(drawing);
    }
    return true;
}

bool DatabaseManager::countSubtreeStats(qint64 rootNodeId, int &partCount,
                                        int &drawingCount) const
{
    partCount = 0;
    drawingCount = 0;
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    // 单条 WITH RECURSIVE 完成无限级子树遍历：零件数统计子树内 type=Part 的节点，
    // 图纸数统计子树内零件经 part 表关联的 drawing 记录（含全部版本，口径与删除弹窗一致）
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "WITH RECURSIVE subtree(id) AS ("
        "  SELECT id FROM node WHERE id = ?"
        "  UNION ALL"
        "  SELECT n.id FROM node n JOIN subtree s ON n.parent_id = s.id"
        ") "
        "SELECT"
        "  (SELECT COUNT(*) FROM node n"
        "     WHERE n.id IN (SELECT id FROM subtree)"
        "       AND n.type = ? AND n.deleted = 0),"
        "  (SELECT COUNT(*) FROM drawing d JOIN part p ON d.part_id = p.id"
        "     WHERE p.node_id IN (SELECT id FROM subtree)"
        "       AND d.deleted = 0);"));
    query.addBindValue(rootNodeId);
    query.addBindValue(static_cast<int>(NodeType::Part));

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 统计子树零件/图纸数失败" << m_lastError;
        return false;
    }

    // 无 FROM 的单行查询恒返回一行；子树为空（无效根 id）时两个计数均为 0
    if (query.next()) {
        partCount = query.value(0).toInt();
        drawingCount = query.value(1).toInt();
    }
    return true;
}

// ---- 删除（物理删除，无回收站） ----

bool DatabaseManager::collectDeletionPlan(qint64 nodeId, DeletionPlan &plan) const
{
    plan.nodes.clear();
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    int guard = 0;
    if (!collectDeletionRecursive(this, nodeId, plan, guard, &m_lastError)) {
        return false;
    }
    return true;
}

bool DatabaseManager::deleteNodeTree(qint64 nodeId, QVector<QString> &filePathsToRemove,
                                     const std::function<void(const HFADMNode &)> &onNodeDeleted)
{
    filePathsToRemove.clear();
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    // 1. 收集删除计划（叶子优先，含旧软删除残留），同时汇总需清理的图纸文件相对路径
    DeletionPlan plan;
    if (!collectDeletionPlan(nodeId, plan)) {
        return false;
    }
    if (plan.nodes.isEmpty()) {
        m_lastError = QStringLiteral("节点不存在（id=%1）").arg(nodeId);
        return false;
    }
    for (const DeletionPlan::Node &item : plan.nodes) {
        for (const Drawing &drawing : item.drawings) {
            if (!drawing.filePath.isEmpty() && !filePathsToRemove.contains(drawing.filePath)) {
                filePathsToRemove.append(drawing.filePath);
            }
        }
    }

    if (!beginTransaction()) {
        return false;
    }

    // 2. 逐个节点物理删除（叶子优先：先图纸、再零件/部件属性、最后节点），每删一个回调
    for (const DeletionPlan::Node &item : plan.nodes) {
        {
            QSqlQuery query(m_database);
            query.prepare(QStringLiteral(
                "DELETE FROM drawing WHERE part_id IN "
                "(SELECT id FROM part WHERE node_id = ?);"));
            query.addBindValue(item.nodeId);
            if (!query.exec()) {
                rollbackTransaction();
                m_lastError = query.lastError().text();
                qWarning() << "DatabaseManager: 删除图纸记录失败" << m_lastError;
                return false;
            }
        }
        {
            QSqlQuery query(m_database);
            query.prepare(QStringLiteral("DELETE FROM part WHERE node_id = ?;"));
            query.addBindValue(item.nodeId);
            if (!query.exec()) {
                rollbackTransaction();
                m_lastError = query.lastError().text();
                qWarning() << "DatabaseManager: 删除零件记录失败" << m_lastError;
                return false;
            }
        }
        {
            QSqlQuery query(m_database);
            query.prepare(QStringLiteral("DELETE FROM component WHERE node_id = ?;"));
            query.addBindValue(item.nodeId);
            if (!query.exec()) {
                rollbackTransaction();
                m_lastError = query.lastError().text();
                qWarning() << "DatabaseManager: 删除部件记录失败" << m_lastError;
                return false;
            }
        }
        {
            QSqlQuery query(m_database);
            query.prepare(QStringLiteral("DELETE FROM node WHERE id = ?;"));
            query.addBindValue(item.nodeId);
            if (!query.exec()) {
                rollbackTransaction();
                m_lastError = query.lastError().text();
                qWarning() << "DatabaseManager: 删除节点记录失败" << m_lastError;
                return false;
            }
        }
        if (onNodeDeleted) {
            HFADMNode node;
            node.id = item.nodeId;
            node.name = item.name;
            node.type = item.type;
            onNodeDeleted(node);
        }
    }

    if (!commitTransaction()) {
        return false;
    }
    return true;
}

// ---- part 表 ----

bool DatabaseManager::insertPart(qint64 nodeId, const QString &material, int quantity)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO part (node_id, material, quantity) VALUES (?, ?, ?);"));
    query.addBindValue(nodeId);
    query.addBindValue(material);
    query.addBindValue(quantity);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 插入零件记录失败" << m_lastError;
        return false;
    }

    m_lastInsertId = query.lastInsertId().toLongLong();
    return true;
}

bool DatabaseManager::queryPartByNodeId(qint64 nodeId, Part &part) const
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, node_id, material, quantity FROM part WHERE node_id = ? LIMIT 1;"));
    query.addBindValue(nodeId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 读取零件记录失败" << m_lastError;
        return false;
    }

    if (!query.next()) {
        m_lastError = QStringLiteral("零件记录不存在（node_id=%1）").arg(nodeId);
        return false;
    }

    part.id = query.value(0).toLongLong();
    part.nodeId = query.value(1).toLongLong();
    part.material = query.value(2).toString();
    part.quantity = query.value(3).toInt();
    return true;
}

bool DatabaseManager::updatePart(qint64 nodeId, const QString &material, int quantity)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE part SET material = ?, quantity = ? WHERE node_id = ?;"));
    query.addBindValue(material);
    query.addBindValue(quantity);
    query.addBindValue(nodeId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 更新零件记录失败" << m_lastError;
        return false;
    }
    return true;
}

QStringList DatabaseManager::fetchMaterialList() const
{
    QStringList list;
    if (!isOpen()) {
        return list;
    }
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT DISTINCT material FROM part "
            "WHERE material IS NOT NULL AND material != '' "
            "ORDER BY material;"))) {
        qWarning() << "DatabaseManager: 查询材质列表失败" << query.lastError().text();
        return list;
    }
    while (query.next()) {
        list.append(query.value(0).toString());
    }
    return list;
}

// ---- component 表 ----

bool DatabaseManager::insertComponent(qint64 nodeId, int quantity)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO component (node_id, quantity) VALUES (?, ?);"));
    query.addBindValue(nodeId);
    query.addBindValue(quantity);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 插入部件记录失败" << m_lastError;
        return false;
    }

    m_lastInsertId = query.lastInsertId().toLongLong();
    return true;
}

bool DatabaseManager::queryComponentByNodeId(qint64 nodeId, Component &component) const
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, node_id, quantity FROM component WHERE node_id = ? LIMIT 1;"));
    query.addBindValue(nodeId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 读取部件记录失败" << m_lastError;
        return false;
    }

    if (!query.next()) {
        m_lastError = QStringLiteral("部件记录不存在（node_id=%1）").arg(nodeId);
        return false;
    }

    component.id = query.value(0).toLongLong();
    component.nodeId = query.value(1).toLongLong();
    component.quantity = query.value(2).toInt();
    return true;
}

bool DatabaseManager::updateComponentQuantity(qint64 nodeId, int quantity)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE component SET quantity = ? WHERE node_id = ?;"));
    query.addBindValue(quantity);
    query.addBindValue(nodeId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 更新部件记录失败" << m_lastError;
        return false;
    }
    return true;
}

// ---- remote_device 表 ----

bool DatabaseManager::insertRemoteDevice(const RemoteDevice &device)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO remote_device(uuid, device_name, aes_key, permission, created_at, last_seen) "
        "VALUES(?, ?, ?, ?, ?, ?);"));
    query.addBindValue(device.uuid);
    query.addBindValue(device.deviceName);
    query.addBindValue(QString::fromLatin1(device.aesKey.toBase64()));
    query.addBindValue(static_cast<int>(device.permission));
    query.addBindValue(device.createdAt);
    query.addBindValue(device.lastSeen);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: 插入远程设备失败" << m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::remoteDeviceExists(const QString &uuid) const
{
    if (!isOpen()) {
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT 1 FROM remote_device WHERE uuid = ?;"));
    query.addBindValue(uuid);
    if (!query.exec()) {
        return false;
    }
    return query.next();
}

bool DatabaseManager::getRemoteDevice(const QString &uuid, RemoteDevice &device) const
{
    if (!isOpen()) {
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uuid, device_name, aes_key, permission, created_at, last_seen "
        "FROM remote_device WHERE uuid = ?;"));
    query.addBindValue(uuid);
    if (!query.exec() || !query.next()) {
        return false;
    }
    device.uuid = query.value(0).toString();
    device.deviceName = query.value(1).toString();
    device.aesKey = QByteArray::fromBase64(query.value(2).toString().toLatin1());
    device.permission = static_cast<RemoteProtocol::Permission>(query.value(3).toInt());
    device.createdAt = query.value(4).toDateTime();
    device.lastSeen = query.value(5).toDateTime();
    return true;
}

bool DatabaseManager::listRemoteDevices(QVector<RemoteDevice> &devices) const
{
    devices.clear();
    if (!isOpen()) {
        return false;
    }
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT uuid, device_name, aes_key, permission, created_at, last_seen "
            "FROM remote_device ORDER BY created_at DESC;"))) {
        return false;
    }
    while (query.next()) {
        RemoteDevice d;
        d.uuid = query.value(0).toString();
        d.deviceName = query.value(1).toString();
        d.aesKey = QByteArray::fromBase64(query.value(2).toString().toLatin1());
        d.permission = static_cast<RemoteProtocol::Permission>(query.value(3).toInt());
        d.createdAt = query.value(4).toDateTime();
        d.lastSeen = query.value(5).toDateTime();
        devices.append(d);
    }
    return true;
}

bool DatabaseManager::deleteRemoteDevice(const QString &uuid)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM remote_device WHERE uuid = ?;"));
    query.addBindValue(uuid);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::updateRemoteDevicePermission(const QString &uuid, RemoteProtocol::Permission permission)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE remote_device SET permission = ? WHERE uuid = ?;"));
    query.addBindValue(static_cast<int>(permission));
    query.addBindValue(uuid);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::renameRemoteDevice(const QString &uuid, const QString &newName)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE remote_device SET device_name = ? WHERE uuid = ?;"));
    query.addBindValue(newName);
    query.addBindValue(uuid);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::updateRemoteDeviceLastSeen(const QString &uuid, const QDateTime &time)
{
    if (!isOpen()) {
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE remote_device SET last_seen = ? WHERE uuid = ?;"));
    query.addBindValue(time);
    query.addBindValue(uuid);
    return query.exec();
}

// ---- remote_idempotency 表 ----

bool DatabaseManager::getIdempotencyResult(const QString &deviceUuid, const QString &idKey,
                                           QJsonObject &payload) const
{
    if (!isOpen() || deviceUuid.isEmpty() || idKey.isEmpty()) {
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT response_json FROM remote_idempotency WHERE device_uuid = ? AND id_key = ?;"));
    query.addBindValue(deviceUuid);
    query.addBindValue(idKey);
    if (!query.exec() || !query.next()) {
        return false;
    }
    const QByteArray json = query.value(0).toByteArray();
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return false;
    }
    payload = doc.object();
    return true;
}

bool DatabaseManager::insertIdempotencyResult(const QString &deviceUuid, const QString &idKey,
                                              const QJsonObject &payload)
{
    if (!isOpen() || deviceUuid.isEmpty() || idKey.isEmpty()) {
        return false;
    }
    // INSERT OR IGNORE：重复键（并发/重试）视为成功，保留首次响应
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO remote_idempotency(device_uuid, id_key, response_json, created_at) "
        "VALUES(?, ?, ?, ?);"));
    query.addBindValue(deviceUuid);
    query.addBindValue(idKey);
    query.addBindValue(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
    query.addBindValue(QDateTime::currentDateTime());
    return query.exec();
}

int DatabaseManager::cleanupExpiredIdempotency(int keepDays)
{
    if (!isOpen() || keepDays <= 0) {
        return -1;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM remote_idempotency WHERE created_at < ?;"));
    query.addBindValue(QDateTime::currentDateTime().addDays(-keepDays));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return -1;
    }
    return query.numRowsAffected();
}

// ---- 内部 ----

bool DatabaseManager::createTables()
{
    const QStringList tableStatements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS project ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "name TEXT NOT NULL,"
                       "version TEXT DEFAULT '1.0',"
                       "create_time DATETIME,"
                       "update_time DATETIME,"
                       "last_open_time DATETIME"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS node ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "parent_id INTEGER DEFAULT 0,"
                       "name TEXT NOT NULL,"
                       "type INTEGER NOT NULL,"
                       "part_no TEXT,"
                       "remark TEXT,"
                       "create_time DATETIME,"
                       "update_time DATETIME,"
                       "deleted INTEGER DEFAULT 0,"
                       "FOREIGN KEY(parent_id) REFERENCES node(id)"
                       ");"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_node_parent ON node(parent_id);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_node_name ON node(name);"),
        QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_node_part_no_component "
                       "ON node(part_no) WHERE type = 2 AND part_no IS NOT NULL;"),
        QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_node_part_no_part "
                       "ON node(parent_id, part_no) WHERE type = 3 AND part_no IS NOT NULL;"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS part ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "node_id INTEGER NOT NULL,"
                       "material TEXT,"
                       "quantity INTEGER DEFAULT 1,"
                       "FOREIGN KEY(node_id) REFERENCES node(id)"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS component ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "node_id INTEGER NOT NULL,"
                       "quantity INTEGER DEFAULT 1,"
                       "FOREIGN KEY(node_id) REFERENCES node(id)"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS drawing ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "part_id INTEGER NOT NULL,"
                       "file_name TEXT NOT NULL,"
                       "file_path TEXT NOT NULL,"
                       "version TEXT NOT NULL,"
                       "is_current INTEGER DEFAULT 1,"
                       "create_time DATETIME,"
                       "deleted INTEGER DEFAULT 0,"
                       "FOREIGN KEY(part_id) REFERENCES part(id)"
                       ");"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_drawing_part ON drawing(part_id);"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS operation_log ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "operation TEXT,"
                       "target_id INTEGER,"
                       "target_type INTEGER,"
                       "create_time DATETIME"
                       ");"),
        // 远程访问已授权设备：首次配对成功后写入，二次连接按 uuid 查密钥与权限
        QStringLiteral("CREATE TABLE IF NOT EXISTS remote_device ("
                       "uuid TEXT PRIMARY KEY,"
                       "device_name TEXT NOT NULL,"
                       "aes_key TEXT NOT NULL,"           // Base64
                       "permission INTEGER NOT NULL DEFAULT 0,"
                       "created_at DATETIME,"
                       "last_seen DATETIME"
                       ");"),
        // 写操作幂等去重：按 (设备uuid, 内容哈希键) 存首次响应，重试命中直接返回首次结果
        QStringLiteral("CREATE TABLE IF NOT EXISTS remote_idempotency ("
                       "device_uuid TEXT NOT NULL,"
                       "id_key TEXT NOT NULL,"
                       "response_json TEXT,"
                       "created_at DATETIME,"
                       "PRIMARY KEY (device_uuid, id_key)"
                       ");")
    };

    if (!beginTransaction()) {
        return false;
    }

    for (const QString &statement : tableStatements) {
        if (!executeSql(statement)) {
            rollbackTransaction();
            qWarning() << "DatabaseManager: 建表失败" << m_lastError;
            return false;
        }
    }

    if (!commitTransaction()) {
        return false;
    }
    // 旧库结构迁移（幂等）：CREATE TABLE IF NOT EXISTS 不会给已存在的表补列
    ensureSchemaMigration();
    return true;
}

void DatabaseManager::ensureSchemaMigration()
{
    // node 表 remark 列：旧库无此列，缺则 ALTER TABLE 补上（可空 TEXT，
    // 规避 SQLite ADD COLUMN 带 NOT NULL 必须提供默认值的限制）
    QSqlQuery info(m_database);
    if (!info.exec(QStringLiteral("PRAGMA table_info(node)"))) {
        qWarning() << "DatabaseManager: 检查 node 表结构失败" << info.lastError().text();
        return;
    }
    bool hasRemark = false;
    while (info.next()) {
        if (info.value(1).toString() == QLatin1String("remark")) {
            hasRemark = true;
            break;
        }
    }
    if (!hasRemark) {
        if (!executeSql(QStringLiteral("ALTER TABLE node ADD COLUMN remark TEXT;"))) {
            qWarning() << "DatabaseManager: node 表迁移 remark 列失败" << m_lastError;
            return;
        }
        qInfo() << "DatabaseManager: node 表已迁移，新增 remark 列";
    }
}

bool DatabaseManager::executeSql(const QString &sql)
{
    QSqlQuery query(m_database);
    if (!query.exec(sql)) {
        m_lastError = query.lastError().text();
        qWarning() << "DatabaseManager: SQL 执行失败" << sql << m_lastError;
        return false;
    }
    return true;
}
