#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include "ui/tabmanager.h"

#include <QList>
#include <QObject>
#include <QString>

// 会话持久化：关闭时保存标签页状态，启动时恢复上一次的浏览现场
// 记录文件：程序同目录/hfadm.session（INI 格式，QSettings 序列化）
class SessionManager : public QObject
{
    Q_OBJECT

public:
    // 一个标签页的可恢复状态
    struct SessionTab {
        TabManager::TabType type = TabManager::TabType::Directory;
        QString projectPath;      // 所属项目
        qint64 currentNodeId = 0; // 目录模式：当前浏览节点
        QString pdfFilePath;      // PDF 模式：图纸完整路径
    };

    explicit SessionManager(QObject *parent = nullptr);

    // 程序同目录下的会话文件路径
    static QString sessionFilePath();

    // 保存标签页集合与激活索引；返回是否成功
    bool save(const QList<SessionTab> &tabs, int activeIndex);
    // 读取上次会话；activeIndex 输出上次激活的标签索引（无记录时为 0）
    QList<SessionTab> load(int *activeIndex = nullptr) const;

    // 记住详情列表各列宽（键 detailView/count + detailView/colN，与标签会话同文件）；返回是否成功
    bool saveColumnWidths(const QList<int> &widths);
    // 读取上次保存的列宽；无记录或损坏时返回空列表（调用方回退默认值）
    QList<int> loadColumnWidths() const;

private:
    static const int kMaxSessionTabs = 32; // 单次会话标签上限，防止文件无限膨胀
};

#endif // SESSIONMANAGER_H
