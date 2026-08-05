#include "drawingoperations.h"
#include "service/drawingservice.h"

#include <QFileDialog>
#include <QMessageBox>

bool importPdfForPart(QWidget *parent, DrawingService *service,
                      qint64 partNodeId, QString *errorMessage)
{
    const QString sourcePath = QFileDialog::getOpenFileName(
        parent, QStringLiteral("导入 PDF 图纸"), QString(), QStringLiteral("PDF 文件 (*.pdf)"));
    if (sourcePath.isEmpty()) {
        return false; // 用户取消不算失败
    }

    if (!service->importPdf(partNodeId, sourcePath)) {
        if (errorMessage) {
            *errorMessage = service->lastError();
        }
        return false;
    }
    return true;
}

bool setCurrentVersionForPart(DrawingService *service, qint64 partNodeId,
                              qint64 drawingId, QString *errorMessage)
{
    if (!service->setCurrentDrawing(partNodeId, drawingId)) {
        if (errorMessage) {
            *errorMessage = service->lastError();
        }
        return false;
    }
    return true;
}

bool deleteDrawingWithConfirm(QWidget *parent, DrawingService *service,
                              qint64 drawingId, const QString &fileName,
                              QString *errorMessage)
{
    const auto answer = QMessageBox::warning(
        parent, QStringLiteral("确认删除"),
        QStringLiteral("确定删除图纸「%1」吗？文件将一并删除，此操作不可恢复。").arg(fileName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return false; // 用户取消不算失败
    }

    // 物理删除（无回收站）：仅删除该图纸，其余版本保持不变
    if (!service->removeDrawing(drawingId, DrawingService::DrawingRemovalMode::KeepVersions)) {
        if (errorMessage) {
            *errorMessage = service->lastError();
        }
        return false;
    }
    return true;
}
