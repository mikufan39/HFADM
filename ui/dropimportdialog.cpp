#include "dropimportdialog.h"

#include "service/drawingservice.h"
#include "service/nodeservice.h"
#include "ui/pdftabviewer.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// 解析结果状态
enum class ResolveStatus {
    ExistingPart,   // 零件已存在：确认后直接导入
    AutoCreatePart, // 零件不存在：确认后自动创建（材质 Default，数量 1）再导入
    Failed,         // 解析失败（机型不匹配/部件号不存在/文件名无法解析）
};

struct DropFileItem {
    QString sourcePath;
    QString fileName;
    QString fullPartNo;   // 解析出的完整图号（可能为空）
    QString version;      // 解析出的版本字母（展示用，实际版本导入时自动取下一字母）
    QString partName;     // 解析出的零件名（可能为空）
    QString failReason;   // Failed 原因
    QString partDisplay;  // 现有零件显示名
    QString autoPartNo;   // AutoCreatePart：零件图号末段
    qint64 partNodeId = 0;    // ExistingPart：目标零件 id
    qint64 componentId = 0;   // AutoCreatePart：目标部件 id
    ResolveStatus status = ResolveStatus::Failed;
};

// 拖拽导入确认对话框：表格逐行展示解析结果 + 内嵌 PDF 预览（选中行即预览）
class DropImportDialog : public QDialog
{
public:
    DropImportDialog(QWidget *parent, QVector<DropFileItem> &items)
        : QDialog(parent)
        , m_items(items)
    {
        setWindowTitle(QStringLiteral("拖拽导入图纸确认"));
        resize(1000, 520);

        auto *layout = new QVBoxLayout(this);
        auto *hint = new QLabel(
            QStringLiteral("已解析 %1 个 PDF 文件。零件不存在时会自动创建"
                           "（名称取文件名、材质 Default、数量 1）；解析失败的文件将被跳过。"
                           "右侧为选中文件的图纸预览。")
                .arg(items.size()),
            this);
        hint->setWordWrap(true);
        layout->addWidget(hint);

        auto *split = new QHBoxLayout;
        m_table = new QTableWidget(this);
        m_table->setColumnCount(4);
        m_table->setHorizontalHeaderLabels(
            {QStringLiteral("文件名"), QStringLiteral("完整图号"),
             QStringLiteral("版本"), QStringLiteral("目标/说明")});
        m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->verticalHeader()->setVisible(false);
        split->addWidget(m_table, 3);

        m_preview = new PdfTabViewer(QString(), this);
        split->addWidget(m_preview, 2);
        layout->addLayout(split, 1);

        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认导入"));
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);

        rebuildRows();
        connect(m_table, &QTableWidget::itemSelectionChanged, this,
                &DropImportDialog::updatePreview);
        if (m_table->rowCount() > 0) {
            m_table->selectRow(0); // 默认预览第一个文件
        }
    }

private:
    void rebuildRows()
    {
        m_table->clearContents();
        m_table->setRowCount(0);
        for (int i = 0; i < m_items.size(); ++i) {
            const DropFileItem &item = m_items.at(i);
            m_table->insertRow(i);
            m_table->setItem(i, 0, new QTableWidgetItem(item.fileName));
            m_table->setItem(i, 1, new QTableWidgetItem(item.fullPartNo));
            m_table->setItem(i, 2, new QTableWidgetItem(item.version));

            QString targetText;
            QColor targetColor;
            switch (item.status) {
            case ResolveStatus::ExistingPart:
                targetText = QStringLiteral("零件：%1（%2）")
                                 .arg(item.partDisplay, item.fullPartNo);
                break;
            case ResolveStatus::AutoCreatePart:
                targetText = QStringLiteral("将自动创建零件：%1（图号 %2，材质 Default，数量 1）")
                                 .arg(item.partName, item.autoPartNo);
                targetColor = QColor(0x2F, 0x6D, 0xB5);
                break;
            case ResolveStatus::Failed:
                targetText = QStringLiteral("解析失败：%1").arg(item.failReason);
                targetColor = QColor(0xC0, 0x39, 0x2B);
                break;
            }
            auto *targetItem = new QTableWidgetItem(targetText);
            if (targetColor.isValid()) {
                targetItem->setForeground(targetColor);
            }
            m_table->setItem(i, 3, targetItem);
        }
    }

    void updatePreview()
    {
        const int row = m_table->currentRow();
        if (row < 0 || row >= m_items.size()) {
            m_preview->load(QString());
            return;
        }
        m_preview->load(m_items.at(row).sourcePath);
    }

    QVector<DropFileItem> &m_items;
    QTableWidget *m_table = nullptr;
    PdfTabViewer *m_preview = nullptr;
};

} // namespace

bool parseDrawingFileName(const QString &fileName, QString &fullPartNo,
                          QString &version, QString &partName)
{
    fullPartNo.clear();
    version.clear();
    partName.clear();

    QString base = fileName.trimmed();
    const int dot = base.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) {
        base = base.left(dot); // 去扩展名
    }
    const int sep = base.lastIndexOf(QLatin1Char('_'));
    if (sep <= 0 || sep >= base.size() - 1) {
        return false;
    }
    partName = base.mid(sep + 1).trimmed();
    if (partName.isEmpty()) {
        return false;
    }

    // 前缀 = 完整图号 + 版本字母；版本字母为前缀末尾的连续 ASCII 字母
    const QString prefix = base.left(sep);
    int end = prefix.size();
    while (end > 0) {
        const QChar c = prefix.at(end - 1);
        if ((c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
            || (c >= QLatin1Char('a') && c <= QLatin1Char('z'))) {
            --end;
        } else {
            break;
        }
    }
    version = prefix.mid(end);
    fullPartNo = prefix.left(end);

    // 三段式校验：机型名.部件段.零件段；机型名允许字母数字，其余段必须为纯数字
    const QStringList segs = fullPartNo.split(QLatin1Char('.'));
    if (segs.size() < 3) {
        return false;
    }
    for (int i = 0; i < segs.size(); ++i) {
        if (segs.at(i).isEmpty()) {
            return false;
        }
        if (i == 0) {
            continue;
        }
        bool ok = false;
        segs.at(i).toInt(&ok);
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool matchPartByFullPartNo(NodeService *nodeService, const QString &machineName,
                           const QString &fullPartNo, HFADMNode &part)
{
    if (!nodeService || fullPartNo.isEmpty()) {
        return false;
    }
    const QStringList segs = fullPartNo.split(QLatin1Char('.'));
    if (segs.size() < 3) {
        return false;
    }
    // 机型名段必须与当前项目一致（不匹配即解析失败）
    if (!machineName.isEmpty() && segs.first() != machineName) {
        return false;
    }
    HFADMNode component;
    if (!nodeService->findComponentByPartNo(segs.at(1), component)) {
        return false;
    }
    return nodeService->findPartByParentAndPartNo(component.id, segs.last(), part);
}

bool resolveDropImport(QWidget *parent, NodeService *nodeService,
                       DrawingService *drawingService, const QString &machineName,
                       const QStringList &pdfPaths, QVector<QString> &results)
{
    results.clear();
    if (!nodeService || !drawingService) {
        results.append(QStringLiteral("节点/图纸服务不可用"));
        return false;
    }

    // 解析 + 校验：机型名匹配、部件号存在；零件段存在→现有零件，否则→自动创建
    QVector<DropFileItem> items;
    for (const QString &path : pdfPaths) {
        DropFileItem item;
        item.sourcePath = path;
        item.fileName = QFileInfo(path).fileName();
        if (!parseDrawingFileName(item.fileName, item.fullPartNo, item.version, item.partName)) {
            item.status = ResolveStatus::Failed;
            item.failReason = QStringLiteral("文件名无法解析");
            items.append(item);
            continue;
        }

        const QStringList segs = item.fullPartNo.split(QLatin1Char('.'));
        if (segs.size() < 3) {
            item.status = ResolveStatus::Failed;
            item.failReason = QStringLiteral("文件名无法解析");
            items.append(item);
            continue;
        }
        if (!machineName.isEmpty() && segs.first() != machineName) {
            item.status = ResolveStatus::Failed;
            item.failReason = QStringLiteral("机型不匹配（文件机型 %1 ≠ 当前机型 %2）")
                                  .arg(segs.first(), machineName);
            items.append(item);
            continue;
        }

        HFADMNode component;
        if (!nodeService->findComponentByPartNo(segs.at(1), component)) {
            item.status = ResolveStatus::Failed;
            item.failReason = QStringLiteral("部件号 %1 不存在").arg(segs.at(1));
            items.append(item);
            continue;
        }
        item.componentId = component.id;
        item.autoPartNo = segs.last();

        HFADMNode part;
        if (nodeService->findPartByParentAndPartNo(component.id, item.autoPartNo, part)) {
            item.status = ResolveStatus::ExistingPart;
            item.partNodeId = part.id;
            item.partDisplay = part.name;
        } else {
            item.status = ResolveStatus::AutoCreatePart;
        }
        items.append(item);
    }
    if (items.isEmpty()) {
        return false;
    }

    DropImportDialog dlg(parent, items);
    if (dlg.exec() != QDialog::Accepted) {
        return false; // 用户取消
    }

    int okCount = 0;
    int failCount = 0;
    int skipCount = 0;
    for (const DropFileItem &item : items) {
        if (item.status == ResolveStatus::Failed) {
            ++skipCount;
            results.append(QStringLiteral("跳过（%1）：%2").arg(item.failReason, item.fileName));
            continue;
        }

        qint64 targetPartId = item.partNodeId;
        if (item.status == ResolveStatus::AutoCreatePart) {
            qint64 newPartId = 0;
            if (!nodeService->createPart(item.componentId, item.partName, item.autoPartNo,
                                         QStringLiteral("Default"), 1, &newPartId)) {
                ++failCount;
                results.append(QStringLiteral("✗ %1：创建零件失败（%2）")
                                   .arg(item.fileName, nodeService->lastError()));
                continue;
            }
            targetPartId = newPartId;
        }

        if (drawingService->importPdf(targetPartId, item.sourcePath)) {
            ++okCount;
            if (item.status == ResolveStatus::AutoCreatePart) {
                results.append(QStringLiteral("✓ %1 → 已创建零件 %2（Default/1）并导入图纸")
                                   .arg(item.fileName, item.partName));
            } else {
                results.append(QStringLiteral("✓ %1 → 零件 %2")
                                   .arg(item.fileName, item.partDisplay));
            }
        } else {
            ++failCount;
            results.append(QStringLiteral("✗ %1：%2")
                               .arg(item.fileName, drawingService->lastError()));
        }
    }
    results.prepend(QStringLiteral("成功 %1 个，失败 %2 个，跳过 %3 个")
                        .arg(okCount).arg(failCount).arg(skipCount));
    return true;
}
