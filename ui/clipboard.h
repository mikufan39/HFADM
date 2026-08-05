#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include "model/hfdadnode.h"

#include <QString>
#include <QVector>

class NodeService;
class QWidget;

// 节点剪贴板（复制 / 剪切）：支持一次选中多个节点批量操作
struct NodeClipboard {
    enum class Mode {
        None,
        Copy,
        Cut
    };
    Mode mode = Mode::None;
    QVector<HFADMNode> nodes;
    bool valid() const { return mode != Mode::None && !nodes.isEmpty(); }
    // 状态提示摘要：单个返回名称，多个返回 "N 个项目"
    QString summary() const
    {
        if (nodes.isEmpty()) {
            return QString();
        }
        if (nodes.size() == 1) {
            return nodes.first().name;
        }
        return QStringLiteral("%1 个项目").arg(nodes.size());
    }
};

// 执行粘贴：Cut 移动全部节点（不处理图号冲突）；
// Copy 深拷贝全部子树，若图号与目标位置冲突则弹窗让用户修改图号（循环直至合法）后复制
bool pasteNodeClipboard(QWidget *parent, const NodeClipboard &clip, qint64 targetNodeId,
                        NodeService *nodeService, QString *errorMessage);

#endif // CLIPBOARD_H
