#include "pdfpreviewdialog.h"
#include "pdftabviewer.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QVBoxLayout>

PdfPreviewDialog::PdfPreviewDialog(const QString &filePath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("预览 - %1").arg(QFileInfo(filePath).fileName()));
    resize(760, 680);

    auto *layout = new QVBoxLayout(this);

    // 复用 PdfTabViewer：自带缩放/翻页/适应窗口工具条
    auto *viewer = new PdfTabViewer(filePath, this);
    layout->addWidget(viewer, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::clicked, this, &QDialog::close);
    layout->addWidget(buttons);
}
