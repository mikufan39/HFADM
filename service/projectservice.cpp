#include "projectservice.h"
#include "database/databasemanager.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

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

    m_projectPath = projectPath;
    if (!m_databaseManager->getProjectInfo(m_projectInfo)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    qInfo() << "ProjectService: 项目创建成功" << projectPath << projectName;
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
