#ifndef DRAWINGREPOSITORY_H
#define DRAWINGREPOSITORY_H

#include "model/drawing.h"

#include <QObject>
#include <QString>
#include <QVector>

class DatabaseManager;

// 图纸表（drawing）数据访问层：所有 drawing 相关 SQL 集中于此
// 属于 Database 层内部组件；事务经由 DatabaseManager 统一管理
class DrawingRepository : public QObject
{
    Q_OBJECT

public:
    explicit DrawingRepository(DatabaseManager *databaseManager, QObject *parent = nullptr);

    // 导入新版本：同一零件旧版本 is_current 置 0，新记录为当前版本（事务内完成）
    // version 为版本字母（A/B/C...）
    bool insertDrawing(qint64 partId, const QString &fileName, const QString &filePath,
                       const QString &version);
    bool queryDrawingsByPartNode(qint64 partNodeId, QVector<Drawing> &drawings) const;
    bool queryDrawingById(qint64 drawingId, Drawing &drawing) const;
    bool setCurrentDrawing(qint64 partNodeId, qint64 drawingId);
    // 计算下一个版本字母：当前最大字母 + 1；无图纸返回 "A"
    bool getNextVersionLetter(qint64 partId, QString &letter) const;
    bool permanentDeleteDrawing(qint64 drawingId, QString &filePathToRemove);
    // 更新图纸的版本字母/文件名/路径（删除后重排版本时使用，事务内完成）
    bool updateDrawingVersion(qint64 drawingId, const QString &newVersion,
                              const QString &newFileName, const QString &newFilePath);

    QString lastError() const;

private:
    DatabaseManager *m_databaseManager;
    mutable QString m_lastError;
};

#endif // DRAWINGREPOSITORY_H
