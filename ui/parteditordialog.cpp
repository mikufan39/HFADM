#include <QCoreApplication>
#include "parteditordialog.h"
#include "pdfpreviewdialog.h"
#include "reversewheelspinbox.h"
#include "service/nodeservice.h"
#include "service/drawingservice.h"
#include "model/part.h"

#include <QAbstractItemView>
#include <QCompleter>
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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

class PartEditorDialog : public QDialog
{
public:
    PartEditorDialog(QWidget *parent, NodeService *nodeService,
                     DrawingService *drawingService, qint64 partNodeId)
        : QDialog(parent)
        , m_nodeService(nodeService)
        , m_drawingService(drawingService)
        , m_partNodeId(partNodeId)
    {
        setWindowTitle(QCoreApplication::translate("PartEditorDialog", "零件编辑"));
        setMinimumWidth(520);

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
        if (!m_nodeService->getNode(m_partNodeId, m_node)) {
            m_error = m_nodeService->lastError();
            return false;
        }
        Part part;
        if (m_nodeService->loadPart(m_partNodeId, part)) {
            m_part = part;
        }

        // 图号前缀 = 完整图号去掉本段部分
        const QString full = m_nodeService->computeFullPartNo(m_partNodeId);
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

        // 图号：只读前缀（机型.部件.）+ 本段输入（与新建零件一致）
        auto *prefixLabel = new QLabel(m_partNoPrefix, this);
        prefixLabel->setStyleSheet(QStringLiteral("color: palette(placeholder-text);")); // 只读样式
        auto *partNoRow = new QWidget(this);
        auto *partNoLayout = new QHBoxLayout(partNoRow);
        partNoLayout->setContentsMargins(0, 0, 0, 0);
        partNoLayout->setSpacing(2);
        partNoLayout->addWidget(prefixLabel);
        partNoLayout->addWidget(m_partNoEdit, 1);

        // 材质（自动补全）+ 数量（滚轮向下增加/向上减少）同行
        m_materialEdit = new QLineEdit(m_part.material, this);
        const QStringList materialList = m_nodeService->fetchMaterialList();
        if (!materialList.isEmpty()) {
            auto *completer = new QCompleter(materialList, this);
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            completer->setFilterMode(Qt::MatchContains); // 模糊包含，像搜索引擎提示
            completer->setCompletionMode(QCompleter::PopupCompletion);
            m_materialEdit->setCompleter(completer);
        }
        m_quantitySpin = new ReverseWheelSpinBox(this);
        m_quantitySpin->setRange(1, 999999);
        m_quantitySpin->setValue(m_part.quantity > 0 ? m_part.quantity : 1);
        auto *attrsRow = new QWidget(this);
        auto *attrsLayout = new QHBoxLayout(attrsRow);
        attrsLayout->setContentsMargins(0, 0, 0, 0);
        attrsLayout->addWidget(new QLabel(QCoreApplication::translate("PartEditorDialog", "材质："), this));
        attrsLayout->addWidget(m_materialEdit, 1);
        attrsLayout->addSpacing(16);
        attrsLayout->addWidget(new QLabel(QCoreApplication::translate("PartEditorDialog", "数量："), this));
        attrsLayout->addWidget(m_quantitySpin);

        m_remarkEdit = new QPlainTextEdit(m_node.remark, this);
        m_remarkEdit->setFixedHeight(70);
        m_remarkEdit->setPlaceholderText(QCoreApplication::translate("PartEditorDialog", "（可选）"));

        form->addRow(QCoreApplication::translate("PartEditorDialog", "零件名称："), m_nameEdit);
        form->addRow(QCoreApplication::translate("PartEditorDialog", "图号："), partNoRow);
        form->addRow(nullptr, attrsRow); // 材质/数量同行（无行标签）
        form->addRow(QCoreApplication::translate("PartEditorDialog", "备注："), m_remarkEdit);
        layout->addLayout(form);

        // 图纸区域
        auto *drawingLabel = new QLabel(QCoreApplication::translate("PartEditorDialog", "图纸"), this);
        drawingLabel->setStyleSheet(QStringLiteral("font-weight: 500;"));
        layout->addWidget(drawingLabel);

        m_drawingTable = new QTableWidget(this);
        m_drawingTable->setColumnCount(5);
        m_drawingTable->setHorizontalHeaderLabels(
            {QCoreApplication::translate("PartEditorDialog", "图纸号"), QCoreApplication::translate("PartEditorDialog", "更新时间"),
             QCoreApplication::translate("PartEditorDialog", "预览"), QCoreApplication::translate("PartEditorDialog", "导出"), QCoreApplication::translate("PartEditorDialog", "删除")});
        m_drawingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_drawingTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_drawingTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_drawingTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        m_drawingTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        m_drawingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_drawingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_drawingTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_drawingTable->verticalHeader()->setVisible(false);
        m_drawingTable->setAlternatingRowColors(true);
        layout->addWidget(m_drawingTable, 1);

        m_importButton = new QPushButton(this);
        auto *buttonRow = new QHBoxLayout;
        buttonRow->addStretch();
        buttonRow->addWidget(m_importButton);
        layout->addLayout(buttonRow);
        connect(m_importButton, &QPushButton::clicked, this,
                &PartEditorDialog::onImportDrawing);

        // 右下角：确定 / 取消（支持多语言）
        auto *buttons = new QDialogButtonBox(this);
        buttons->addButton(QCoreApplication::translate("PartEditorDialog", "确定"), QDialogButtonBox::AcceptRole);
        buttons->addButton(QCoreApplication::translate("PartEditorDialog", "取消"), QDialogButtonBox::RejectRole);
        connect(buttons, &QDialogButtonBox::accepted, this, &PartEditorDialog::onAccept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    void refreshDrawingList()
    {
        m_drawingTable->clearContents();
        QVector<Drawing> drawings;
        if (m_drawingService && m_drawingService->queryDrawings(m_partNodeId, drawings)) {
            // queryDrawings 已按创建时间倒序：最新图纸显示在最前面
            const QString fullPartNo = m_nodeService->computeFullPartNo(m_partNodeId);
            m_drawingTable->setRowCount(drawings.size());
            for (int row = 0; row < drawings.size(); ++row) {
                const Drawing &drawing = drawings.at(row);
                const QString drawingNo = fullPartNo + drawing.version;
                const QString fullPath = DrawingService::resolveDrawingPath(
                    m_drawingService->projectPath(), drawing.filePath);

                auto *noItem = new QTableWidgetItem(drawingNo);
                noItem->setToolTip(drawing.fileName);
                m_drawingTable->setItem(row, 0, noItem);

                auto *timeItem = new QTableWidgetItem(
                    drawing.createTime.toString(QCoreApplication::translate("PartEditorDialog", "yyyy年M月d日H:mm")));
                m_drawingTable->setItem(row, 1, timeItem);

                auto *previewButton = new QPushButton(QCoreApplication::translate("PartEditorDialog", "预览"), m_drawingTable);
                connect(previewButton, &QPushButton::clicked, this,
                        [this, fullPath] {
                            // 非模态弹出：可同时操作编辑窗口，关闭时自动销毁
                            auto *preview = new PdfPreviewDialog(fullPath, this);
                            preview->setAttribute(Qt::WA_DeleteOnClose);
                            preview->show();
                        });
                m_drawingTable->setCellWidget(row, 2, previewButton);

                auto *exportButton = new QPushButton(QCoreApplication::translate("PartEditorDialog", "导出"), m_drawingTable);
                connect(exportButton, &QPushButton::clicked, this,
                        [this, fullPath, drawing] {
                            const QString target = QFileDialog::getSaveFileName(
                                this, QCoreApplication::translate("PartEditorDialog", "导出图纸"), drawing.fileName,
                                QCoreApplication::translate("PartEditorDialog", "PDF 文件 (*.pdf)"));
                            if (target.isEmpty()) {
                                return;
                            }
                            if (!QFile::copy(fullPath, target)) {
                                QMessageBox::warning(
                                    this, QCoreApplication::translate("PartEditorDialog", "导出失败"),
                                    QCoreApplication::translate("PartEditorDialog", "无法复制图纸文件到所选位置"));
                            }
                        });
                m_drawingTable->setCellWidget(row, 3, exportButton);

                auto *deleteButton = new QPushButton(QCoreApplication::translate("PartEditorDialog", "删除"), m_drawingTable);
                connect(deleteButton, &QPushButton::clicked, this,
                        [this, drawing] { onDeleteDrawing(drawing); });
                m_drawingTable->setCellWidget(row, 4, deleteButton);
            }
        } else {
            m_drawingTable->setRowCount(0);
        }
        // 无图纸显示"导入新图纸"，有图纸显示"更新图纸"
        m_importButton->setText(drawings.isEmpty() ? QCoreApplication::translate("PartEditorDialog", "导入新图纸...")
                                                   : QCoreApplication::translate("PartEditorDialog", "更新图纸..."));
    }

    void onImportDrawing()
    {
        const QString path = QFileDialog::getOpenFileName(
            this, QCoreApplication::translate("PartEditorDialog", "选择图纸 PDF"), QString(), QCoreApplication::translate("PartEditorDialog", "PDF 文件 (*.pdf)"));
        if (path.isEmpty()) {
            return;
        }
        if (!m_drawingService || !m_drawingService->importPdf(m_partNodeId, path)) {
            QMessageBox::warning(this, QCoreApplication::translate("PartEditorDialog", "导入失败"),
                                 m_drawingService ? m_drawingService->lastError()
                                                  : QCoreApplication::translate("PartEditorDialog", "图纸服务不可用"));
            return;
        }
        m_changed = true;
        refreshDrawingList();
    }

    void onDeleteDrawing(const Drawing &drawing)
    {
        // 三选项确认框：仅删除 / 删除并更新图号 / 取消
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(QCoreApplication::translate("PartEditorDialog", "删除图纸"));
        box.setText(QCoreApplication::translate("PartEditorDialog", "确定要删除图纸「%1%2」吗？删除后无法恢复。")
                        .arg(m_nodeService->computeFullPartNo(m_partNodeId), drawing.version));
        box.setInformativeText(QStringLiteral(
            "<b>仅删除</b>：删除这张图纸，其他版本保持不变。"
            "例如 A、B、C 三个版本删除 B 后，剩下 A、C，B 可以空缺。<br/>"
            "<b>删除并更新图号</b>：删除后，后续版本自动前移补位。"
            "例如 A、B、C 三个版本删除 B 后，C 会自动变为 B。"));
        auto *keepButton = box.addButton(QCoreApplication::translate("PartEditorDialog", "仅删除"), QMessageBox::AcceptRole);
        auto *renumberButton = box.addButton(QCoreApplication::translate("PartEditorDialog", "删除并更新图号"), QMessageBox::AcceptRole);
        box.addButton(QCoreApplication::translate("PartEditorDialog", "取消"), QMessageBox::RejectRole);
        box.exec();

        DrawingService::DrawingRemovalMode mode;
        if (box.clickedButton() == keepButton) {
            mode = DrawingService::DrawingRemovalMode::KeepVersions;
        } else if (box.clickedButton() == renumberButton) {
            mode = DrawingService::DrawingRemovalMode::Renumber;
        } else {
            return; // 取消（含右上角关闭 / Esc）
        }

        if (!m_drawingService || !m_drawingService->removeDrawing(drawing.id, mode)) {
            QMessageBox::warning(this, QCoreApplication::translate("PartEditorDialog", "删除失败"),
                                 m_drawingService ? m_drawingService->lastError()
                                                  : QCoreApplication::translate("PartEditorDialog", "图纸服务不可用"));
            return;
        }
        m_changed = true;
        refreshDrawingList();
    }

    void onAccept()
    {
        const QString newName = m_nameEdit->text().trimmed();
        if (newName.isEmpty()) {
            QMessageBox::warning(this, QCoreApplication::translate("PartEditorDialog", "输入无效"), QCoreApplication::translate("PartEditorDialog", "零件名称不能为空"));
            return;
        }
        const QString newPartNo = m_partNoEdit->text().trimmed();
        if (newPartNo.isEmpty()) {
            QMessageBox::warning(this, QCoreApplication::translate("PartEditorDialog", "输入无效"), QCoreApplication::translate("PartEditorDialog", "图号本段不能为空"));
            return;
        }

        if (newName != m_node.name) {
            if (!m_nodeService->renameNode(m_partNodeId, newName)) {
                QMessageBox::warning(this, QCoreApplication::translate("PartEditorDialog", "保存失败"), m_nodeService->lastError());
                return;
            }
            m_changed = true;
        }
        if (newPartNo != m_node.partNo) {
            if (!m_nodeService->updateNodePartNo(m_partNodeId, newPartNo)) {
                QMessageBox::warning(this, QCoreApplication::translate("PartEditorDialog", "保存失败"), m_nodeService->lastError());
                return;
            }
            m_changed = true;
        }
        if (m_materialEdit->text().trimmed() != m_part.material
            || m_quantitySpin->value() != m_part.quantity) {
            if (!m_nodeService->updatePartAttributes(m_partNodeId,
                                                     m_materialEdit->text().trimmed(),
                                                     m_quantitySpin->value())) {
                QMessageBox::warning(this, QCoreApplication::translate("PartEditorDialog", "保存失败"),
                                     m_nodeService->lastError());
                return;
            }
            m_changed = true;
        }
        if (m_remarkEdit->toPlainText().trimmed() != m_node.remark) {
            if (!m_nodeService->updateNodeRemark(m_partNodeId,
                                                 m_remarkEdit->toPlainText().trimmed())) {
                QMessageBox::warning(this, QCoreApplication::translate("PartEditorDialog", "保存失败"),
                                     m_nodeService->lastError());
                return;
            }
            m_changed = true;
        }
        accept();
    }

    NodeService *m_nodeService;
    DrawingService *m_drawingService;
    qint64 m_partNodeId;
    HFADMNode m_node;
    Part m_part;
    QString m_partNoPrefix;
    QString m_error;
    bool m_changed = false;

    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_partNoEdit = nullptr;
    QLineEdit *m_materialEdit = nullptr;
    QSpinBox *m_quantitySpin = nullptr;
    QPlainTextEdit *m_remarkEdit = nullptr;
    QTableWidget *m_drawingTable = nullptr;
    QPushButton *m_importButton = nullptr;
};

} // namespace

bool showPartEditorDialog(QWidget *parent, NodeService *nodeService,
                          DrawingService *drawingService, qint64 partNodeId,
                          QString *errorMessage)
{
    PartEditorDialog dialog(parent, nodeService, drawingService, partNodeId);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    return dialog.changed();
}
