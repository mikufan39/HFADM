#include <QCoreApplication>
#include "dropimportdialog.h"

#include "service/drawingservice.h"
#include "service/nodeservice.h"
#include "ui/pdftabviewer.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSvgRenderer>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// 解析结果状态
enum class ResolveStatus {
    ExistingPart,   // 零件已存在：导入到现有零件
    AutoCreatePart, // 零件不存在：先自动创建（材质 Default，数量 1）再导入
    Failed,         // 解析失败（文件名无法解析/机型未打开/部件号不存在）
};

// 导入列单元格展示状态
enum class ImportCellState {
    Pending,      // 待导入（可导入）：未选中图标 + 默认色
    Unimportable, // 不可导入（解析失败）：未选中图标 + 红色
    Imported,     // 导入成功：选中图标 + 39C5BB
    Failed,       // 导入失败：错误图标 + 红色
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
    QString projectPath;  // 文件所属机型对应的项目（导入前切换上下文用）
    qint64 partNodeId = 0;    // ExistingPart：目标零件 id
    qint64 componentId = 0;   // AutoCreatePart：目标部件 id
    ResolveStatus status = ResolveStatus::Failed;
};

// 单色 SVG 图标染色（图标 fill 固定 #515151，用 SourceIn 把整图染成目标色；
// tint 无效 = 保持原色）。静态缓存避免重复渲染。
QIcon tintedSvgIcon(const QString &resPath, const QColor &tint, int size = 18)
{
    QSvgRenderer renderer(resPath);
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        renderer.render(&p);
    }
    if (tint.isValid()) {
        QPainter p(&pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), tint);
    }
    return QIcon(pm);
}

// 导入列状态图标：待导入=未选中（原色 #515151）；不可导入=未选中染红；
// 导入成功=选中染青（39C5BB）；导入失败=错误染红
QIcon importPendingIcon()
{
    static const QIcon icon = tintedSvgIcon(QStringLiteral(":/assets/PDFView/weixuanzhong.svg"), QColor());
    return icon;
}
QIcon importUnimportableIcon()
{
    static const QIcon icon = tintedSvgIcon(QStringLiteral(":/assets/PDFView/weixuanzhong.svg"),
                                            QColor(0xC0, 0x39, 0x2B));
    return icon;
}
QIcon importSuccessIcon()
{
    static const QIcon icon = tintedSvgIcon(QStringLiteral(":/assets/PDFView/xuanzhong.svg"),
                                            QColor(0x39, 0xC5, 0xBB));
    return icon;
}
QIcon importErrorIcon()
{
    static const QIcon icon = tintedSvgIcon(QStringLiteral(":/assets/PDFView/chucuo.svg"),
                                            QColor(0xC0, 0x39, 0x2B));
    return icon;
}

// 导入列（纯图标单元格）专用委托：QTableWidgetItem::setTextAlignment 只影响文本对齐，
// 图标水平位置由 QStyleOptionViewItem::decorationAlignment 决定，故在此强制水平居中
class CenteredIconDelegate : public QStyledItemDelegate
{
public:
    explicit CenteredIconDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        option->decorationAlignment = Qt::AlignCenter;
        option->displayAlignment = Qt::AlignCenter;
    }
};

// 拖拽导入确认面板：表格五列（文件名/图号/版本/零件名/导入，列宽可调+支持排序）
// + 内嵌 PDF 预览（选中行即预览）+ 左下角进度条/成功失败计数 + 右下角 开始导入/取消。
// 点击「开始导入」在面板内逐文件执行导入并实时刷新导入列状态，完成后面板保持打开供观察。
class DropImportDialog : public QDialog
{
public:
    DropImportDialog(QWidget *parent, QVector<DropFileItem> &items,
                     const QString &machineName, NodeService *nodeService,
                     DrawingService *drawingService,
                     std::function<bool(const QString &)> switchContext)
        : QDialog(parent)
        , m_items(items)
        , m_nodeService(nodeService)
        , m_drawingService(drawingService)
        , m_switchContext(std::move(switchContext))
    {
        setWindowTitle(QCoreApplication::translate("DropImportDialog", "导入到%1").arg(machineName));
        resize(1080, 560);

        const int importable = countImportable();
        auto *layout = new QVBoxLayout(this);
        auto *hint = new QLabel(
            QCoreApplication::translate("DropImportDialog", "已解析 %1 个文件，预计可以成功导入 %2 个")
                .arg(items.size())
                .arg(importable),
            this);
        hint->setWordWrap(true);
        layout->addWidget(hint);

        auto *split = new QHBoxLayout;
        m_table = new QTableWidget(this);
        m_table->setColumnCount(5);
        m_table->setHorizontalHeaderLabels(
            {QCoreApplication::translate("DropImportDialog", "文件名"), QCoreApplication::translate("DropImportDialog", "图号"),
             QCoreApplication::translate("DropImportDialog", "版本"), QCoreApplication::translate("DropImportDialog", "零件名"),
             QCoreApplication::translate("DropImportDialog", "导入")});
        // 列宽：全部列 Interactive 可拖拽调宽（含文件名列）+ 点击列头排序；
        // 打开时按初始宽度排布（总宽约 610px，与表格区域相当），用户可自由调整
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        m_table->setColumnWidth(0, 200); // 文件名
        m_table->setColumnWidth(1, 140); // 图号
        m_table->setColumnWidth(2, 60);  // 版本
        m_table->setColumnWidth(3, 140); // 零件名
        m_table->setColumnWidth(4, 70);  // 导入
        // 导入列图标居中：单元格仅含图标，文本对齐不影响图标位置，由专用委托强制居中
        m_table->setItemDelegateForColumn(4, new CenteredIconDelegate(m_table));
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->verticalHeader()->setVisible(false);
        m_table->setSortingEnabled(true);
        split->addWidget(m_table, 3);

        m_preview = new PdfTabViewer(QString(), this);
        split->addWidget(m_preview, 2);
        layout->addLayout(split, 1);

        // 底部：左下角进度条 + 成功/失败计数；右下角 开始导入/取消
        auto *bottom = new QHBoxLayout;
        m_progress = new QProgressBar(this);
        m_progress->setRange(0, qMax(1, importable));
        m_progress->setValue(0);
        m_progress->setFixedWidth(260);
        m_progressLabel = new QLabel(
            QCoreApplication::translate("DropImportDialog", "已成功导入 %1 个，失败 %2 个").arg(0).arg(0),
            this);
        bottom->addWidget(m_progress);
        bottom->addWidget(m_progressLabel);
        bottom->addStretch(1);
        m_startButton = new QPushButton(QCoreApplication::translate("DropImportDialog", "开始导入"), this);
        m_cancelButton = new QPushButton(QCoreApplication::translate("DropImportDialog", "取消"), this);
        bottom->addWidget(m_startButton);
        bottom->addWidget(m_cancelButton);
        layout->addLayout(bottom);

        connect(m_startButton, &QPushButton::clicked, this, &DropImportDialog::startImport);
        connect(m_cancelButton, &QPushButton::clicked, this, &DropImportDialog::reject);
        connect(m_table, &QTableWidget::itemSelectionChanged, this,
                &DropImportDialog::updatePreview);

        rebuildRows();
        if (importable == 0) {
            m_startButton->setEnabled(false); // 没有可导入的文件
        }
        if (m_table->rowCount() > 0) {
            m_table->selectRow(0); // 默认预览第一个文件
        }
    }

    // 导入中禁止关闭（Esc / 窗口 X / 取消按钮均经 reject()）
    void reject() override
    {
        if (m_importing) {
            return;
        }
        QDialog::reject();
    }

private slots:
    void startImport()
    {
        if (m_importing) {
            return;
        }
        m_importing = true;
        m_startButton->setEnabled(false);
        m_cancelButton->setEnabled(false);
        m_table->setSortingEnabled(false); // 导入期间行序稳定，按原始顺序逐行推进
        m_progress->setValue(0);
        m_successCount = 0;
        m_failCount = 0;
        m_progressLabel->setText(
            QCoreApplication::translate("DropImportDialog", "已成功导入 %1 个，失败 %2 个").arg(0).arg(0));

        int processed = 0;
        for (int i = 0; i < m_items.size(); ++i) {
            const DropFileItem &item = m_items.at(i);
            if (item.status == ResolveStatus::Failed) {
                continue; // 解析失败：不参与导入
            }

            QString error;
            bool ok = false;
            // 切换到目标机型上下文（跨标签页导入的关键；同项目无操作）
            if (m_switchContext && !m_switchContext(item.projectPath)) {
                error = QCoreApplication::translate("DropImportDialog", "无法切换到项目 %1")
                            .arg(item.projectPath);
            } else {
                qint64 targetPartId = item.partNodeId;
                if (item.status == ResolveStatus::AutoCreatePart) {
                    qint64 newPartId = 0;
                    if (!m_nodeService->createPart(item.componentId, item.partName, item.autoPartNo,
                                                   QStringLiteral("Default"), 1, &newPartId)) {
                        error = m_nodeService->lastError();
                    } else {
                        targetPartId = newPartId;
                    }
                }
                if (error.isEmpty()) {
                    if (m_drawingService->importPdf(targetPartId, item.sourcePath)) {
                        ok = true;
                    } else {
                        error = m_drawingService->lastError();
                    }
                }
            }

            if (ok) {
                updateImportCell(i, ImportCellState::Imported);
                ++m_successCount;
            } else {
                updateImportCell(i, ImportCellState::Failed, error);
                ++m_failCount;
            }
            ++processed;
            m_progress->setValue(processed);
            m_progressLabel->setText(
                QCoreApplication::translate("DropImportDialog", "已成功导入 %1 个，失败 %2 个")
                    .arg(m_successCount)
                    .arg(m_failCount));
            QApplication::processEvents(); // 刷新表格图标与进度条
        }

        m_progress->setValue(m_progress->maximum());
        m_table->setSortingEnabled(true);
        m_importing = false;
        m_cancelButton->setEnabled(true); // 完成：允许关闭面板观察结果
    }

private:
    int countImportable() const
    {
        int n = 0;
        for (const DropFileItem &item : m_items) {
            if (item.status != ResolveStatus::Failed) {
                ++n;
            }
        }
        return n;
    }

    // 排序/预览/更新时按 UserRole 找回原始索引（排序会打乱行序）
    int itemIndexAtRow(int row) const
    {
        if (row < 0) {
            return -1;
        }
        QTableWidgetItem *it = m_table->item(row, 0);
        return it ? it->data(Qt::UserRole).toInt() : -1;
    }

    int rowForItem(int index) const
    {
        for (int r = 0; r < m_table->rowCount(); ++r) {
            QTableWidgetItem *it = m_table->item(r, 0);
            if (it && it->data(Qt::UserRole).toInt() == index) {
                return r;
            }
        }
        return -1;
    }

    void rebuildRows()
    {
        m_table->setSortingEnabled(false); // 填行期间禁止自动排序，行序=插入序
        m_table->clearContents();
        m_table->setRowCount(0);
        for (int i = 0; i < m_items.size(); ++i) {
            const DropFileItem &item = m_items.at(i);
            m_table->insertRow(i);
            auto *nameItem = new QTableWidgetItem(item.fileName);
            nameItem->setData(Qt::UserRole, i); // 原始索引，排序后仍可定位
            m_table->setItem(i, 0, nameItem);
            m_table->setItem(i, 1, new QTableWidgetItem(item.fullPartNo));
            auto *versionItem = new QTableWidgetItem(item.version);
            versionItem->setTextAlignment(Qt::AlignCenter); // 版本字母居中
            m_table->setItem(i, 2, versionItem);
            m_table->setItem(i, 3, new QTableWidgetItem(item.partName));
            m_table->setItem(i, 4, makeImportCell(i));
        }
        m_table->setSortingEnabled(true);
    }

    QTableWidgetItem *makeImportCell(int index) const
    {
        const DropFileItem &item = m_items.at(index);
        auto *cell = new QTableWidgetItem;
        cell->setTextAlignment(Qt::AlignCenter); // 导入状态图标居中
        if (item.status == ResolveStatus::Failed) {
            cell->setIcon(importUnimportableIcon());
            cell->setToolTip(item.failReason);
        } else {
            cell->setIcon(importPendingIcon());
            cell->setToolTip(QCoreApplication::translate("DropImportDialog", "待导入"));
        }
        return cell;
    }

    void updateImportCell(int index, ImportCellState state, const QString &tooltip = QString())
    {
        const int row = rowForItem(index);
        if (row < 0) {
            return;
        }
        QTableWidgetItem *cell = m_table->item(row, 4);
        if (!cell) {
            return;
        }
        switch (state) {
        case ImportCellState::Pending:
            cell->setIcon(importPendingIcon());
            break;
        case ImportCellState::Unimportable:
            cell->setIcon(importUnimportableIcon());
            break;
        case ImportCellState::Imported:
            cell->setIcon(importSuccessIcon());
            break;
        case ImportCellState::Failed:
            cell->setIcon(importErrorIcon());
            break;
        }
        if (!tooltip.isEmpty()) {
            cell->setToolTip(tooltip);
        }
    }

    void updatePreview()
    {
        const int idx = itemIndexAtRow(m_table->currentRow());
        if (idx < 0) {
            m_preview->load(QString());
            return;
        }
        m_preview->load(m_items.at(idx).sourcePath);
    }

    QVector<DropFileItem> &m_items;
    NodeService *m_nodeService = nullptr;
    DrawingService *m_drawingService = nullptr;
    std::function<bool(const QString &)> m_switchContext;
    QTableWidget *m_table = nullptr;
    PdfTabViewer *m_preview = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_progressLabel = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    bool m_importing = false;
    int m_successCount = 0;
    int m_failCount = 0;
};

// 解析/导入过程中会按文件目标机型切换数据库上下文，退出前（含取消）恢复到进入时的激活项目
class ProjectContextRestorer
{
public:
    ProjectContextRestorer(const std::function<bool(const QString &)> &switcher,
                           const QString &target)
        : m_switcher(switcher)
        , m_target(target)
    {
    }
    ~ProjectContextRestorer()
    {
        if (m_switcher && !m_target.isEmpty()) {
            m_switcher(m_target);
        }
    }

private:
    std::function<bool(const QString &)> m_switcher;
    QString m_target;
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
                       DrawingService *drawingService,
                       const QVector<DropTargetProject> &openProjects,
                       const QString &currentMachineName,
                       const QStringList &pdfPaths,
                       const std::function<bool(const QString &projectPath)> &switchContext,
                       const QString &activeProjectPath)
{
    if (!nodeService || !drawingService) {
        return false;
    }

    // 解析 + 校验：文件机型段必须匹配某个已打开的标签页机型；部件号存在；
    // 零件段存在→现有零件，否则→自动创建。解析前先把上下文切到该机型对应的项目。
    QVector<DropFileItem> items;
    for (const QString &path : pdfPaths) {
        DropFileItem item;
        item.sourcePath = path;
        item.fileName = QFileInfo(path).fileName();
        if (!parseDrawingFileName(item.fileName, item.fullPartNo, item.version, item.partName)) {
            item.status = ResolveStatus::Failed;
            item.failReason = QCoreApplication::translate("DropImportDialog", "文件名无法解析");
            items.append(item);
            continue;
        }

        const QStringList segs = item.fullPartNo.split(QLatin1Char('.'));
        if (segs.size() < 3) {
            item.status = ResolveStatus::Failed;
            item.failReason = QCoreApplication::translate("DropImportDialog", "文件名无法解析");
            items.append(item);
            continue;
        }
        // 在已打开的标签页中定位文件机型对应的项目
        const DropTargetProject *target = nullptr;
        for (const DropTargetProject &p : openProjects) {
            if (p.machineName == segs.first()) {
                target = &p;
                break;
            }
        }
        if (!target) {
            item.status = ResolveStatus::Failed;
            item.failReason = QCoreApplication::translate("DropImportDialog", "机型 %1 未打开")
                                  .arg(segs.first());
            items.append(item);
            continue;
        }
        item.projectPath = target->projectPath;
        // 切换到目标机型上下文（与当前激活项目相同则无操作）
        if (switchContext && !switchContext(target->projectPath)) {
            item.status = ResolveStatus::Failed;
            item.failReason = QCoreApplication::translate("DropImportDialog", "无法切换到项目 %1")
                                  .arg(target->projectPath);
            items.append(item);
            continue;
        }

        HFADMNode component;
        if (!nodeService->findComponentByPartNo(segs.at(1), component)) {
            item.status = ResolveStatus::Failed;
            item.failReason = QCoreApplication::translate("DropImportDialog", "部件号 %1 不存在").arg(segs.at(1));
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

    // 退出前（含取消/完成）恢复进入时的激活项目上下文
    ProjectContextRestorer restorer(switchContext, activeProjectPath);

    DropImportDialog dlg(parent, items, currentMachineName, nodeService, drawingService,
                         switchContext);
    dlg.exec(); // 面板内完成导入；关闭后返回（不再弹结果框）
    return true;
}
