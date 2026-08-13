#ifndef WELCOMEPAGE_H
#define WELCOMEPAGE_H

#include <QPixmap>
#include <QWidget>

class QLabel;
class QToolButton;

// 欢迎页（启动覆盖层）：软件首次启动或无可恢复会话时全屏显示
// 覆盖整个主窗口内容区，提供 创建项目 / 打开项目 / 连接到远程 三个入口
class WelcomePage : public QWidget
{
    Q_OBJECT

public:
    explicit WelcomePage(QWidget *parent = nullptr);

signals:
    void createProjectRequested();
    void openProjectRequested();
    void connectRemoteRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    // 语言切换：重新设置标题与三个按钮文本（Qt 原生多语言刷新入口）
    void changeEvent(QEvent *event) override;

private:
    QToolButton *makeActionButton(const QString &iconPath, const QString &text);
    // 文本统一在此重设（tr() 源串只写一处，构造与语言切换共用）
    void applyTexts();

    QPixmap m_background;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_createButton = nullptr;
    QToolButton *m_openButton = nullptr;
    QToolButton *m_remoteButton = nullptr;
};

#endif // WELCOMEPAGE_H
