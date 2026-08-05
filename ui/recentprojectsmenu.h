#ifndef RECENTPROJECTSMENU_H
#define RECENTPROJECTSMENU_H

#include <QMenu>

class RecentProjectsMenu : public QMenu
{
    Q_OBJECT

public:
    explicit RecentProjectsMenu(QWidget *parent = nullptr);

    // 用最近项目路径列表重建菜单（每条：路径 + 移除按钮）
    void rebuild(const QStringList &projectPaths);

signals:
    void openRequested(const QString &projectPath);
    void removeRequested(const QString &projectPath);
};

#endif // RECENTPROJECTSMENU_H
