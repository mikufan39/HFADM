#ifndef DRAWINGOPERATIONS_H
#define DRAWINGOPERATIONS_H

#include <QString>

class QWidget;
class DrawingService;

// 选择 PDF 并导入到零件（含文件选择对话框）；成功返回 true
bool importPdfForPart(QWidget *parent, DrawingService *service,
                      qint64 partNodeId, QString *errorMessage);

// 将指定图纸设为零件当前版本
bool setCurrentVersionForPart(DrawingService *service, qint64 partNodeId,
                              qint64 drawingId, QString *errorMessage);

// 物理删除图纸（含确认对话框，文件一并删除、不可恢复；主窗口目录右键使用）
bool deleteDrawingWithConfirm(QWidget *parent, DrawingService *service,
                              qint64 drawingId, const QString &fileName,
                              QString *errorMessage);

#endif // DRAWINGOPERATIONS_H
