#ifndef DRAWINGSERVICE_H
#define DRAWINGSERVICE_H

#include "model/drawing.h"
#include "model/hfdadnode.h"

#include <QObject>
#include <QString>
#include <QVector>

class DatabaseManager;
class DrawingRepository;
class NodeService;

class DrawingService : public QObject
{
    Q_OBJECT

public:
    explicit DrawingService(DatabaseManager *databaseManager, QObject *parent = nullptr);

    // 设置当前项目路径（用于解析 files/ 相对路径）
    void setProjectPath(const QString &projectPath);
    QString projectPath() const;
    // 设置节点服务（用于计算零件完整图号，参与图纸命名）
    void setNodeService(NodeService *nodeService);

    // 导入 PDF 图纸到零件：仅允许 PDF；复制到 files/ 并按 {完整图号}{版本字母}_{零件名}.pdf 命名
    // 版本字母自动递增（A -> B -> C...），首次导入为 A
    bool importPdf(qint64 partNodeId, const QString &sourceFilePath);
    // 查询零件全部图纸（含版本，deleted=0，按版本升序）
    bool queryDrawings(qint64 partNodeId, QVector<Drawing> &drawings);
    // 读取图纸记录
    bool getDrawing(qint64 drawingId, Drawing &drawing);
    // 删除图纸（物理删除，从零件移除，无回收站；主窗口目录右键使用）
    // KeepVersions：仅删除，其余版本字母不变（A/B/C 删 B 后剩 A/C）
    // Renumber：删除后后续版本前移补位（删 B 后 C 变为 B），涉及文件重命名
    enum class DrawingRemovalMode {
        KeepVersions,
        Renumber
    };
    bool removeDrawing(qint64 drawingId, DrawingRemovalMode mode);
    // 设为当前版本
    bool setCurrentDrawing(qint64 partNodeId, qint64 drawingId);

    // 由图纸记录解析出完整文件路径（相对路径拼接项目目录）
    static QString resolveDrawingPath(const QString &projectPath, const QString &filePath);

    QString lastError() const;

private:
    // 由旧图纸记录计算版本前移后的新文件名/相对路径（{图号}{新字母}_{零件名}.pdf）
    bool buildRenamedDrawingInfo(const Drawing &drawing, const QString &newVersion,
                                 QString &newFileName, QString &newFilePath) const;

    DatabaseManager *m_databaseManager;
    DrawingRepository *m_repository = nullptr;
    NodeService *m_nodeService = nullptr;
    QString m_projectPath;
    mutable QString m_lastError;
};

#endif // DRAWINGSERVICE_H
