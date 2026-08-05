#ifndef PDFPREVIEWDIALOG_H
#define PDFPREVIEWDIALOG_H

#include <QDialog>
#include <QString>

// PDF 独立速览窗口：内嵌 PdfTabViewer（QPdfView + 缩放/翻页工具条）
// 类似 PowerToys 对 PDF 的速览：独立弹出、基础预览（缩放、翻页、适应窗口）
// 模态打开；加载失败时内部展示错误占位
class PdfPreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PdfPreviewDialog(const QString &filePath, QWidget *parent = nullptr);
};

#endif // PDFPREVIEWDIALOG_H
