#include "pdftabviewer.h"

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
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->addAction(QStringLiteral("缩小"), this, &PdfTabViewer::zoomOut);
    toolbar->addAction(QStringLiteral("放大"), this, &PdfTabViewer::zoomIn);
    toolbar->addAction(QStringLiteral("适应窗口"), this, &PdfTabViewer::zoomFit);
    toolbar->addSeparator();

    m_pageSpin = new QSpinBox(toolbar);
    m_pageSpin->setMinimum(1);
    m_pageSpin->setMaximum(1);
    m_pageSpin->setPrefix(QStringLiteral("第 "));
    m_pageSpin->setSuffix(QStringLiteral(" 页"));
    toolbar->addWidget(m_pageSpin);

    m_pageLabel = new QLabel(QStringLiteral(" / 共 1 页"), toolbar);
    toolbar->addWidget(m_pageLabel);
    layout->addWidget(toolbar);

    m_view = new QPdfView(this);
    m_view->setDocument(nullptr);
    layout->addWidget(m_view);

    // 加载 PDF；失败时显示占位提示
    m_document = new QPdfDocument(this);
    m_document->load(filePath);
    if (m_document->status() != QPdfDocument::Status::Ready) {
        auto *errorLabel = new QLabel(
            QStringLiteral("PDF 加载失败：%1").arg(filePath), this);
        errorLabel->setAlignment(Qt::AlignCenter);
        errorLabel->setWordWrap(true);
        layout->addWidget(errorLabel);
        m_view->hide();
        return;
    }

    m_view->setDocument(m_document);
    m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
    m_pageSpin->setMaximum(m_document->pageCount());

    connect(m_pageSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PdfTabViewer::onPageSpinChanged);
    connect(m_view->pageNavigator(), &QPdfPageNavigator::currentPageChanged,
            this, &PdfTabViewer::onPageNavigated);

    m_view->pageNavigator()->jump(0, QPointF(), 0);
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
        m_pageLabel->setText(
            QStringLiteral(" / 共 %1 页").arg(m_document->pageCount()));
    }
}
