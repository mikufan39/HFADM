#include "drawingrepository.h"
#include "databasemanager.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

DrawingRepository::DrawingRepository(DatabaseManager *databaseManager, QObject *parent)
    : QObject(parent)
    , m_databaseManager(databaseManager)
{
}

bool DrawingRepository::insertDrawing(qint64 partId, const QString &fileName,
                                      const QString &filePath, const QString &version)
{
    if (!m_databaseManager || !m_databaseManager->isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    if (!m_databaseManager->beginTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    // 旧版本取消当前标记
    {
        QSqlQuery query(m_databaseManager->database());
        query.prepare(QStringLiteral("UPDATE drawing SET is_current = 0 WHERE part_id = ?;"));
        query.addBindValue(partId);
        if (!query.exec()) {
            m_databaseManager->rollbackTransaction();
            m_lastError = query.lastError().text();
            qWarning() << "DrawingRepository: 取消旧版本当前标记失败" << m_lastError;
            return false;
        }
    }

    {
        QSqlQuery query(m_databaseManager->database());
        query.prepare(QStringLiteral(
            "INSERT INTO drawing (part_id, file_name, file_path, version, is_current, create_time, deleted) "
            "VALUES (?, ?, ?, ?, 1, ?, 0);"));
        query.addBindValue(partId);
        query.addBindValue(fileName);
        query.addBindValue(filePath);
        query.addBindValue(version);
        query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        if (!query.exec()) {
            m_databaseManager->rollbackTransaction();
            m_lastError = query.lastError().text();
            qWarning() << "DrawingRepository: 插入图纸记录失败" << m_lastError;
            return false;
        }
    }

    if (!m_databaseManager->commitTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    return true;
}

bool DrawingRepository::queryDrawingsByPartNode(qint64 partNodeId, QVector<Drawing> &drawings) const
{
    drawings.clear();
    if (!m_databaseManager || !m_databaseManager->isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral(
        "SELECT d.id, d.part_id, d.file_name, d.file_path, d.version, d.is_current, d.create_time, d.deleted "
        "FROM drawing d JOIN part p ON d.part_id = p.id "
        "WHERE p.node_id = ? AND d.deleted = 0 "
        "ORDER BY d.create_time DESC, d.id DESC;"));
    query.addBindValue(partNodeId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DrawingRepository: 查询图纸失败" << m_lastError;
        return false;
    }

    while (query.next()) {
        Drawing drawing;
        drawing.id = query.value(0).toLongLong();
        drawing.partId = query.value(1).toLongLong();
        drawing.fileName = query.value(2).toString();
        drawing.filePath = query.value(3).toString();
        drawing.version = query.value(4).toString();
        drawing.isCurrent = query.value(5).toInt() != 0;
        drawing.createTime = query.value(6).toDateTime();
        drawing.deleted = query.value(7).toInt() != 0;
        drawings.append(drawing);
    }
    return true;
}

bool DrawingRepository::queryDrawingById(qint64 drawingId, Drawing &drawing) const
{
    if (!m_databaseManager || !m_databaseManager->isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral(
        "SELECT d.id, d.part_id, p.node_id, d.file_name, d.file_path, d.version, "
        "       d.is_current, d.create_time, d.deleted "
        "FROM drawing d JOIN part p ON d.part_id = p.id WHERE d.id = ?;"));
    query.addBindValue(drawingId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DrawingRepository: 读取图纸记录失败" << m_lastError;
        return false;
    }

    if (!query.next()) {
        m_lastError = QStringLiteral("图纸记录不存在（id=%1）").arg(drawingId);
        return false;
    }

    drawing.id = query.value(0).toLongLong();
    drawing.partId = query.value(1).toLongLong();
    drawing.partNodeId = query.value(2).toLongLong();
    drawing.fileName = query.value(3).toString();
    drawing.filePath = query.value(4).toString();
    drawing.version = query.value(5).toString();
    drawing.isCurrent = query.value(6).toInt() != 0;
    drawing.createTime = query.value(7).toDateTime();
    drawing.deleted = query.value(8).toInt() != 0;
    return true;
}

bool DrawingRepository::setCurrentDrawing(qint64 partNodeId, qint64 drawingId)
{
    if (!m_databaseManager || !m_databaseManager->isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    if (!m_databaseManager->beginTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    {
        QSqlQuery query(m_databaseManager->database());
        query.prepare(QStringLiteral(
            "UPDATE drawing SET is_current = 0 WHERE part_id IN ("
            "  SELECT id FROM part WHERE node_id = ?);"));
        query.addBindValue(partNodeId);
        if (!query.exec()) {
            m_databaseManager->rollbackTransaction();
            m_lastError = query.lastError().text();
            qWarning() << "DrawingRepository: 取消当前版本标记失败" << m_lastError;
            return false;
        }
    }

    {
        QSqlQuery query(m_databaseManager->database());
        query.prepare(QStringLiteral("UPDATE drawing SET is_current = 1 WHERE id = ?;"));
        query.addBindValue(drawingId);
        if (!query.exec()) {
            m_databaseManager->rollbackTransaction();
            m_lastError = query.lastError().text();
            qWarning() << "DrawingRepository: 设置当前版本失败" << m_lastError;
            return false;
        }
    }

    if (!m_databaseManager->commitTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    return true;
}

bool DrawingRepository::getNextVersionLetter(qint64 partId, QString &letter) const
{
    letter = QStringLiteral("A");
    if (!m_databaseManager || !m_databaseManager->isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    // 取该零件全部版本字母（含已删除记录，避免文件被覆盖），按 ASCII 降序取最大
    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral(
        "SELECT version FROM drawing WHERE part_id = ?;"));
    query.addBindValue(partId);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "DrawingRepository: 查询图纸版本失败" << m_lastError;
        return false;
    }

    QChar maxLetter(0);
    while (query.next()) {
        const QString v = query.value(0).toString();
        if (v.size() == 1) {
            const QChar c = v.at(0);
            if (c.isLetter() && c.toUpper() > maxLetter) {
                maxLetter = c.toUpper();
            }
        }
    }
    if (maxLetter.unicode() != 0) {
        letter = QChar(maxLetter.unicode() + 1); // A -> B，Z 之后不再处理（需求明确）
    }
    return true;
}

bool DrawingRepository::permanentDeleteDrawing(qint64 drawingId, QString &filePathToRemove)
{
    filePathToRemove.clear();
    if (!m_databaseManager || !m_databaseManager->isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    if (!m_databaseManager->beginTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    {
        QSqlQuery query(m_databaseManager->database());
        query.prepare(QStringLiteral("SELECT file_path FROM drawing WHERE id = ?;"));
        query.addBindValue(drawingId);
        if (!query.exec()) {
            m_databaseManager->rollbackTransaction();
            m_lastError = query.lastError().text();
            return false;
        }
        if (query.next()) {
            filePathToRemove = query.value(0).toString();
        }
    }

    {
        QSqlQuery query(m_databaseManager->database());
        query.prepare(QStringLiteral("DELETE FROM drawing WHERE id = ?;"));
        query.addBindValue(drawingId);
        if (!query.exec()) {
            m_databaseManager->rollbackTransaction();
            m_lastError = query.lastError().text();
            qWarning() << "DrawingRepository: 永久删除图纸记录失败" << m_lastError;
            return false;
        }
    }

    if (!m_databaseManager->commitTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    return true;
}

bool DrawingRepository::updateDrawingVersion(qint64 drawingId, const QString &newVersion,
                                             const QString &newFileName,
                                             const QString &newFilePath)
{
    if (!m_databaseManager || !m_databaseManager->isOpen()) {
        m_lastError = QStringLiteral("数据库未打开");
        return false;
    }

    if (!m_databaseManager->beginTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral(
        "UPDATE drawing SET version = ?, file_name = ?, file_path = ? WHERE id = ?;"));
    query.addBindValue(newVersion);
    query.addBindValue(newFileName);
    query.addBindValue(newFilePath);
    query.addBindValue(drawingId);
    if (!query.exec()) {
        m_databaseManager->rollbackTransaction();
        m_lastError = query.lastError().text();
        qWarning() << "DrawingRepository: 更新图纸版本失败" << m_lastError;
        return false;
    }

    if (!m_databaseManager->commitTransaction()) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    return true;
}

QString DrawingRepository::lastError() const
{
    return m_lastError;
}
