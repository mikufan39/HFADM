#include "devicemanagerdialog.h"

#include "model/remotedevice.h"
#include "service/remoteserver.h"
#include "service/remoteprotocol.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
constexpr int kColName = 0; // 设备名称
constexpr int kColPerm = 1; // 权限（下拉选择）
constexpr int kColCreated = 2; // 首次授权
constexpr int kColLastSeen = 3; // 上次连接
} // namespace

DeviceManagerDialog::DeviceManagerDialog(RemoteServer *server, QWidget *parent)
    : QDialog(parent)
    , m_server(server)
{
    setWindowTitle(tr("授权管理"));
    resize(680, 420);

    auto *layout = new QVBoxLayout(this);

    // 顶部操作栏：刷新 / 重命名 / 删除设备
    auto *toolBar = new QWidget(this);
    auto *toolLayout = new QHBoxLayout(toolBar);
    toolLayout->setContentsMargins(0, 0, 0, 0);
    auto *btnRefresh = new QPushButton(tr("刷新"), toolBar);
    m_btnRename = new QPushButton(tr("重命名"), toolBar);
    m_btnDelete = new QPushButton(tr("删除设备"), toolBar);
    toolLayout->addWidget(btnRefresh);
    toolLayout->addWidget(m_btnRename);
    toolLayout->addWidget(m_btnDelete);
    toolLayout->addStretch();
    layout->addWidget(toolBar);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {tr("设备名称"), tr("权限"),
         tr("首次授权"), tr("上次连接")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    // 首次授权/上次连接列可压缩，权限列固定宽度
    m_table->setColumnWidth(kColPerm, 170);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColPerm, QHeaderView::Fixed);
    layout->addWidget(m_table);

    connect(btnRefresh, &QPushButton::clicked, this, &DeviceManagerDialog::refresh);
    connect(m_btnRename, &QPushButton::clicked, this, &DeviceManagerDialog::onRename);
    connect(m_btnDelete, &QPushButton::clicked, this, &DeviceManagerDialog::onDelete);
    // 选中行变化时更新按钮可用状态（仅黑名单设备不可重命名）
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &DeviceManagerDialog::updateButtonState);
    // 设备列表变化（新设备配对/权限变更/删除）时自动刷新。
    // 使用 QueuedConnection：避免在权限下拉框的信号处理栈内重建表格，
    // 导致正在交互的下拉控件被提前销毁
    if (m_server) {
        connect(m_server, &RemoteServer::deviceListChanged,
                this, &DeviceManagerDialog::refresh, Qt::QueuedConnection);
    }

    refresh();
}

void DeviceManagerDialog::refresh()
{
    QVector<RemoteDevice> devices;
    if (!m_server || !m_server->listAuthorizedDevices(devices)) {
        m_table->setRowCount(0);
        updateButtonState();
        return;
    }
    m_table->setRowCount(devices.size());
    for (int i = 0; i < devices.size(); ++i) {
        const RemoteDevice &d = devices[i];
        // 设备名称（uuid 与 ignoredOnly 标记存于该单元格，供操作定位）
        auto *nameItem = new QTableWidgetItem(d.deviceName);
        nameItem->setData(Qt::UserRole, d.uuid);
        nameItem->setData(Qt::UserRole + 1, d.ignoredOnly);
        if (d.ignoredOnly) {
            nameItem->setForeground(Qt::darkGray);
        }
        m_table->setItem(i, kColName, nameItem);

        // 权限列：下拉选择（受限的访问权限 / 完全访问权限 / 不允许的连接）
        auto *combo = new QComboBox(m_table);
        combo->addItem(RemoteProtocol::permissionToString(RemoteProtocol::Permission::ReadOnly),
                       static_cast<int>(RemoteProtocol::Permission::ReadOnly));
        combo->addItem(RemoteProtocol::permissionToString(RemoteProtocol::Permission::ReadWrite),
                       static_cast<int>(RemoteProtocol::Permission::ReadWrite));
        combo->addItem(RemoteProtocol::permissionToString(RemoteProtocol::Permission::Denied),
                       static_cast<int>(RemoteProtocol::Permission::Denied));
        // 旧版本 Admin 权限（行为等价完全访问权限）映射到完全访问权限
        RemoteProtocol::Permission shown = d.permission;
        if (shown == RemoteProtocol::Permission::Admin) {
            shown = RemoteProtocol::Permission::ReadWrite;
        }
        combo->setCurrentIndex(combo->findData(static_cast<int>(shown)));
        m_table->setCellWidget(i, kColPerm, combo);
        const QString uuid = d.uuid;
        const bool wasIgnoredOnly = d.ignoredOnly;
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, combo, uuid, wasIgnoredOnly](int index) {
            const auto perm = static_cast<RemoteProtocol::Permission>(
                combo->itemData(index).toInt());
            if (!m_server->updateDevicePermission(uuid, perm)) {
                QMessageBox::warning(this, tr("修改权限失败"),
                                     tr("无法更新该设备的权限。"));
                refresh(); // 失败回滚显示
                return;
            }
            // 纯黑名单设备（配对被拒、无设备记录与密钥）：解除禁止后仅回到未授权状态，
            // 需由客户端重新发起配对才能连接
            if (wasIgnoredOnly && !RemoteProtocol::isDenied(perm)) {
                QMessageBox::information(this, tr("授权管理"),
                    tr("该设备从未完成过授权，解除禁止后需由客户端重新发起配对连接。"));
            }
            // 成功：deviceListChanged 信号（QueuedConnection）会自动刷新列表
        });

        m_table->setItem(i, kColCreated,
            new QTableWidgetItem(d.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        m_table->setItem(i, kColLastSeen,
            new QTableWidgetItem(d.lastSeen.isValid()
                ? d.lastSeen.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                : QStringLiteral("—"))); // 破折号占位符，不参与翻译
    }
    updateButtonState();
}

QString DeviceManagerDialog::currentUuid() const
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, kColName)) {
        return QString();
    }
    return m_table->item(row, kColName)->data(Qt::UserRole).toString();
}

bool DeviceManagerDialog::currentIgnoredOnly() const
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, kColName)) {
        return false;
    }
    return m_table->item(row, kColName)->data(Qt::UserRole + 1).toBool();
}

void DeviceManagerDialog::updateButtonState()
{
    const bool hasRow = m_table->currentRow() >= 0;
    m_btnRename->setEnabled(hasRow && !currentIgnoredOnly());
    m_btnDelete->setEnabled(hasRow);
}

void DeviceManagerDialog::onDelete()
{
    const QString uuid = currentUuid();
    if (uuid.isEmpty()) {
        return;
    }
    const bool ignoredOnly = currentIgnoredOnly();
    const QString name = m_table->item(m_table->currentRow(), kColName)->text();
    const QString msg = ignoredOnly
        ? tr("确定解除对设备 “%1” 的禁止连接吗？解除后该设备可重新配对连接。").arg(name)
        : tr("确定删除设备 “%1” 吗？删除后该设备将无法再连接，需重新配对。").arg(name);
    if (QMessageBox::question(this, tr("删除设备"), msg) != QMessageBox::Yes) {
        return;
    }
    if (!m_server->deleteAuthorizedDevice(uuid)) {
        QMessageBox::warning(this, tr("删除失败"),
                             tr("删除设备记录失败。"));
    }
    refresh();
}

void DeviceManagerDialog::onRename()
{
    const QString uuid = currentUuid();
    if (uuid.isEmpty() || currentIgnoredOnly()) {
        return;
    }
    const QString oldName = m_table->item(m_table->currentRow(), kColName)->text();
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("重命名设备"),
        tr("设备名称："), QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.trimmed().isEmpty()) {
        return;
    }
    m_server->renameAuthorizedDevice(uuid, newName.trimmed());
    refresh();
}
