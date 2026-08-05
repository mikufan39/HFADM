#include "drawingservice.h"
#include "database/databasemanager.h"
#include "database/drawingrepository.h"
#include "service/nodeservice.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <utility>

DrawingService::DrawingService(DatabaseManager *databaseManager, QObject *parent)
    : QObject(parent)
    , m_databaseManager(databaseManager)
    , m_repository(new DrawingRepository(databaseManager, this))
{
}

void DrawingService::setProjectPath(const QString &projectPath)
{
    m_projectPath = projectPath;
}

QString DrawingService::projectPath() const
{
    return m_projectPath;
}

void DrawingService::setNodeService(NodeService *nodeService)
{
    m_nodeService = nodeService;
}

bool DrawingService::importPdf(qint64 partNodeId, const QString &sourceFilePath)
{
    if (m_projectPath.isEmpty()) {
        m_lastError = QStringLiteral("项目路径未设置");
        qWarning() << "DrawingService: 导入 PDF 失败 - 项目路径未设置";
        return false;
    }
    if (!QFile::exists(sourceFilePath)) {
        m_lastError = QStringLiteral("源文件不存在：%1").arg(sourceFilePath);
        qWarning() << "DrawingService: 导入 PDF 失败 - 源文件不存在" << sourceFilePath;
        return false;
    }
    // 格式限制：仅 PDF
    if (QFileInfo(sourceFilePath).suffix().compare(QStringLiteral("pdf"),
                                                   Qt::CaseInsensitive) != 0) {
        m_lastError = QStringLiteral("仅支持 PDF 格式图纸：%1").arg(sourceFilePath);
        qWarning() << "DrawingService: 导入失败 - 非 PDF 文件" << sourceFilePath;
        return false;
    }

    // 零件名称与完整图号（用于命名）
    HFADMNode partNode;
    if (!m_databaseManager->getNode(partNodeId, partNode)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }
    if (partNode.type != NodeType::Part) {
        m_lastError = QStringLiteral("图纸只能导入到零件下");
        return false;
    }
    QString fullPartNo;
    if (m_nodeService) {
        fullPartNo = m_nodeService->computeFullPartNo(partNodeId);
    }
    if (fullPartNo.isEmpty()) {
        m_lastError = QStringLiteral("零件完整图号未生成，无法命名图纸");
        return false;
    }

    // 零件 part 记录
    Part part;
    if (!m_databaseManager->queryPartByNodeId(partNodeId, part)) {
        m_lastError = m_databaseManager->lastError();
        return false;
    }

    // 下一个版本字母（A -> B -> C...）
    QString versionLetter;
    if (!m_repository->getNextVersionLetter(part.id, versionLetter)) {
        m_lastError = m_repository->lastError();
        return false;
    }

    // 目标文件名与路径（files/ 目录，{完整图号}{版本字母}_{零件名}.pdf）
    const QString fileName = QStringLiteral("%1%2_%3.pdf")
                                 .arg(fullPartNo, versionLetter, partNode.name);
    const QString filesDir = DatabaseManager::composeFilesPath(m_projectPath);
    const QString targetPath = QDir(filesDir).filePath(fileName);
    const QString relativePath = QDir(m_projectPath).relativeFilePath(targetPath);

    if (QFile::exists(targetPath)) {
        m_lastError = QStringLiteral("目标文件已存在：%1").arg(targetPath);
        return false;
    }

    // 复制文件到 files/ 目录
    if (!QFile::copy(sourceFilePath, targetPath)) {
        m_lastError = QStringLiteral("复制 PDF 文件失败：%1").arg(sourceFilePath);
        qWarning() << "DrawingService: 复制文件失败" << sourceFilePath << "->" << targetPath;
        return false;
    }

    // 写入数据库；失败则回滚删除已复制文件
    if (!m_repository->insertDrawing(part.id, fileName, relativePath, versionLetter)) {
        QFile::remove(targetPath);
        m_lastError = m_repository->lastError();
        return false;
    }
    m_databaseManager->addOperationLog(QStringLiteral("IMPORT PDF"),
                                       partNodeId, static_cast<int>(NodeType::Part));

    qInfo() << "DrawingService: PDF 导入成功" << partNode.name
            << "版本" << versionLetter << fileName;
    return true;
}

bool DrawingService::queryDrawings(qint64 partNodeId, QVector<Drawing> &drawings)
{
    if (!m_repository) {
        m_lastError = QStringLiteral("数据访问层为空");
        return false;
    }
    if (!m_repository->queryDrawingsByPartNode(partNodeId, drawings)) {
        m_lastError = m_repository->lastError();
        return false;
    }
    return true;
}

bool DrawingService::getDrawing(qint64 drawingId, Drawing &drawing)
{
    if (!m_repository) {
        m_lastError = QStringLiteral("数据访问层为空");
        return false;
    }
    if (!m_repository->queryDrawingById(drawingId, drawing)) {
        m_lastError = m_repository->lastError();
        return false;
    }
    return true;
}

namespace {
// 版本字母升序比较（A < B < C ...），供删除后重排版本使用
bool drawingVersionLessThan(const Drawing &lhs, const Drawing &rhs)
{
    return lhs.version < rhs.version;
}
} // namespace

bool DrawingService::removeDrawing(qint64 drawingId, DrawingRemovalMode mode)
{
    if (!m_repository) {
        m_lastError = QStringLiteral("数据访问层为空");
        return false;
    }
    if (m_projectPath.isEmpty()) {
        m_lastError = QStringLiteral("项目路径未设置");
        return false;
    }

    // 1. 读取目标图纸（含所属零件节点 id）
    Drawing target;
    if (!m_repository->queryDrawingById(drawingId, target)) {
        m_lastError = m_repository->lastError();
        return false;
    }
    if (target.partNodeId == 0) {
        m_lastError = QStringLiteral("无法定位图纸所属零件");
        return false;
    }

    // 2. 读取该零件全部图纸，按版本字母升序
    QVector<Drawing> drawings;
    if (!m_repository->queryDrawingsByPartNode(target.partNodeId, drawings)) {
        m_lastError = m_repository->lastError();
        return false;
    }
    std::sort(drawings.begin(), drawings.end(), drawingVersionLessThan);

    // 3. 物理删除目标（数据库记录 + 磁盘文件）
    QString filePathToRemove;
    if (!m_repository->permanentDeleteDrawing(drawingId, filePathToRemove)) {
        m_lastError = m_repository->lastError();
        return false;
    }
    if (!filePathToRemove.isEmpty()) {
        const QString fullPath = resolveDrawingPath(m_projectPath, filePathToRemove);
        if (QFile::exists(fullPath) && !QFile::remove(fullPath)) {
            qWarning() << "DrawingService: 删除图纸文件失败" << fullPath;
        }
    }

    // 4. 从剩余列表中移除目标
    QVector<Drawing> remaining;
    remaining.reserve(drawings.size());
    for (const Drawing &d : std::as_const(drawings)) {
        if (d.id != drawingId) {
            remaining.append(d);
        }
    }

    // 5. Renumber：后续版本前移补位，并重命名磁盘文件
    if (mode == DrawingRemovalMode::Renumber) {
        QChar expected('A');
        for (const Drawing &d : remaining) {
            if (d.version.size() == 1 && d.version.at(0) == expected) {
                expected = QChar(expected.unicode() + 1); // 版本号正好衔接，无需改动
                continue;
            }
            const QString newVersion = QString(expected);
            QString newFileName;
            QString newFilePath;
            if (!buildRenamedDrawingInfo(d, newVersion, newFileName, newFilePath)) {
                m_lastError = QStringLiteral("重命名图纸失败：%1").arg(d.fileName);
                return false;
            }
            // 磁盘重命名
            const QString oldFull = resolveDrawingPath(m_projectPath, d.filePath);
            const QString newFull = resolveDrawingPath(m_projectPath, newFilePath);
            if (QFile::exists(oldFull) && !QFile::rename(oldFull, newFull)) {
                m_lastError = QStringLiteral("重命名图纸文件失败：%1").arg(d.fileName);
                return false;
            }
            // 更新数据库记录
            if (!m_repository->updateDrawingVersion(d.id, newVersion, newFileName, newFilePath)) {
                m_lastError = m_repository->lastError();
                return false;
            }
            expected = QChar(expected.unicode() + 1);
        }
    }

    // 6. 若删除的是当前版本且仍有剩余图纸，将剩余中最新版本设为当前
    if (target.isCurrent && !remaining.isEmpty()) {
        qint64 newestId = remaining.first().id;
        QString newestVersion = remaining.first().version;
        for (const Drawing &d : remaining) {
            if (d.version > newestVersion) {
                newestVersion = d.version;
                newestId = d.id;
            }
        }
        if (!m_repository->setCurrentDrawing(target.partNodeId, newestId)) {
            m_lastError = m_repository->lastError();
            return false;
        }
    }

    m_databaseManager->addOperationLog(QStringLiteral("REMOVE DRAWING"), drawingId, 10);
    qInfo() << "DrawingService: 删除图纸成功（物理）" << drawingId
            << (mode == DrawingRemovalMode::Renumber ? "并重排版本" : "保持版本");
    return true;
}

bool DrawingService::setCurrentDrawing(qint64 partNodeId, qint64 drawingId)
{
    if (!m_repository) {
        m_lastError = QStringLiteral("数据访问层为空");
        return false;
    }
    if (!m_repository->setCurrentDrawing(partNodeId, drawingId)) {
        m_lastError = m_repository->lastError();
        return false;
    }
    m_databaseManager->addOperationLog(QStringLiteral("SET CURRENT VERSION"), drawingId, 10);
    qInfo() << "DrawingService: 设置当前版本成功" << drawingId;
    return true;
}

QString DrawingService::resolveDrawingPath(const QString &projectPath, const QString &filePath)
{
    if (QFileInfo(filePath).isAbsolute()) {
        return filePath;
    }
    return QDir(projectPath).filePath(filePath);
}

bool DrawingService::buildRenamedDrawingInfo(const Drawing &drawing, const QString &newVersion,
                                             QString &newFileName, QString &newFilePath) const
{
    newFileName.clear();
    newFilePath.clear();

    // 文件名形如 {完整图号}{版本字母}_{零件名}.pdf，版本字母位于第一个 '_' 前
    // （零件名本身可能包含 '_'，因此用 indexOf 而非 lastIndexOf）
    const QString &oldName = drawing.fileName;
    const int underscore = oldName.indexOf(QLatin1Char('_'));
    if (underscore <= 0) {
        m_lastError = QStringLiteral("图纸文件名格式异常：%1").arg(oldName);
        return false;
    }
    newFileName = oldName.left(underscore - 1) + newVersion + oldName.mid(underscore);

    // 相对路径基于新的文件名重建（files/ 目录下）
    const QString filesDir = DatabaseManager::composeFilesPath(m_projectPath);
    const QString newFullPath = QDir(filesDir).filePath(newFileName);
    newFilePath = QDir(m_projectPath).relativeFilePath(newFullPath);
    return true;
}

QString DrawingService::lastError() const
{
    return m_lastError;
}
