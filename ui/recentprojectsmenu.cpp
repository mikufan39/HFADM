#include "recentprojectsmenu.h"

#include <QDir>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QWidgetAction>

RecentProjectsMenu::RecentProjectsMenu(QWidget *parent)
    : QMenu(parent)
{
}

void RecentProjectsMenu::rebuild(const QStringList &projectPaths)
{
    clear();

    if (projectPaths.isEmpty()) {
        QAction *empty = addAction(QStringLiteral("（无）"));
        empty->setEnabled(false);
        return;
    }

    for (const QString &path : projectPaths) {
        auto *widgetAction = new QWidgetAction(this);
        auto *container = new QWidget;
        auto *layout = new QHBoxLayout(container);
        layout->setContentsMargins(6, 2, 6, 2);
        layout->setSpacing(6);

        auto *openButton = new QPushButton(container);
        const QString display = QDir::toNativeSeparators(path);
        const QFontMetrics fm(openButton->font());
        openButton->setText(fm.elidedText(display, Qt::ElideMiddle, 320));
        openButton->setToolTip(display);
        openButton->setFlat(true);
        openButton->setCursor(Qt::PointingHandCursor);
        layout->addWidget(openButton, 1);

        auto *removeButton = new QToolButton(container);
        removeButton->setText(QStringLiteral("✕"));
        removeButton->setAutoRaise(true);
        removeButton->setToolTip(QStringLiteral("移除记录"));
        layout->addWidget(removeButton);

        connect(openButton, &QPushButton::clicked, this, [this, path] {
            emit openRequested(path);
        });
        connect(removeButton, &QToolButton::clicked, this, [this, path] {
            emit removeRequested(path);
        });

        widgetAction->setDefaultWidget(container);
        addAction(widgetAction);
    }
}
