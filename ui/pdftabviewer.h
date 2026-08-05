#ifndef PDFTABVIEWER_H
#define PDFTABVIEWER_H

#include <QWidget>

class QPdfDocument;
class QPdfView;
class QLabel;
class QSpinBox;

// 自包含的 PDF 查看页（QPdfView + 缩放/页码工具条）
// 由 MainWindow 以 Tab 页形式嵌入；加载失败时 isLoaded() 返回 false
class PdfTabViewer : public QWidget
{
    Q_OBJECT

public:
    explicit PdfTabViewer(const QString &filePath, QWidget *parent = nullptr);

    bool isLoaded() const;
    int pageCount() const;

private slots:
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void onPageSpinChanged(int page);
    void onPageNavigated(int page);

private:
    QPdfDocument *m_document = nullptr;
    QPdfView *m_view = nullptr;
    QSpinBox *m_pageSpin = nullptr;
    QLabel *m_pageLabel = nullptr;
};

#endif // PDFTABVIEWER_H
