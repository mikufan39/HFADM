#include "projectservice.h"
#include "database/databasemanager.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

// 新建机型默认部件模板（2026-08-10）：顶层分组 + 子部件两级结构。
// 图号段为 0–9999 纯数字（符合部件图号规则，全机型唯一）；名称为模板中文名。
struct TemplateChild {
    const char *partNo;
    const char *name;
};
struct TemplateGroup {
    const char *partNo;
    const char *name;
    const TemplateChild *children;
    int childCount;
};

const TemplateChild kBodyChildren[] = {
    {"1010", "主机身"}, {"1020", "起落架"}, {"1030", "尾管"}, {"1040", "外壳"},
};
const TemplateChild kPowerChildren[] = {
    {"2010", "发动机座"}, {"2020", "发动机本体"}, {"2030", "水路"},
    {"2040", "油路"}, {"2050", "气路"},
};
const TemplateChild kTransmissionChildren[] = {
    {"3010", "前减速器"}, {"3020", "后减速器"}, {"3030", "传动连杆"},
};
const TemplateChild kRotorChildren[] = {
    {"4010", "前旋翼"}, {"4020", "后旋翼"}, {"4030", "前倾斜盘"},
    {"4040", "后倾斜盘"}, {"4050", "连杆"}, {"4060", "舵机"},
};
const TemplateChild kAvionicsChildren[] = {
    {"5010", "航电模块"}, {"5020", "线路"},
};

const TemplateGroup kDefaultComponentTemplate[] = {
    {"1000", "机体", kBodyChildren, 4},
    {"2000", "动力", kPowerChildren, 5},
    {"3000", "传动", kTransmissionChildren, 3},
    {"4000", "旋翼", kRotorChildren, 6},
    {"5000", "航电", kAvionicsChildren, 2},
    {"6000", "标准件", nullptr, 0},
    {"7000", "外购件", nullptr, 0},
    {"8000", "其他", nullptr, 0},
};

} // namespace

ProjectService::ProjectService(QObject *parent)
    : QObject(parent)
    , m_databaseManager(new DatabaseManager(this))
{
}

ProjectService::~ProjectService() = default;

bool ProjectService::createProject(const QString &projectPath, const QString &projectName)
{
    if (projectPath.isEmpty() || projectName.isEmpty()) {
        m_lastError = QStringLiteral("项目路径和机型名称不能为空");
        qWarning() << "ProjectService: 创建项目失败 - 路径或名称为空";
        return false;
    }

    if (!ensureProjectDirectory(projectPath)) {
        return false;
    }

    // 目录已存在且已包含旧项目时拒绝创建，避免产生重复项目记录
    if (isProjectValid(projectPath)) {
        m_lastError = QStringLiteral("该目录已是一个 HFADM 项目，不能重复创建");
        qWarning() << "ProjectService: 创建项目失败 - 目录已包含有效项目" << projectPath;
        return false;
    }

    if (!createFilesDirectory(projectPath)) {
        return false;
    }

    const QString databasePath = DatabaseManager::composeDatabasePath(projectPath);
    if (!m_databaseManager->openDatabase(databasePath)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    if (!m_databaseManager->initializeDatabase()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    if (!m_databaseManager->insertProject(projectName, QStringLiteral("1.0"))) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    // 创建项目根节点（机型，type=Aircraft，parent_id=0）
    if (!m_databaseManager->insertNode(0, projectName, NodeType::Aircraft)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    // 创建默认部件模板（事务化：任一步失败整体回滚，不留下半套部件）
    const qint64 rootNodeId = m_databaseManager->lastInsertId();
    if (!createDefaultComponentTemplate(rootNodeId)) {
        return false;
    }

    m_projectPath = projectPath;
    if (!m_databaseManager->getProjectInfo(m_projectInfo)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    qInfo() << "ProjectService: 项目创建成功" << projectPath << projectName;
    return true;
}

bool ProjectService::createDefaultComponentTemplate(qint64 rootNodeId)
{
    if (rootNodeId == 0 || !m_databaseManager) {
        m_lastError = QStringLiteral("机型根节点无效，无法生成默认部件");
        return false;
    }
    if (!m_databaseManager->beginTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    int createdCount = 0;
    for (const TemplateGroup &group : kDefaultComponentTemplate) {
        const QString groupNo = QString::fromUtf8(group.partNo);
        const QString groupName = QString::fromUtf8(group.name);
        if (!m_databaseManager->insertNode(rootNodeId, groupName, NodeType::Component, groupNo)) {
            m_databaseManager->rollbackTransaction();
            m_lastError = m_databaseManager->lastError();
            return false;
        }
        const qint64 groupId = m_databaseManager->lastInsertId();
        if (!m_databaseManager->insertComponent(groupId, 1)) {
            m_databaseManager->rollbackTransaction();
            m_lastError = m_databaseManager->lastError();
            return false;
        }
        m_databaseManager->addOperationLog(QStringLiteral("CREATE COMPONENT"), groupId,
                                           static_cast<int>(NodeType::Component));
        ++createdCount;

        for (int i = 0; i < group.childCount; ++i) {
            const TemplateChild &child = group.children[i];
            if (!m_databaseManager->insertNode(groupId, QString::fromUtf8(child.name),
                                               NodeType::Component,
                                               QString::fromUtf8(child.partNo))) {
                m_databaseManager->rollbackTransaction();
                m_lastError = m_databaseManager->lastError();
                return false;
            }
            const qint64 childId = m_databaseManager->lastInsertId();
            if (!m_databaseManager->insertComponent(childId, 1)) {
                m_databaseManager->rollbackTransaction();
                m_lastError = m_databaseManager->lastError();
                return false;
            }
            m_databaseManager->addOperationLog(QStringLiteral("CREATE COMPONENT"), childId,
                                               static_cast<int>(NodeType::Component));
            ++createdCount;
        }
    }

    if (!m_databaseManager->commitTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    qInfo() << "ProjectService: 默认部件模板创建成功" << createdCount << "个部件";
    return true;
}

bool ProjectService::openProject(const QString &projectPath)
{
    if (!isProjectValid(projectPath)) {
        m_lastError = QStringLiteral("不是有效的 HFADM 项目目录");
        qWarning() << "ProjectService: 打开项目失败 - 目录无效" << projectPath;
        return false;
    }

    const QString databasePath = DatabaseManager::composeDatabasePath(projectPath);
    if (!m_databaseManager->openDatabase(databasePath)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    if (!m_databaseManager->initializeDatabase()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    if (!m_databaseManager->getProjectInfo(m_projectInfo)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    if (!m_databaseManager->updateProjectLastOpenTime()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    m_projectPath = projectPath;
    qInfo() << "ProjectService: 项目打开成功" << projectPath << m_projectInfo.name;
    return true;
}

bool ProjectService::isProjectValid(const QString &projectPath) const
{
    if (projectPath.isEmpty()) {
        return false;
    }

    QDir projectDir(projectPath);
    const bool dbExists = projectDir.exists(DatabaseManager::projectDatabaseFilename());
    const bool filesExists = projectDir.exists(DatabaseManager::projectFilesDirectoryName());
    return dbExists && filesExists;
}

QString ProjectService::lastError() const
{
    return m_lastError;
}

QString ProjectService::currentProjectPath() const
{
    return m_projectPath;
}

ProjectInfo ProjectService::currentProjectInfo() const
{
    return m_projectInfo;
}

DatabaseManager *ProjectService::databaseManager() const
{
    return m_databaseManager;
}

bool ProjectService::ensureProjectDirectory(const QString &projectPath)
{
    QDir projectDir(projectPath);
    if (projectDir.exists()) {
        return true;
    }

    if (!QDir().mkpath(projectPath)) {
        m_lastError = QStringLiteral("创建项目目录失败：%1").arg(projectPath);
        qWarning() << "ProjectService: 创建目录失败" << m_lastError;
        return false;
    }

    return true;
}

bool ProjectService::createFilesDirectory(const QString &projectPath)
{
    const QString filesPath = DatabaseManager::composeFilesPath(projectPath);
    QDir filesDir(filesPath);
    if (filesDir.exists()) {
        return true;
    }

    if (!QDir().mkpath(filesPath)) {
        m_lastError = QStringLiteral("创建 files 目录失败：%1").arg(filesPath);
        qWarning() << "ProjectService: 创建 files 目录失败" << m_lastError;
        return false;
    }

    return true;
}

bool ProjectService::backupProject(const QString &targetDir, QString &backupPath)
{
    if (m_projectPath.isEmpty()) {
        m_lastError = QStringLiteral("当前没有打开的项目");
        return false;
    }

    // 先 checkpoint，确保主库文件包含全部数据（WAL 模式下主库可能滞后）
    if (!m_databaseManager->checkpointWal()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    // 备份目录：targetDir/<项目名>_<时间戳>
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString backupName = QStringLiteral("%1_%2").arg(m_projectInfo.name, stamp);
    const QString backupRoot = QDir(targetDir).filePath(backupName);
    const QString backupFilesDir = QDir(backupRoot).filePath(DatabaseManager::projectFilesDirectoryName());

    if (!QDir().mkpath(backupFilesDir)) {
        m_lastError = QStringLiteral("创建备份目录失败：%1").arg(backupRoot);
        qWarning() << "ProjectService: 创建备份目录失败" << m_lastError;
        return false;
    }

    // 复制数据库文件
    const QString dbSource = DatabaseManager::composeDatabasePath(m_projectPath);
    const QString dbTarget = DatabaseManager::composeDatabasePath(backupRoot);
    if (!QFile::copy(dbSource, dbTarget)) {
        m_lastError = QStringLiteral("复制数据库文件失败");
        qWarning() << "ProjectService: 复制数据库失败" << dbSource << "->" << dbTarget;
        return false;
    }

    // 复制 files 目录（图纸文件）
    const QString filesSource = DatabaseManager::composeFilesPath(m_projectPath);
    if (!copyDirectoryRecursively(filesSource, backupFilesDir)) {
        QDir(backupRoot).removeRecursively();
        return false;
    }

    backupPath = backupRoot;
    qInfo() << "ProjectService: 项目备份成功" << backupRoot;
    return true;
}

bool ProjectService::copyDirectoryRecursively(const QString &sourceDir, const QString &targetDir)
{
    QDir source(sourceDir);
    if (!source.exists()) {
        return true; // files 目录为空/不存在时视为成功
    }

    if (!QDir().mkpath(targetDir)) {
        m_lastError = QStringLiteral("创建备份目录失败：%1").arg(targetDir);
        return false;
    }

    const QFileInfoList entries = source.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString targetPath = QDir(targetDir).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectoryRecursively(entry.absoluteFilePath(), targetPath)) {
                return false;
            }
        } else if (!QFile::copy(entry.absoluteFilePath(), targetPath)) {
            m_lastError = QStringLiteral("复制文件失败：%1").arg(entry.fileName());
            qWarning() << "ProjectService: 复制文件失败" << entry.absoluteFilePath();
            return false;
        }
    }
    return true;
}
