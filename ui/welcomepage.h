#ifndef WELCOMEPAGE_H
#define WELCOMEPAGE_H

#include <QPixmap>
#include <QWidget>

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

private:
    QToolButton *makeActionButton(const QString &iconPath, const QString &text);

    QPixmap m_background;
};

#endif // WELCOMEPAGE_H
