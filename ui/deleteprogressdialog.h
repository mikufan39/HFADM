#ifndef DELETEPROGRESSDIALOG_H
#define DELETEPROGRESSDIALOG_H

#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

// 删除进度弹窗：模态展示删除过程（进度条 + 逐项删除日志 + 状态标签）。
// 删除进行中关闭按钮禁用（防止中途结束）；全部完成或失败后启用，点击关闭结束删除。
class DeleteProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DeleteProgressDialog(QWidget *parent = nullptr);

    // 设置总进度（节点数 + 图纸文件数）与顶部统计摘要
    void setTotal(int nodeCount, int drawingCount, int fileCount);
    // 更新状态标签（如「正在删除：起落架 (AHZ700.3000)」）
    void setCurrentText(const QString &text);
    // 追加一条日志行（自动滚动到底部）
    void addLog(const QString &line);
    // 进度前进一步（默认 1，删除一个节点或清理一个文件后调用）
    void advance(int step = 1);
    // 删除失败：状态标签显示错误原因，启用关闭按钮（剩余删除已中止）
    void setFailed(const QString &message);
    // 删除完成：状态标签显示统计，启用关闭按钮
    void finish();

private:
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QPushButton *m_closeButton = nullptr;
    int m_done = 0;
    int m_total = 0;
};

#endif // DELETEPROGRESSDIALOG_H
