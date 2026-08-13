#include "pdftabviewer.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QEvent>
#include <QIcon>
#include <QLabel>
#include <QPoint>
#include <QSpinBox>
#include <QToolBar>
#include <QVBoxLayout>

#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>

PdfTabViewer::PdfTabViewer(const QString &filePath, QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    auto *toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly); // 图标按钮（文本仅作 tooltip/无障碍）
    // 缩放：缩小/放大/适应窗口（图标来自 :/assets/PDFView/，文本由 applyTexts() 设置）
    m_zoomOutAction = toolbar->addAction(
        QIcon(QStringLiteral(":/assets/PDFView/suoxiao.svg")), QString(),
        this, &PdfTabViewer::zoomOut);
    m_zoomInAction = toolbar->addAction(
        QIcon(QStringLiteral(":/assets/PDFView/fangda.svg")), QString(),
        this, &PdfTabViewer::zoomIn);
    m_zoomFitAction = toolbar->addAction(
        QIcon(QStringLiteral(":/assets/PDFView/dianshiji.svg")), QString(),
        this, &PdfTabViewer::zoomFit);
    toolbar->addSeparator();
    // 翻页：上一页/下一页（图标来自 :/assets/Icons/）
    m_prevPageAction = toolbar->addAction(
        QIcon(QStringLiteral(":/assets/Icons/arrow-left.svg")), QString(),
        this, &PdfTabViewer::gotoPrevPage);
    m_nextPageAction = toolbar->addAction(
        QIcon(QStringLiteral(":/assets/Icons/arrow-right.svg")), QString(),
        this, &PdfTabViewer::gotoNextPage);
    toolbar->addSeparator();

    // 页码 A/B：A=当前页（可点击输入，下划线样式），B=总页数
    m_pageSpin = new QSpinBox(toolbar);
    m_pageSpin->setMinimum(1);
    m_pageSpin->setMaximum(1);
    m_pageSpin->setButtonSymbols(QAbstractSpinBox::NoButtons); // 无上下箭头，纯输入
    m_pageSpin->setAlignment(Qt::AlignRight);
    m_pageSpin->setFixedWidth(40);
    // 下划线样式（palette(text) 跟随主题深浅色）
    m_pageSpin->setStyleSheet(QStringLiteral(
        "QSpinBox { border: none; border-bottom: 1px solid palette(text);"
        " background: transparent; padding: 0 2px 0 2px; }"));
    toolbar->addWidget(m_pageSpin);

    m_pageTotalLabel = new QLabel(QStringLiteral("/ 1"), toolbar);
    toolbar->addWidget(m_pageTotalLabel);
    m_layout->addWidget(toolbar);

    m_view = new QPdfView(this);
    m_view->setDocument(nullptr);
    m_layout->addWidget(m_view);

    applyTexts();

    connect(m_pageSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PdfTabViewer::onPageSpinChanged);
    connect(m_view->pageNavigator(), &QPdfPageNavigator::currentPageChanged,
            this, &PdfTabViewer::onPageNavigated);

    load(filePath);
}

void PdfTabViewer::applyTexts()
{
    m_zoomOutAction->setText(tr("缩小"));
    m_zoomOutAction->setToolTip(tr("缩小"));
    m_zoomInAction->setText(tr("放大"));
    m_zoomInAction->setToolTip(tr("放大"));
    m_zoomFitAction->setText(tr("适应窗口"));
    m_zoomFitAction->setToolTip(tr("适应窗口"));
    m_prevPageAction->setText(tr("上一页"));
    m_prevPageAction->setToolTip(tr("上一页"));
    m_nextPageAction->setText(tr("下一页"));
    m_nextPageAction->setToolTip(tr("下一页"));
    // 页码 A/B：B 为纯数字，无需翻译；仅文档就绪时刷新
    if (m_document && m_document->status() == QPdfDocument::Status::Ready) {
        m_pageTotalLabel->setText(QStringLiteral("/ %1").arg(m_document->pageCount()));
    } else if (m_errorLabel) {
        m_errorLabel->setText(tr("PDF 加载失败：%1").arg(m_errorPath));
    }
}

void PdfTabViewer::changeEvent(QEvent *event)
{
    // 语言切换：QApplication::installTranslator 后自动收到 LanguageChange，重译工具条文本
    if (event->type() == QEvent::LanguageChange) {
        applyTexts();
    }
    QWidget::changeEvent(event);
}

bool PdfTabViewer::load(const QString &filePath)
{
    // 清除上次加载的错误占位
    if (m_errorLabel) {
        delete m_errorLabel;
        m_errorLabel = nullptr;
    }
    // 先断开视图对旧文档的引用，再重建文档
    m_view->setDocument(nullptr);
    delete m_document;
    m_document = new QPdfDocument(this);
    m_document->load(filePath);
    if (m_document->status() != QPdfDocument::Status::Ready) {
        m_view->hide();
        m_errorPath = filePath; // 供语言切换时重译错误提示
        m_errorLabel = new QLabel(tr("PDF 加载失败：%1").arg(filePath), this);
        m_errorLabel->setAlignment(Qt::AlignCenter);
        m_errorLabel->setWordWrap(true);
        m_layout->addWidget(m_errorLabel);
        return false;
    }

    m_errorPath.clear();
    m_view->show();
    m_view->setDocument(m_document);
    m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
    m_pageSpin->setMaximum(m_document->pageCount());
    m_pageSpin->setValue(1);
    m_pageTotalLabel->setText(QStringLiteral("/ %1").arg(m_document->pageCount()));
    m_view->pageNavigator()->jump(0, QPointF(), 0);
    return true;
}

bool PdfTabViewer::isLoaded() const
{
    return m_document != nullptr
           && m_document->status() == QPdfDocument::Status::Ready;
}

int PdfTabViewer::pageCount() const
{
    return m_document ? m_document->pageCount() : 0;
}

void PdfTabViewer::zoomIn()
{
    if (!m_view) {
        return;
    }
    m_view->setZoomMode(QPdfView::ZoomMode::Custom);
    m_view->setZoomFactor(m_view->zoomFactor() * 1.25);
}

void PdfTabViewer::zoomOut()
{
    if (!m_view) {
        return;
    }
    m_view->setZoomMode(QPdfView::ZoomMode::Custom);
    m_view->setZoomFactor(m_view->zoomFactor() / 1.25);
}

void PdfTabViewer::zoomFit()
{
    if (m_view) {
        m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
    }
}

void PdfTabViewer::gotoPrevPage()
{
    if (!m_view || !m_view->pageNavigator()) {
        return;
    }
    const int cur = m_view->pageNavigator()->currentPage();
    if (cur > 0) {
        m_view->pageNavigator()->jump(cur - 1, QPointF(), 0);
    }
}

void PdfTabViewer::gotoNextPage()
{
    if (!m_view || !m_view->pageNavigator() || !m_document) {
        return;
    }
    const int cur = m_view->pageNavigator()->currentPage();
    if (cur < m_document->pageCount() - 1) {
        m_view->pageNavigator()->jump(cur + 1, QPointF(), 0);
    }
}

void PdfTabViewer::onPageSpinChanged(int page)
{
    if (m_view && m_view->pageNavigator()) {
        m_view->pageNavigator()->jump(page - 1, QPointF(), 0);
    }
}

void PdfTabViewer::onPageNavigated(int page)
{
    if (m_pageSpin && m_document) {
        m_pageSpin->blockSignals(true);
        m_pageSpin->setValue(page + 1);
        m_pageSpin->blockSignals(false);
        m_pageTotalLabel->setText(
            QStringLiteral("/ %1").arg(m_document->pageCount()));
    }
}
