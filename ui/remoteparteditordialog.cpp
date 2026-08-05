#include "remoteparteditordialog.h"
#include "pdfpreviewdialog.h"
#include "model/drawing.h"
#include "model/hfdadnode.h"
#include "model/part.h"
#include "service/remoteclient.h"

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// 远程零件编辑对话框（数据全部经 RemoteClient 协议）
class RemotePartEditorDialog : public QDialog
{
public:
    RemotePartEditorDialog(QWidget *parent, RemoteClient *client, const HFADMNode &node)
        : QDialog(parent)
        , m_client(client)
        , m_node(node)
    {
        setWindowTitle(QStringLiteral("零件编辑（远程）"));
        setMinimumWidth(640);

        if (!loadPartData()) {
            reject();
            return;
        }
        buildUi();
        refreshDrawingList();
    }

    // 是否有修改（供调用方决定是否刷新目录）
    bool changed() const { return m_changed; }

private:
    bool loadPartData()
    {
        QString err;
        Part part;
        if (!m_client->loadPart(m_node.id, part, &err)) {
            m_error = err;
            return false;
        }
        m_part = part;

        // 图号前缀 = 完整图号去掉本段部分（镜像本地零件编辑器）
        QString full;
        if (!m_client->computeFullPartNo(m_node.id, full, &err)) {
            m_error = err;
            return false;
        }
        const QString partNo = m_node.partNo;
        if (!full.isEmpty() && !partNo.isEmpty() && full.endsWith(partNo)) {
            m_partNoPrefix = full.left(full.size() - partNo.size());
        } else if (!full.isEmpty()) {
            m_partNoPrefix = full + QStringLiteral(".");
        }
        return true;
    }

    void buildUi()
    {
        auto *layout = new QVBoxLayout(this);

        auto *form = new QFormLayout;
        m_nameEdit = new QLineEdit(m_node.name, this);
        m_partNoEdit = new QLineEdit(m_node.partNo, this);
        auto *prefixLabel = new QLabel(m_partNoPrefix, this);
        m_fullPartNoPreview = new QLabel(this);
        m_materialEdit = new QLineEdit(m_part.material, this);
        m_quantitySpin = new QSpinBox(this);
        m_quantitySpin->setRange(1, 999999);
        m_quantitySpin->setValue(m_part.quantity > 0 ? m_part.quantity : 1);

        connect(m_partNoEdit, &QLineEdit::textChanged, this,
                [this](const QString &text) {
                    m_fullPartNoPreview->setText(
                        QStringLiteral("%1<b>%2</b>").arg(m_partNoPrefix, text.trimmed()));
                });
        m_fullPartNoPreview->setText(
            QStringLiteral("%1<b>%2</b>").arg(m_partNoPrefix, m_node.partNo));

        form->addRow(QStringLiteral("零件名称："), m_nameEdit);
        form->addRow(QStringLiteral("图号前缀："), prefixLabel);
        form->addRow(QStringLiteral("图号本段："), m_partNoEdit);
        form->addRow(QStringLiteral("完整图号："), m_fullPartNoPreview);
        form->addRow(QStringLiteral("材质："), m_materialEdit);
        form->addRow(QStringLiteral("数量："), m_quantitySpin);
        layout->addLayout(form);

        // 图纸区域：版本列表 + 当前标记 + 预览/导出/删除
        auto *drawingLabel = new QLabel(QStringLiteral("图纸"), this);
        drawingLabel->setStyleSheet(QStringLiteral("font-weight: 500;"));
        layout->addWidget(drawingLabel);

        m_drawingTable = new QTableWidget(this);
        m_drawingTable->setColumnCount(6);
        m_drawingTable->setHorizontalHeaderLabels(
            {QStringLiteral("图纸号"), QStringLiteral("更新时间"),
             QStringLiteral("当前"), QStringLiteral("预览"), QStringLiteral("导出"),
             QStringLiteral("删除")});
        m_drawingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int c = 1; c < 6; ++c) {
            m_drawingTable->horizontalHeader()->setSectionResizeMode(
                c, QHeaderView::ResizeToContents);
        }
        m_drawingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_drawingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_drawingTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_drawingTable->verticalHeader()->setVisible(false);
        m_drawingTable->setAlternatingRowColors(true);
        layout->addWidget(m_drawingTable, 1);

        // 操作行：设为当前版本 + 导入新图纸
        auto *buttonRow = new QHBoxLayout;
        m_setCurrentButton = new QPushButton(QStringLiteral("设为当前版本"), this);
        m_setCurrentButton->setEnabled(false);
        m_importButton = new QPushButton(this);
        buttonRow->addStretch();
        buttonRow->addWidget(m_setCurrentButton);
        buttonRow->addWidget(m_importButton);
        layout->addLayout(buttonRow);
        connect(m_importButton, &QPushButton::clicked, this,
                &RemotePartEditorDialog::onImportDrawing);
        connect(m_setCurrentButton, &QPushButton::clicked, this,
                &RemotePartEditorDialog::onSetCurrent);
        connect(m_drawingTable, &QTableWidget::itemSelectionChanged, this,
                &RemotePartEditorDialog::onSelectionChanged);

        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this,
                &RemotePartEditorDialog::onAccept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    void refreshDrawingList()
    {
        m_drawingTable->clearContents();
        m_drawings.clear();
        QString err;
        QVector<DirectoryItem> items;
        // listDir 对零件节点返回该零件的图纸行（服务端装配器：零件目录展示图纸）
        if (!m_client->listDir(m_node.id, items, &err)) {
            QMessageBox::warning(this, QStringLiteral("加载图纸失败"), err);
            m_drawingTable->setRowCount(0);
            m_importButton->setText(QStringLiteral("导入新图纸..."));
            return;
        }
        QString full;
        m_client->computeFullPartNo(m_node.id, full, nullptr);

        m_drawingTable->setRowCount(items.size());
        for (int row = 0; row < items.size(); ++row) {
            const Drawing &drawing = items.at(row).drawing;
            m_drawings.append(drawing);
            const QString drawingNo = full + drawing.version;

            auto *noItem = new QTableWidgetItem(drawingNo);
            noItem->setToolTip(drawing.fileName);
            m_drawingTable->setItem(row, 0, noItem);

            auto *timeItem = new QTableWidgetItem(
                drawing.createTime.toString(QStringLiteral("yyyy年M月d日H:mm")));
            m_drawingTable->setItem(row, 1, timeItem);

            auto *curItem = new QTableWidgetItem(
                drawing.isCurrent ? QStringLiteral("✓") : QString());
            m_drawingTable->setItem(row, 2, curItem);

            auto *previewButton = new QPushButton(QStringLiteral("预览"), m_drawingTable);
            connect(previewButton, &QPushButton::clicked, this,
                    [this, drawing] { onPreview(drawing); });
            m_drawingTable->setCellWidget(row, 3, previewButton);

            auto *exportButton = new QPushButton(QStringLiteral("导出"), m_drawingTable);
            connect(exportButton, &QPushButton::clicked, this,
                    [this, drawing] { onExport(drawing); });
            m_drawingTable->setCellWidget(row, 4, exportButton);

            auto *deleteButton = new QPushButton(QStringLiteral("删除"), m_drawingTable);
            connect(deleteButton, &QPushButton::clicked, this,
                    [this, drawing] { onDeleteDrawing(drawing); });
            m_drawingTable->setCellWidget(row, 5, deleteButton);
        }
        // 无图纸显示"导入新图纸"，有图纸显示"更新图纸"
        m_importButton->setText(items.isEmpty() ? QStringLiteral("导入新图纸...")
                                                : QStringLiteral("更新图纸..."));
        onSelectionChanged();
    }

    void onSelectionChanged()
    {
        bool hasSelection = false;
        const QList<QTableWidgetItem *> sel = m_drawingTable->selectedItems();
        for (const QTableWidgetItem *item : sel) {
            if (item->column() == 0) {
                hasSelection = true;
                break;
            }
        }
        m_setCurrentButton->setEnabled(hasSelection);
    }

    // 拉取图纸文件到临时目录并速览（非模态，可同时操作编辑窗口）
    void onPreview(const Drawing &drawing)
    {
        QString tempPath;
        QString err;
        if (!m_client->fetchDrawingFile(drawing, tempPath, &err)) {
            QMessageBox::warning(this, QStringLiteral("打开图纸失败"), err);
            return;
        }
        auto *preview = new PdfPreviewDialog(tempPath, this);
        preview->setAttribute(Qt::WA_DeleteOnClose);
        preview->show();
    }

    // 拉取图纸文件到临时目录后复制到用户选择的位置
    void onExport(const Drawing &drawing)
    {
        QString tempPath;
        QString err;
        if (!m_client->fetchDrawingFile(drawing, tempPath, &err)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"), err);
            return;
        }
        const QString target = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出图纸"), drawing.fileName,
            QStringLiteral("PDF 文件 (*.pdf)"));
        if (target.isEmpty()) {
            return;
        }
        if (!QFile::copy(tempPath, target)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"),
                                 QStringLiteral("无法复制图纸文件到所选位置"));
        }
    }

    void onImportDrawing()
    {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择图纸 PDF"), QString(), QStringLiteral("PDF 文件 (*.pdf)"));
        if (path.isEmpty()) {
            return;
        }
        QString err;
        if (!m_client->importPdf(m_node.id, path, &err)) {
            QMessageBox::warning(this, QStringLiteral("导入失败"), err);
            return;
        }
        m_changed = true;
        refreshDrawingList();
    }

    void onSetCurrent()
    {
        const int row = m_drawingTable->currentRow();
        if (row < 0 || row >= m_drawings.size()) {
            return;
        }
        QString err;
        if (!m_client->setCurrentDrawing(m_node.id, m_drawings.at(row).id, &err)) {
            QMessageBox::warning(this, QStringLiteral("操作失败"), err);
            return;
        }
        m_changed = true;
        refreshDrawingList();
    }

    void onDeleteDrawing(const Drawing &drawing)
    {
        const auto answer = QMessageBox::warning(
            this, QStringLiteral("确认删除"),
            QStringLiteral("确定删除图纸「%1」吗？文件将一并删除，此操作不可恢复。")
                .arg(drawing.fileName),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
        QString err;
        if (!m_client->deleteDrawing(drawing.id, &err)) {
            QMessageBox::warning(this, QStringLiteral("删除失败"), err);
            return;
        }
        m_changed = true;
        refreshDrawingList();
    }

    void onAccept()
    {
        const QString newName = m_nameEdit->text().trimmed();
        if (newName.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("输入无效"),
                                 QStringLiteral("零件名称不能为空"));
            return;
        }
        const QString newPartNo = m_partNoEdit->text().trimmed();
        if (newPartNo.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("输入无效"),
                                 QStringLiteral("图号本段不能为空"));
            return;
        }

        QString err;
        if (newName != m_node.name) {
            if (!m_client->renameNode(m_node.id, newName, &err)) {
                QMessageBox::warning(this, QStringLiteral("保存失败"), err);
                return;
            }
            m_changed = true;
        }
        if (newPartNo != m_node.partNo) {
            if (!m_client->updatePartNo(m_node.id, newPartNo, &err)) {
                QMessageBox::warning(this, QStringLiteral("保存失败"), err);
                return;
            }
            m_changed = true;
        }
        if (m_materialEdit->text().trimmed() != m_part.material
            || m_quantitySpin->value() != m_part.quantity) {
            if (!m_client->updatePartAttributes(m_node.id, m_materialEdit->text().trimmed(),
                                                 m_quantitySpin->value(), &err)) {
                QMessageBox::warning(this, QStringLiteral("保存失败"), err);
                return;
            }
            m_changed = true;
        }
        accept();
    }

    RemoteClient *m_client;
    HFADMNode m_node;
    Part m_part;
    QString m_partNoPrefix;
    QString m_error;
    bool m_changed = false;

    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_partNoEdit = nullptr;
    QLabel *m_fullPartNoPreview = nullptr;
    QLineEdit *m_materialEdit = nullptr;
    QSpinBox *m_quantitySpin = nullptr;
    QTableWidget *m_drawingTable = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_setCurrentButton = nullptr;
    QVector<Drawing> m_drawings; // 与表格行一一对应
};

} // namespace

bool showRemotePartEditorDialog(QWidget *parent, RemoteClient *client,
                                const HFADMNode &node, QString *errorMessage)
{
    if (!client) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("远程连接不可用");
        }
        return false;
    }
    RemotePartEditorDialog dialog(parent, client, node);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    return dialog.changed();
}
