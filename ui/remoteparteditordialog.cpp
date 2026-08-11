#include "remoteparteditordialog.h"
#include "pdfpreviewdialog.h"
#include "model/drawing.h"
#include "model/hfdadnode.h"
#include "model/part.h"
#include "service/remoteclient.h"
#include "service/remoteprotocol.h"

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <functional>

namespace {

// 远程零件编辑对话框（数据全部经 RemoteClient 异步协议，awaitOnce 等信号）
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
        buildUi();

        // 受限（只读）授权：属性与图纸只读查看（预览/导出可用），禁止一切写操作
        m_readOnly = m_client
            && m_client->permission() == RemoteProtocol::Permission::ReadOnly;
        if (m_readOnly) {
            setWindowTitle(QStringLiteral("零件查看（远程·只读）"));
            m_nameEdit->setEnabled(false);
            m_partNoEdit->setEnabled(false);
            m_materialEdit->setEnabled(false);
            m_quantitySpin->setEnabled(false);
            m_remarkEdit->setEnabled(false);
            m_importButton->setEnabled(false);
            m_setCurrentButton->setEnabled(false);
        }

        QPointer<RemoteClient> guard(m_client);
        // 异步加载零件属性
        const qint64 loadId = m_client->loadPartAsync(m_node.id);
        awaitOnce(m_client, loadId, this, [this, guard](bool ok, const QJsonObject &data, const QString &err) {
            if (!guard) {
                return;
            }
            if (!ok) {
                QMessageBox::warning(this, QStringLiteral("加载失败"), err);
                reject();
                return;
            }
            m_part = RemoteProtocol::partFromJson(data.value(QStringLiteral("part")).toObject());
            m_materialEdit->setText(m_part.material);
            m_quantitySpin->setValue(m_part.quantity > 0 ? m_part.quantity : 1);
        });
        // 异步加载完整图号，完成后刷新图纸列表
        const qint64 fullId = m_client->computeFullPartNoAsync(m_node.id);
        awaitOnce(m_client, fullId, this, [this, guard](bool ok, const QJsonObject &data, const QString &err) {
            if (!guard) {
                return;
            }
            if (!ok) {
                QMessageBox::warning(this, QStringLiteral("加载失败"), err);
                return;
            }
            m_fullPartNo = data.value(QStringLiteral("full")).toString();
            const QString partNo = m_node.partNo;
            if (!m_fullPartNo.isEmpty() && !partNo.isEmpty() && m_fullPartNo.endsWith(partNo)) {
                m_partNoPrefix = m_fullPartNo.left(m_fullPartNo.size() - partNo.size());
            } else if (!m_fullPartNo.isEmpty()) {
                m_partNoPrefix = m_fullPartNo + QStringLiteral(".");
            }
            if (m_prefixLabel) {
                m_prefixLabel->setText(m_partNoPrefix);
            }
            m_fullPartNoPreview->setText(
                QStringLiteral("%1<b>%2</b>").arg(m_partNoPrefix, m_node.partNo));
            refreshDrawingList();
        });
    }

    bool changed() const { return m_changed; }

private:
    void buildUi()
    {
        auto *layout = new QVBoxLayout(this);

        auto *form = new QFormLayout;
        m_nameEdit = new QLineEdit(m_node.name, this);
        m_partNoEdit = new QLineEdit(m_node.partNo, this);
        m_prefixLabel = new QLabel(m_partNoPrefix, this);
        m_fullPartNoPreview = new QLabel(this);
        m_materialEdit = new QLineEdit(m_part.material, this);
        m_quantitySpin = new QSpinBox(this);
        m_quantitySpin->setRange(1, 999999);
        m_quantitySpin->setValue(m_part.quantity > 0 ? m_part.quantity : 1);
        m_remarkEdit = new QPlainTextEdit(m_node.remark, this);
        m_remarkEdit->setFixedHeight(70);

        connect(m_partNoEdit, &QLineEdit::textChanged, this,
                [this](const QString &text) {
                    m_fullPartNoPreview->setText(
                        QStringLiteral("%1<b>%2</b>").arg(m_partNoPrefix, text.trimmed()));
                });
        m_fullPartNoPreview->setText(
            QStringLiteral("%1<b>%2</b>").arg(m_partNoPrefix, m_node.partNo));

        form->addRow(QStringLiteral("零件名称："), m_nameEdit);
        form->addRow(QStringLiteral("图号前缀："), m_prefixLabel);
        form->addRow(QStringLiteral("图号本段："), m_partNoEdit);
        form->addRow(QStringLiteral("完整图号："), m_fullPartNoPreview);
        form->addRow(QStringLiteral("材质："), m_materialEdit);
        form->addRow(QStringLiteral("数量："), m_quantitySpin);
        form->addRow(QStringLiteral("备注："), m_remarkEdit);
        layout->addLayout(form);

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
        QPointer<RemoteClient> guard(m_client);
        const qint64 id = m_client->listDirAsync(m_node.id);
        awaitOnce(m_client, id, this, [this, guard](bool ok, const QJsonObject &data, const QString &err) {
            if (!guard) {
                return;
            }
            if (!ok) {
                QMessageBox::warning(this, QStringLiteral("加载图纸失败"), err);
                m_drawingTable->setRowCount(0);
                m_importButton->setText(QStringLiteral("导入新图纸..."));
                onSelectionChanged();
                return;
            }
            QVector<DirectoryItem> items;
            const QJsonArray arr = data.value(QStringLiteral("items")).toArray();
            for (const QJsonValue &v : arr) {
                items.append(RemoteProtocol::directoryItemFromJson(v.toObject()));
            }
            m_drawingTable->setRowCount(items.size());
            for (int row = 0; row < items.size(); ++row) {
                const Drawing &drawing = items.at(row).drawing;
                m_drawings.append(drawing);
                const QString drawingNo = m_fullPartNo + drawing.version;

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

                if (!m_readOnly) {
                    auto *deleteButton = new QPushButton(QStringLiteral("删除"), m_drawingTable);
                    connect(deleteButton, &QPushButton::clicked, this,
                            [this, drawing] { onDeleteDrawing(drawing); });
                    m_drawingTable->setCellWidget(row, 5, deleteButton);
                }
            }
            m_importButton->setText(items.isEmpty() ? QStringLiteral("导入新图纸...")
                                                    : QStringLiteral("更新图纸..."));
            onSelectionChanged();
        });
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
        m_setCurrentButton->setEnabled(hasSelection && !m_readOnly);
    }

    void onPreview(const Drawing &drawing)
    {
        QPointer<RemoteClient> guard(m_client);
        const qint64 id = m_client->fetchDrawingFileAsync(drawing.id);
        awaitOnce(m_client, id, this, [this, guard](bool ok, const QJsonObject &data, const QString &err) {
            if (!guard) {
                return;
            }
            if (!ok) {
                QMessageBox::warning(this, QStringLiteral("打开图纸失败"), err);
                return;
            }
            const QString tempPath = data.value(QStringLiteral("tempFilePath")).toString();
            if (tempPath.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("打开图纸失败"), QStringLiteral("图纸数据为空"));
                return;
            }
            auto *preview = new PdfPreviewDialog(tempPath, this);
            preview->setAttribute(Qt::WA_DeleteOnClose);
            preview->show();
        });
    }

    void onExport(const Drawing &drawing)
    {
        QPointer<RemoteClient> guard(m_client);
        const qint64 id = m_client->fetchDrawingFileAsync(drawing.id);
        awaitOnce(m_client, id, this, [this, guard, drawing](bool ok, const QJsonObject &data, const QString &err) {
            if (!guard) {
                return;
            }
            if (!ok) {
                QMessageBox::warning(this, QStringLiteral("导出失败"), err);
                return;
            }
            const QString tempPath = data.value(QStringLiteral("tempFilePath")).toString();
            if (tempPath.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("图纸数据为空"));
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
        });
    }

    void onImportDrawing()
    {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择图纸 PDF"), QString(), QStringLiteral("PDF 文件 (*.pdf)"));
        if (path.isEmpty()) {
            return;
        }
        QPointer<RemoteClient> guard(m_client);
        const qint64 id = m_client->importPdfAsync(m_node.id, path);
        awaitOnce(m_client, id, this, [this, guard](bool ok, const QJsonObject &, const QString &err) {
            if (!guard) {
                return;
            }
            if (!ok) {
                QMessageBox::warning(this, QStringLiteral("导入失败"), err);
                return;
            }
            m_changed = true;
            refreshDrawingList();
        });
    }

    void onSetCurrent()
    {
        const int row = m_drawingTable->currentRow();
        if (row < 0 || row >= m_drawings.size()) {
            return;
        }
        const qint64 drawingId = m_drawings.at(row).id;
        QPointer<RemoteClient> guard(m_client);
        const qint64 id = m_client->setCurrentDrawingAsync(m_node.id, drawingId);
        awaitOnce(m_client, id, this, [this, guard](bool ok, const QJsonObject &, const QString &err) {
            if (!guard) {
                return;
            }
            if (!ok) {
                QMessageBox::warning(this, QStringLiteral("操作失败"), err);
                return;
            }
            m_changed = true;
            refreshDrawingList();
        });
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
        QPointer<RemoteClient> guard(m_client);
        const qint64 id = m_client->deleteDrawingAsync(drawing.id);
        awaitOnce(m_client, id, this, [this, guard, drawing](bool ok, const QJsonObject &, const QString &err) {
            if (!guard) {
                return;
            }
            if (!ok) {
                QMessageBox::warning(this, QStringLiteral("删除失败"), err);
                return;
            }
            m_changed = true;
            refreshDrawingList();
        });
    }

    void onAccept()
    {
        if (m_readOnly) {
            accept(); // 只读查看：直接关闭，不做任何写操作
            return;
        }
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
        const QString newMaterial = m_materialEdit->text().trimmed();
        const int newQuantity = m_quantitySpin->value();

        // 串行保存：rename → updatePartNo → updatePartAttributes → updateNodeRemark → accept
        // 每步用 std::function 持所有权，awaitOnce 回调里调下一步
        QPointer<RemoteClient> guard(m_client);
        std::function<void()> doUpdateRemark = [this, guard]() {
            if (!guard) {
                return;
            }
            const QString newRemark = m_remarkEdit->toPlainText().trimmed();
            if (newRemark != m_node.remark) {
                const qint64 id = guard->updateNodeRemarkAsync(m_node.id, newRemark);
                awaitOnce(guard, id, this, [this, guard](bool ok, const QJsonObject &, const QString &err) {
                    if (!guard) {
                        return;
                    }
                    if (!ok) {
                        QMessageBox::warning(this, QStringLiteral("保存失败"), err);
                        return;
                    }
                    m_changed = true;
                    accept();
                });
            } else {
                accept();
            }
        };
        std::function<void()> doUpdateAttrs = [this, guard, newMaterial, newQuantity, doUpdateRemark]() {
            if (!guard) {
                return;
            }
            if (newMaterial != m_part.material || newQuantity != m_part.quantity) {
                const qint64 id = guard->updatePartAttributesAsync(m_node.id, newMaterial, newQuantity);
                awaitOnce(guard, id, this, [this, guard, doUpdateRemark](bool ok, const QJsonObject &, const QString &err) {
                    if (!guard) {
                        return;
                    }
                    if (!ok) {
                        QMessageBox::warning(this, QStringLiteral("保存失败"), err);
                        return;
                    }
                    m_changed = true;
                    doUpdateRemark();
                });
            } else {
                doUpdateRemark();
            }
        };
        std::function<void()> doUpdatePartNo = [this, guard, newPartNo, doUpdateAttrs]() {
            if (!guard) {
                return;
            }
            if (newPartNo != m_node.partNo) {
                const qint64 id = guard->updatePartNoAsync(m_node.id, newPartNo);
                awaitOnce(guard, id, this, [this, guard, doUpdateAttrs](bool ok, const QJsonObject &, const QString &err) {
                    if (!guard) {
                        return;
                    }
                    if (!ok) {
                        QMessageBox::warning(this, QStringLiteral("保存失败"), err);
                        return;
                    }
                    m_changed = true;
                    doUpdateAttrs();
                });
            } else {
                doUpdateAttrs();
            }
        };
        if (newName != m_node.name) {
            const qint64 id = guard->renameNodeAsync(m_node.id, newName);
            awaitOnce(guard, id, this, [this, guard, doUpdatePartNo](bool ok, const QJsonObject &, const QString &err) {
                if (!guard) {
                    return;
                }
                if (!ok) {
                    QMessageBox::warning(this, QStringLiteral("保存失败"), err);
                    return;
                }
                m_changed = true;
                doUpdatePartNo();
            });
        } else {
            doUpdatePartNo();
        }
    }

    RemoteClient *m_client;
    HFADMNode m_node;
    Part m_part;
    QString m_partNoPrefix;
    QString m_fullPartNo;
    bool m_changed = false;
    bool m_readOnly = false;

    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_partNoEdit = nullptr;
    QLabel *m_prefixLabel = nullptr;
    QLabel *m_fullPartNoPreview = nullptr;
    QLineEdit *m_materialEdit = nullptr;
    QSpinBox *m_quantitySpin = nullptr;
    QPlainTextEdit *m_remarkEdit = nullptr;
    QTableWidget *m_drawingTable = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_setCurrentButton = nullptr;
    QVector<Drawing> m_drawings;
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
