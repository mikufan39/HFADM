#include "sessionmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>

// qMin 对 kMaxSessionTabs 的 odr-use 需要类外定义（C++17 静态成员）
const int SessionManager::kMaxSessionTabs;

SessionManager::SessionManager(QObject *parent)
    : QObject(parent)
{
}

QString SessionManager::sessionFilePath()
{
    // 程序同目录（开发构建目录可写；发布版如需只读保护可另行调整）
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("hfadm.session"));
}

bool SessionManager::save(const QList<SessionTab> &tabs, int activeIndex)
{
    QSettings settings(sessionFilePath(), QSettings::IniFormat);
    // hfadm.session 同时是软件配置文件（AppConfig 维护客户端凭证/服务端黑名单），
    // 这里只清理本组件维护的键（tab*/activeIndex/count），不清空整个文件
    const QStringList keys = settings.allKeys();
    for (const QString &key : keys) {
        if (key == QLatin1String("activeIndex") || key == QLatin1String("count")
            || key.startsWith(QLatin1String("tab"))) {
            settings.remove(key);
        }
    }
    settings.setValue(QStringLiteral("activeIndex"), activeIndex);

    const int count = qMin(tabs.size(), kMaxSessionTabs);
    settings.setValue(QStringLiteral("count"), count);
    for (int i = 0; i < count; ++i) {
        const SessionTab &tab = tabs.at(i);
        const QString prefix = QStringLiteral("tab%1.").arg(i);
        settings.setValue(prefix + QStringLiteral("type"), static_cast<int>(tab.type));
        settings.setValue(prefix + QStringLiteral("project"), tab.projectPath);
        settings.setValue(prefix + QStringLiteral("nodeId"), tab.currentNodeId);
        settings.setValue(prefix + QStringLiteral("pdf"), tab.pdfFilePath);
    }
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QList<SessionManager::SessionTab> SessionManager::load(int *activeIndex) const
{
    QList<SessionTab> tabs;
    if (activeIndex) {
        *activeIndex = 0;
    }

    const QString path = sessionFilePath();
    if (!QFile::exists(path)) {
        return tabs; // 无记录 = 默认状态
    }

    QSettings settings(path, QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
        qWarning() << "SessionManager: 会话文件读取失败" << path;
        return tabs;
    }

    if (activeIndex) {
        *activeIndex = settings.value(QStringLiteral("activeIndex"), 0).toInt();
    }
    const int count = settings.value(QStringLiteral("count"), 0).toInt();
    for (int i = 0; i < count; ++i) {
        const QString prefix = QStringLiteral("tab%1.").arg(i);
        SessionTab tab;
        tab.type = static_cast<TabManager::TabType>(
            settings.value(prefix + QStringLiteral("type"), 0).toInt());
        tab.projectPath = settings.value(prefix + QStringLiteral("project")).toString();
        tab.currentNodeId = settings.value(prefix + QStringLiteral("nodeId"), 0).toLongLong();
        tab.pdfFilePath = settings.value(prefix + QStringLiteral("pdf")).toString();
        if (tab.projectPath.isEmpty()) {
            continue; // 缺项目信息的损坏记录跳过
        }
        tabs.append(tab);
    }
    return tabs;
}
