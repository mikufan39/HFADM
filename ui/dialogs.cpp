#include "dialogs.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
// 记住「打开项目」对话框上次访问的目录（QSettings 全局存储）
const QString kLastProjectDirKey = QStringLiteral("lastProjectDir");

// 创建备注输入控件：多行文本框，固定高度避免 QFormLayout 被撑高
QPlainTextEdit *createRemarkEdit(QWidget *parent, const QString &text = QString())
{
    auto *edit = new QPlainTextEdit(parent);
    edit->setPlainText(text);
    edit->setFixedHeight(70);
    edit->setPlaceholderText(QStringLiteral("（可选）填写备注，超长时列表仅显示前部"));
    return edit;
}
}

bool showNewProjectDialog(QWidget *parent, QString &parentDir, QString &projectName)
{
    parentDir = QFileDialog::getExistingDirectory(
        parent, QStringLiteral("选择项目保存目录"), QString());
    if (parentDir.isEmpty()) {
        return false;
    }

    bool ok = false;
    projectName = QInputDialog::getText(
        parent, QStringLiteral("新建项目"), QStringLiteral("机型名称："),
        QLineEdit::Normal, QString(), &ok);
    return ok && !projectName.trimmed().isEmpty();
}

bool showNewComponentDialog(QWidget *parent, QString &name, QString &partNo,
                            const QString &fullPartNoPrefix, int &quantity, QString &remark)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("新建部件"));
    auto *form = new QFormLayout(&dialog);

    auto *nameEdit = new QLineEdit(&dialog);
    auto *partNoEdit = new QLineEdit(&dialog);
    partNoEdit->setPlaceholderText(QStringLiteral("0-9999"));
    auto *prefixLabel = new QLabel(fullPartNoPrefix, &dialog);
    auto *previewLabel = new QLabel(&dialog);
    // 用 textChanged 实时刷新完整图号
    QObject::connect(partNoEdit, &QLineEdit::textChanged, &dialog,
                     [previewLabel, fullPartNoPrefix](const QString &text) {
                         previewLabel->setText(QStringLiteral("%1<b>%2</b>")
                                                   .arg(fullPartNoPrefix, text.trimmed()));
                     });
    auto *quantitySpin = new QSpinBox(&dialog);
    quantitySpin->setRange(1, 999999);
    quantitySpin->setValue(quantity > 0 ? quantity : 1);
    auto *remarkEdit = createRemarkEdit(&dialog);

    form->addRow(QStringLiteral("部件名称："), nameEdit);
    form->addRow(QStringLiteral("图号前缀："), prefixLabel);
    form->addRow(QStringLiteral("图号本段："), partNoEdit);
    form->addRow(QStringLiteral("完整图号："), previewLabel);
    form->addRow(QStringLiteral("数量："), quantitySpin);
    form->addRow(QStringLiteral("备注："), remarkEdit);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (nameEdit->text().trimmed().isEmpty() || partNoEdit->text().trimmed().isEmpty()) {
        return false;
    }

    name = nameEdit->text().trimmed();
    partNo = partNoEdit->text().trimmed();
    quantity = quantitySpin->value();
    remark = remarkEdit->toPlainText().trimmed();
    return true;
}

bool showOpenProjectDialog(QWidget *parent, QString &projectPath)
{
    // 默认定位到上一次选择的目录（不存在时退回系统默认位置）
    QSettings settings;
    const QString startDir = settings.value(kLastProjectDirKey).toString();
    const QString startDirValid = (!startDir.isEmpty() && QDir(startDir).exists())
        ? startDir : QString();

    projectPath = QFileDialog::getExistingDirectory(
        parent, QStringLiteral("打开项目"), startDirValid);
    if (projectPath.isEmpty()) {
        return false;
    }

    // 记住本次选择的目录，供下次打开使用
    settings.setValue(kLastProjectDirKey, projectPath);
    return true;
}

bool showBackupTargetDialog(QWidget *parent, QString &targetDir)
{
    targetDir = QFileDialog::getExistingDirectory(
        parent, QStringLiteral("选择备份保存目录"), QString());
    return !targetDir.isEmpty();
}

bool showNewPartDialog(QWidget *parent, QString &name, QString &partNo,
                       const QString &fullPartNoPrefix, QString &material, int &quantity,
                       QString &pdfFilePath, QString &remark)
{
    pdfFilePath.clear();
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("新建零件"));
    auto *form = new QFormLayout(&dialog);

    auto *nameEdit = new QLineEdit(&dialog);
    auto *partNoEdit = new QLineEdit(&dialog);
    partNoEdit->setPlaceholderText(QStringLiteral("如 06"));
    auto *prefixLabel = new QLabel(fullPartNoPrefix, &dialog);
    auto *previewLabel = new QLabel(&dialog);
    QObject::connect(partNoEdit, &QLineEdit::textChanged, &dialog,
                     [previewLabel, fullPartNoPrefix](const QString &text) {
                         previewLabel->setText(QStringLiteral("%1<b>%2</b>")
                                                   .arg(fullPartNoPrefix, text.trimmed()));
                     });

    auto *materialEdit = new QLineEdit(&dialog);
    auto *quantitySpin = new QSpinBox(&dialog);
    quantitySpin->setRange(1, 999999);
    quantitySpin->setValue(1);

    // 图纸文件（可选）：选择 PDF 作为零件图纸，导入时自动命名
    auto *pdfEdit = new QLineEdit(&dialog);
    pdfEdit->setReadOnly(true);
    pdfEdit->setPlaceholderText(QStringLiteral("（可选）选择 PDF 图纸，导入时自动命名"));
    auto *pdfButton = new QPushButton(QStringLiteral("浏览..."), &dialog);
    auto *pdfRow = new QWidget(&dialog);
    auto *pdfLayout = new QHBoxLayout(pdfRow);
    pdfLayout->setContentsMargins(0, 0, 0, 0);
    pdfLayout->addWidget(pdfEdit, 1);
    pdfLayout->addWidget(pdfButton);
    QObject::connect(pdfButton, &QPushButton::clicked, &dialog, [parent, pdfEdit] {
        const QString path = QFileDialog::getOpenFileName(
            parent, QStringLiteral("选择图纸 PDF"), QString(), QStringLiteral("PDF 文件 (*.pdf)"));
        if (!path.isEmpty()) {
            pdfEdit->setText(path);
        }
    });

    auto *remarkEdit = createRemarkEdit(&dialog);

    form->addRow(QStringLiteral("零件名称："), nameEdit);
    form->addRow(QStringLiteral("图号前缀："), prefixLabel);
    form->addRow(QStringLiteral("图号本段："), partNoEdit);
    form->addRow(QStringLiteral("完整图号："), previewLabel);
    form->addRow(QStringLiteral("材质："), materialEdit);
    form->addRow(QStringLiteral("数量："), quantitySpin);
    form->addRow(QStringLiteral("图纸文件："), pdfRow);
    form->addRow(QStringLiteral("备注："), remarkEdit);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (nameEdit->text().trimmed().isEmpty() || partNoEdit->text().trimmed().isEmpty()) {
        return false;
    }

    name = nameEdit->text().trimmed();
    partNo = partNoEdit->text().trimmed();
    material = materialEdit->text().trimmed();
    quantity = quantitySpin->value();
    pdfFilePath = pdfEdit->text().trimmed();
    remark = remarkEdit->toPlainText().trimmed();
    return true;
}

bool showNodePropertiesDialog(QWidget *parent,
                              const QString &nodeName,
                              const QString &typeName,
                              const QString &createTimeText,
                              bool hasPartNo,
                              const QString &fullPartNoPrefix,
                              const QString &currentPartNo,
                              bool isPart,
                              const QString &partMaterial,
                              int partQuantity,
                              bool isComponent,
                              int componentQuantity,
                              const QString &currentRemark,
                              QString &newName,
                              QString &newPartNo,
                              QString &newMaterial,
                              int &newQuantity,
                              int &newComponentQuantity,
                              QString &newRemark)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("属性 - %1").arg(nodeName));
    auto *form = new QFormLayout(&dialog);

    auto *nameEdit = new QLineEdit(nodeName, &dialog);
    form->addRow(QStringLiteral("名称："), nameEdit);
    form->addRow(QStringLiteral("类型："), new QLabel(typeName, &dialog));
    form->addRow(QStringLiteral("创建时间："), new QLabel(createTimeText, &dialog));

    QLineEdit *partNoEdit = nullptr;
    if (hasPartNo) {
        auto *prefixLabel = new QLabel(fullPartNoPrefix, &dialog);
        auto *previewLabel = new QLabel(&dialog);
        partNoEdit = new QLineEdit(currentPartNo, &dialog);
        QObject::connect(partNoEdit, &QLineEdit::textChanged, &dialog,
                         [previewLabel, fullPartNoPrefix](const QString &text) {
                             previewLabel->setText(QStringLiteral("%1<b>%2</b>")
                                                       .arg(fullPartNoPrefix, text.trimmed()));
                         });
        previewLabel->setText(QStringLiteral("%1<b>%2</b>")
                                  .arg(fullPartNoPrefix, currentPartNo));
        form->addRow(QStringLiteral("图号前缀："), prefixLabel);
        form->addRow(QStringLiteral("图号本段："), partNoEdit);
        form->addRow(QStringLiteral("完整图号："), previewLabel);
    }

    QLineEdit *materialEdit = nullptr;
    QSpinBox *quantitySpin = nullptr;
    if (isPart) {
        materialEdit = new QLineEdit(partMaterial, &dialog);
        quantitySpin = new QSpinBox(&dialog);
        quantitySpin->setRange(1, 999999);
        quantitySpin->setValue(partQuantity);
        form->addRow(QStringLiteral("材质："), materialEdit);
        form->addRow(QStringLiteral("数量："), quantitySpin);
    }

    QSpinBox *componentQuantitySpin = nullptr;
    if (isComponent) {
        componentQuantitySpin = new QSpinBox(&dialog);
        componentQuantitySpin->setRange(1, 999999);
        componentQuantitySpin->setValue(componentQuantity);
        form->addRow(QStringLiteral("数量："), componentQuantitySpin);
    }

    // 备注（部件/零件可编辑；机型不显示）
    QPlainTextEdit *remarkEdit = nullptr;
    if (isPart || isComponent) {
        remarkEdit = createRemarkEdit(&dialog, currentRemark);
        form->addRow(QStringLiteral("备注："), remarkEdit);
    }

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (nameEdit->text().trimmed().isEmpty()) {
        return false;
    }
    if (hasPartNo && partNoEdit->text().trimmed().isEmpty()) {
        return false;
    }

    newName = nameEdit->text().trimmed();
    newPartNo = hasPartNo ? partNoEdit->text().trimmed() : QString();
    newMaterial = materialEdit ? materialEdit->text().trimmed() : QString();
    newQuantity = quantitySpin ? quantitySpin->value() : partQuantity;
    newComponentQuantity = componentQuantitySpin ? componentQuantitySpin->value()
                                                 : componentQuantity;
    newRemark = remarkEdit ? remarkEdit->toPlainText().trimmed() : currentRemark;
    return true;
}

void showAboutDialog(QWidget *parent)
{
    QMessageBox::about(
        parent, QStringLiteral("关于艾锐奥智能图纸管理系统"),
        QStringLiteral("艾锐奥智能图纸管理系统\n\n"
                       "Power by QT，Designed by Mikufan\n"
                       "版本：A（内部测试版本）"));
}

bool resolveCopyPartNoConflictDialog(QWidget *parent, QVector<CopyConflictItem> &items)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("复制图号冲突"));
    dialog.setMinimumWidth(560);
    auto *layout = new QVBoxLayout(&dialog);

    auto *hint = new QLabel(
        QStringLiteral("目标位置存在图号冲突，请修改下图号（前缀已按目标目录自动生成）。"), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *table = new QTableWidget(static_cast<int>(items.size()), 3, &dialog);
    table->setHorizontalHeaderLabels(
        {QStringLiteral("名称"), QStringLiteral("原图号"), QStringLiteral("新图号")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setAlternatingRowColors(true);

    for (int row = 0; row < items.size(); ++row) {
        CopyConflictItem &item = items[row];
        table->setItem(row, 0, new QTableWidgetItem(item.name));
        table->setItem(row, 1, new QTableWidgetItem(item.originalFullPartNo));

        // 新图号：自动前缀（灰）+ 可编辑图号本段
        auto *cell = new QWidget(table);
        auto *cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(4, 2, 4, 2);
        cellLayout->setSpacing(0);
        auto *prefixLabel = new QLabel(item.prefix, cell);
        prefixLabel->setStyleSheet(QStringLiteral("color: #888888;"));
        auto *edit = new QLineEdit(item.newPartNo, cell);
        edit->setPlaceholderText(QStringLiteral("图号本段"));
        cellLayout->addWidget(prefixLabel);
        cellLayout->addWidget(edit, 1);
        table->setCellWidget(row, 2, cell);

        // 输入实时回写 items：循环弹窗时保留用户上次填写的内容
        QObject::connect(edit, &QLineEdit::textChanged, &dialog, [&item](const QString &text) {
            item.newPartNo = text.trimmed();
        });
    }
    layout->addWidget(table, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    return dialog.exec() == QDialog::Accepted;
}
