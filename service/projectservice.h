#ifndef PROJECTSERVICE_H
#define PROJECTSERVICE_H

#include "model/projectinfo.h"

#include <QObject>
#include <QString>

class DatabaseManager;

class ProjectService : public QObject
{
    Q_OBJECT

public:
    explicit ProjectService(QObject *parent = nullptr);
    ~ProjectService() override;

    bool createProject(const QString &projectPath, const QString &projectName);
    bool openProject(const QString &projectPath);
    bool isProjectValid(const QString &projectPath) const;
    // 备份项目：将 hfadm.db 与 files/ 复制到 targetDir 下的独立备份目录
    bool backupProject(const QString &targetDir, QString &backupPath);
    QString lastError() const;
    QString currentProjectPath() const;
    ProjectInfo currentProjectInfo() const;
    DatabaseManager *databaseManager() const;

private:
    bool ensureProjectDirectory(const QString &projectPath);
    bool createFilesDirectory(const QString &projectPath);
    bool copyDirectoryRecursively(const QString &sourceDir, const QString &targetDir);
    // 新建机型时生成默认部件模板（8 个顶层分组 + 20 个子部件，共 28 个，事务化）
    bool createDefaultComponentTemplate(qint64 rootNodeId);

    DatabaseManager *m_databaseManager;
    QString m_projectPath;
    ProjectInfo m_projectInfo;
    QString m_lastError;
};

#endif // PROJECTSERVICE_H
