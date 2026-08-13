#ifndef PDFTABVIEWER_H
#define PDFTABVIEWER_H

#include <QWidget>

class QAction;
class QPdfDocument;
class QPdfView;
class QLabel;
class QSpinBox;
class QVBoxLayout;

// 自包含的 PDF 查看页（QPdfView + 缩放/翻页/页码工具条）
// 由 MainWindow 以 Tab 页形式嵌入；加载失败时 isLoaded() 返回 false
// 同时被导入面板预览与图纸速览窗口（PdfPreviewDialog）复用
class PdfTabViewer : public QWidget
{
    Q_OBJECT

public:
    explicit PdfTabViewer(const QString &filePath, QWidget *parent = nullptr);

    // 重新加载另一份 PDF（拖拽导入预览等场景复用同一查看器）
    bool load(const QString &filePath);
    bool isLoaded() const;
    int pageCount() const;

protected:
    // 语言切换：重译工具条动作/页码控件文本（PDF 标签为常驻界面）
    void changeEvent(QEvent *event) override;

private slots:
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void gotoPrevPage();
    void gotoNextPage();
    void onPageSpinChanged(int page);
    void onPageNavigated(int page);

private:
    // 文本统一在此重设（tr() 源串只写一处，构造与语言切换共用）
    void applyTexts();

    QPdfDocument *m_document = nullptr;
    QPdfView *m_view = nullptr;
    // 页码 A/B：A 为可输入当前页（QSpinBox 无按钮+下划线样式），B 为总页数标签
    QSpinBox *m_pageSpin = nullptr;
    QLabel *m_pageTotalLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomFitAction = nullptr;
    QAction *m_prevPageAction = nullptr;
    QAction *m_nextPageAction = nullptr;
    QString m_errorPath; // 加载失败的文件路径（错误标签重译用）
};

#endif // PDFTABVIEWER_H
