#include <QCoreApplication>
#include "dialogs.h"
#include "reversewheelspinbox.h"

#include <QCompleter>
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
    edit->setPlaceholderText(QCoreApplication::translate("Dialogs", "（可选）填写备注，超长时列表仅显示前部"));
    return edit;
}
}

bool showNewProjectDialog(QWidget *parent, QString &parentDir, QString &projectName)
{
    parentDir = QFileDialog::getExistingDirectory(
        parent, QCoreApplication::translate("Dialogs", "选择项目保存目录"), QString());
    if (parentDir.isEmpty()) {
        return false;
    }

    bool ok = false;
    projectName = QInputDialog::getText(
        parent, QCoreApplication::translate("Dialogs", "新建项目"), QCoreApplication::translate("Dialogs", "机型名称："),
        QLineEdit::Normal, QString(), &ok);
    return ok && !projectName.trimmed().isEmpty();
}

bool showNewComponentDialog(QWidget *parent, QString &name, QString &partNo,
                            const QString &fullPartNoPrefix, int &quantity, QString &remark)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Dialogs", "新建部件"));
    dialog.setMinimumWidth(500);
    auto *form = new QFormLayout(&dialog);

    // 第一行：部件名称
    auto *nameEdit = new QLineEdit(&dialog);

    // 第二行：图号（左，只读前缀+本段输入）与数量（右）同行左右分布
    auto *prefixLabel = new QLabel(fullPartNoPrefix, &dialog);
    prefixLabel->setStyleSheet(QStringLiteral("color: palette(placeholder-text);")); // 只读样式
    auto *partNoEdit = new QLineEdit(&dialog);
    partNoEdit->setPlaceholderText(QStringLiteral("0-9999"));
    auto *quantitySpin = new ReverseWheelSpinBox(&dialog);
    quantitySpin->setRange(1, 999999);
    quantitySpin->setValue(quantity > 0 ? quantity : 1);
    auto *partNoQuantityRow = new QWidget(&dialog);
    auto *partNoQuantityLayout = new QHBoxLayout(partNoQuantityRow);
    partNoQuantityLayout->setContentsMargins(0, 0, 0, 0);
    partNoQuantityLayout->setSpacing(4);
    partNoQuantityLayout->addWidget(new QLabel(QCoreApplication::translate("Dialogs", "图号："), &dialog));
    partNoQuantityLayout->addWidget(prefixLabel);
    partNoQuantityLayout->addWidget(partNoEdit, 1); // 图号输入弹性占位，把数量推到右侧
    partNoQuantityLayout->addSpacing(24);
    partNoQuantityLayout->addWidget(new QLabel(QCoreApplication::translate("Dialogs", "数量："), &dialog));
    partNoQuantityLayout->addWidget(quantitySpin);

    // 备注（可选）
    auto *remarkEdit = createRemarkEdit(&dialog);
    remarkEdit->setPlaceholderText(QCoreApplication::translate("Dialogs", "（可选）"));

    form->addRow(QCoreApplication::translate("Dialogs", "部件名称："), nameEdit);
    form->addRow(nullptr, partNoQuantityRow); // 图号+数量同行（行内自带标签）
    form->addRow(QCoreApplication::translate("Dialogs", "备注："), remarkEdit);

    // 右下角：确定 / 取消（支持多语言）
    auto *buttons = new QDialogButtonBox(&dialog);
    buttons->addButton(QCoreApplication::translate("Dialogs", "确定"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QCoreApplication::translate("Dialogs", "取消"), QDialogButtonBox::RejectRole);
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
        parent, QCoreApplication::translate("Dialogs", "打开项目"), startDirValid);
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
        parent, QCoreApplication::translate("Dialogs", "选择备份保存目录"), QString());
    return !targetDir.isEmpty();
}

bool showNewPartDialog(QWidget *parent, QString &name, QString &partNo,
                       const QString &fullPartNoPrefix, QString &material, int &quantity,
                       QString &pdfFilePath, QString &remark,
                       const QStringList &materialList)
{
    pdfFilePath.clear();
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Dialogs", "新建零件"));
    dialog.setMinimumWidth(430);
    auto *form = new QFormLayout(&dialog);

    // 第一行：零件名称
    auto *nameEdit = new QLineEdit(&dialog);

    // 第二行：图号 = [机型+部件（只读前缀）] + [零件号（用户填写）]
    auto *prefixLabel = new QLabel(fullPartNoPrefix, &dialog);
    prefixLabel->setStyleSheet(QStringLiteral("color: palette(placeholder-text);")); // 只读样式
    auto *partNoEdit = new QLineEdit(&dialog);
    partNoEdit->setPlaceholderText(QCoreApplication::translate("Dialogs", "如 06"));
    auto *partNoRow = new QWidget(&dialog);
    auto *partNoLayout = new QHBoxLayout(partNoRow);
    partNoLayout->setContentsMargins(0, 0, 0, 0);
    partNoLayout->setSpacing(2);
    partNoLayout->addWidget(prefixLabel);
    partNoLayout->addWidget(partNoEdit, 1);

    // 第三行：左边材质（自动补全），右边数量（滚轮向下增加、向上减少，默认 1，最小 1）
    auto *materialEdit = new QLineEdit(&dialog);
    if (!materialList.isEmpty()) {
        auto *completer = new QCompleter(materialList, &dialog);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains); // 模糊包含，像搜索引擎提示
        completer->setCompletionMode(QCompleter::PopupCompletion);
        materialEdit->setCompleter(completer);
    }
    auto *quantitySpin = new ReverseWheelSpinBox(&dialog);
    quantitySpin->setRange(1, 999999);
    quantitySpin->setValue(1);
    auto *attrsRow = new QWidget(&dialog);
    auto *attrsLayout = new QHBoxLayout(attrsRow);
    attrsLayout->setContentsMargins(0, 0, 0, 0);
    attrsLayout->addWidget(new QLabel(QCoreApplication::translate("Dialogs", "材质："), &dialog));
    attrsLayout->addWidget(materialEdit, 1);
    attrsLayout->addSpacing(16);
    attrsLayout->addWidget(new QLabel(QCoreApplication::translate("Dialogs", "数量："), &dialog));
    attrsLayout->addWidget(quantitySpin);

    // 图纸文件（可选）：选择 PDF 作为零件图纸，导入时自动命名
    auto *pdfEdit = new QLineEdit(&dialog);
    pdfEdit->setReadOnly(true);
    pdfEdit->setPlaceholderText(QCoreApplication::translate("Dialogs", "（可选）"));
    auto *pdfButton = new QPushButton(QCoreApplication::translate("Dialogs", "浏览..."), &dialog);
    auto *pdfRow = new QWidget(&dialog);
    auto *pdfLayout = new QHBoxLayout(pdfRow);
    pdfLayout->setContentsMargins(0, 0, 0, 0);
    pdfLayout->addWidget(pdfEdit, 1);
    pdfLayout->addWidget(pdfButton);
    QObject::connect(pdfButton, &QPushButton::clicked, &dialog, [parent, pdfEdit] {
        const QString path = QFileDialog::getOpenFileName(
            parent, QCoreApplication::translate("Dialogs", "选择图纸 PDF"), QString(), QCoreApplication::translate("Dialogs", "PDF 文件 (*.pdf)"));
        if (!path.isEmpty()) {
            pdfEdit->setText(path);
        }
    });

    // 备注（可选）
    auto *remarkEdit = createRemarkEdit(&dialog);
    remarkEdit->setPlaceholderText(QCoreApplication::translate("Dialogs", "（可选）"));

    form->addRow(QCoreApplication::translate("Dialogs", "零件名称："), nameEdit);
    form->addRow(QCoreApplication::translate("Dialogs", "图号："), partNoRow);
    form->addRow(nullptr, attrsRow); // 材质/数量同行（无行标签）
    form->addRow(QCoreApplication::translate("Dialogs", "图纸文件："), pdfRow);
    form->addRow(QCoreApplication::translate("Dialogs", "备注："), remarkEdit);

    // 右下角：确定 / 取消（支持多语言）
    auto *buttons = new QDialogButtonBox(&dialog);
    buttons->addButton(QCoreApplication::translate("Dialogs", "确定"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QCoreApplication::translate("Dialogs", "取消"), QDialogButtonBox::RejectRole);
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
    dialog.setWindowTitle(QCoreApplication::translate("Dialogs", "属性 - %1").arg(nodeName));
    dialog.setMinimumWidth(480);
    auto *form = new QFormLayout(&dialog);

    auto *nameEdit = new QLineEdit(nodeName, &dialog);
    form->addRow(QCoreApplication::translate("Dialogs", "名称："), nameEdit);
    form->addRow(QCoreApplication::translate("Dialogs", "类型："), new QLabel(typeName, &dialog));
    form->addRow(QCoreApplication::translate("Dialogs", "创建时间："), new QLabel(createTimeText, &dialog));

    QLineEdit *partNoEdit = nullptr;
    QSpinBox *componentQuantitySpin = nullptr;
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
        form->addRow(QCoreApplication::translate("Dialogs", "图号前缀："), prefixLabel);
        if (isComponent) {
            // 图号本段（左）与数量（右）同行左右分布
            componentQuantitySpin = new QSpinBox(&dialog);
            componentQuantitySpin->setRange(1, 999999);
            componentQuantitySpin->setValue(componentQuantity);
            auto *partNoQuantityRow = new QWidget(&dialog);
            auto *rowLayout = new QHBoxLayout(partNoQuantityRow);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(4);
            rowLayout->addWidget(new QLabel(QCoreApplication::translate("Dialogs", "图号本段："), &dialog));
            rowLayout->addWidget(partNoEdit, 1); // 图号输入弹性占位，把数量推到右侧
            rowLayout->addSpacing(24);
            rowLayout->addWidget(new QLabel(QCoreApplication::translate("Dialogs", "数量："), &dialog));
            rowLayout->addWidget(componentQuantitySpin);
            form->addRow(nullptr, partNoQuantityRow);
        } else {
            form->addRow(QCoreApplication::translate("Dialogs", "图号本段："), partNoEdit);
        }
        form->addRow(QCoreApplication::translate("Dialogs", "完整图号："), previewLabel);
    }

    QLineEdit *materialEdit = nullptr;
    QSpinBox *quantitySpin = nullptr;
    if (isPart) {
        materialEdit = new QLineEdit(partMaterial, &dialog);
        quantitySpin = new QSpinBox(&dialog);
        quantitySpin->setRange(1, 999999);
        quantitySpin->setValue(partQuantity);
        form->addRow(QCoreApplication::translate("Dialogs", "材质："), materialEdit);
        form->addRow(QCoreApplication::translate("Dialogs", "数量："), quantitySpin);
    }

    if (isComponent && !componentQuantitySpin) {
        // 兜底：无图号部件的数量独立行（正常部件均有图号，此分支实际不触发）
        componentQuantitySpin = new QSpinBox(&dialog);
        componentQuantitySpin->setRange(1, 999999);
        componentQuantitySpin->setValue(componentQuantity);
        form->addRow(QCoreApplication::translate("Dialogs", "数量："), componentQuantitySpin);
    }

    // 备注（部件/零件可编辑；机型不显示）
    QPlainTextEdit *remarkEdit = nullptr;
    if (isPart || isComponent) {
        remarkEdit = createRemarkEdit(&dialog, currentRemark);
        form->addRow(QCoreApplication::translate("Dialogs", "备注："), remarkEdit);
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
        parent, QCoreApplication::translate("Dialogs", "关于艾锐奥智能图纸管理系统"),
        QCoreApplication::translate("Dialogs", "艾锐奥智能图纸管理系统\n\n"
                                               "Power by QT，Designed by Mikufan\n"
                                               "版本：B（内部测试版本）"));
}

bool resolveCopyPartNoConflictDialog(QWidget *parent, QVector<CopyConflictItem> &items)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Dialogs", "复制图号冲突"));
    dialog.setMinimumWidth(560);
    auto *layout = new QVBoxLayout(&dialog);

    auto *hint = new QLabel(
        QCoreApplication::translate("Dialogs", "目标位置存在图号冲突，请修改下图号（前缀已按目标目录自动生成）。"), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *table = new QTableWidget(static_cast<int>(items.size()), 3, &dialog);
    table->setHorizontalHeaderLabels(
        {QCoreApplication::translate("Dialogs", "名称"), QCoreApplication::translate("Dialogs", "原图号"), QCoreApplication::translate("Dialogs", "新图号")});
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
        edit->setPlaceholderText(QCoreApplication::translate("Dialogs", "图号本段"));
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
