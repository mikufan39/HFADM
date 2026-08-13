#include "deleteprogressdialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTextCursor>
#include <QVBoxLayout>

DeleteProgressDialog::DeleteProgressDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("删除进度"));
    // 去掉系统关闭按钮：删除期间只能由"关闭"按钮结束（删除完成/失败后启用）
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    setModal(true); // 模态：覆盖主窗口，阻止删除期间其他操作
    setMinimumSize(560, 420);

    auto *layout = new QVBoxLayout(this);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    layout->addWidget(m_progressBar);

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(5000);
    m_logView->setPlaceholderText(tr("删除明细将显示在这里…"));
    layout->addWidget(m_logView, 1);

    // 底部：关闭按钮（删除期间禁用，完成后启用）
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_closeButton = buttonBox->button(QDialogButtonBox::Close);
    m_closeButton->setText(tr("关闭"));
    m_closeButton->setEnabled(false);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

void DeleteProgressDialog::setTotal(int nodeCount, int drawingCount, int fileCount)
{
    m_done = 0;
    m_total = nodeCount + fileCount; // 总刻度：节点数 + 图纸文件数
    m_progressBar->setRange(0, qMax(m_total, 1));
    m_progressBar->setValue(0);

    QStringList parts;
    parts << tr("%1 个节点").arg(nodeCount);
    if (drawingCount > 0) {
        parts << tr("%1 张图纸").arg(drawingCount);
    }
    if (fileCount > 0) {
        parts << tr("%1 个文件").arg(fileCount);
    }
    m_summaryLabel->setText(tr("本次将删除：%1，此操作不可恢复。")
                                .arg(parts.join(QStringLiteral("、")))); // 顿号连接符，不参与翻译
}

void DeleteProgressDialog::setCurrentText(const QString &text)
{
    m_statusLabel->setText(text);
}

void DeleteProgressDialog::addLog(const QString &line)
{
    m_logView->appendPlainText(line);
    // 自动滚动到底部，保证最新删除明细可见
    QTextCursor cursor = m_logView->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_logView->setTextCursor(cursor);
}

void DeleteProgressDialog::advance(int step)
{
    m_done += step;
    m_progressBar->setValue(qMin(m_done, m_total));
    if (m_total > 0) {
        const int percent = m_done * 100 / m_total;
        m_progressBar->setFormat(QStringLiteral("%1% (%2/%3)").arg(percent).arg(m_done).arg(m_total));
    }
}

void DeleteProgressDialog::setFailed(const QString &message)
{
    m_statusLabel->setText(tr("删除失败：%1").arg(message));
    m_closeButton->setEnabled(true);
}

void DeleteProgressDialog::finish()
{
    m_progressBar->setValue(m_total);
    m_progressBar->setFormat(QStringLiteral("100% (%1/%1)").arg(m_total));
    m_statusLabel->setText(tr("删除完成"));
    m_closeButton->setEnabled(true);
}
