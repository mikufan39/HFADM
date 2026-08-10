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
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

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
    m_layout->addWidget(toolbar);

    m_view = new QPdfView(this);
    m_view->setDocument(nullptr);
    m_layout->addWidget(m_view);

    connect(m_pageSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PdfTabViewer::onPageSpinChanged);
    connect(m_view->pageNavigator(), &QPdfPageNavigator::currentPageChanged,
            this, &PdfTabViewer::onPageNavigated);

    load(filePath);
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
        m_errorLabel = new QLabel(QStringLiteral("PDF 加载失败：%1").arg(filePath), this);
        m_errorLabel->setAlignment(Qt::AlignCenter);
        m_errorLabel->setWordWrap(true);
        m_layout->addWidget(m_errorLabel);
        return false;
    }

    m_view->show();
    m_view->setDocument(m_document);
    m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
    m_pageSpin->setMaximum(m_document->pageCount());
    m_pageSpin->setValue(1);
    m_pageLabel->setText(QStringLiteral(" / 共 %1 页").arg(m_document->pageCount()));
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
