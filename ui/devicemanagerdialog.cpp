#include "devicemanagerdialog.h"

#include "model/remotedevice.h"
#include "service/remoteserver.h"
#include "service/remoteprotocol.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

DeviceManagerDialog::DeviceManagerDialog(RemoteServer *server, QWidget *parent)
    : QDialog(parent)
    , m_server(server)
{
    setWindowTitle(QStringLiteral("已授权设备管理"));
    resize(720, 420);

    auto *layout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("设备名称"), QStringLiteral("设备ID"), QStringLiteral("权限"),
         QStringLiteral("首次授权"), QStringLiteral("上次连接"), QStringLiteral("操作")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table);

    auto *btnBar = new QWidget(this);
    auto *btnLayout = new QHBoxLayout(btnBar);
    auto *btnRefresh = new QPushButton(QStringLiteral("刷新"), btnBar);
    auto *btnRename = new QPushButton(QStringLiteral("重命名"), btnBar);
    auto *btnPerm = new QPushButton(QStringLiteral("修改权限"), btnBar);
    auto *btnDelete = new QPushButton(QStringLiteral("删除设备"), btnBar);
    auto *btnClose = new QPushButton(QStringLiteral("关闭"), btnBar);
    btnLayout->addWidget(btnRefresh);
    btnLayout->addWidget(btnRename);
    btnLayout->addWidget(btnPerm);
    btnLayout->addWidget(btnDelete);
    btnLayout->addStretch();
    btnLayout->addWidget(btnClose);
    layout->addWidget(btnBar);

    connect(btnRefresh, &QPushButton::clicked, this, &DeviceManagerDialog::refresh);
    connect(btnRename, &QPushButton::clicked, this, &DeviceManagerDialog::onRename);
    connect(btnPerm, &QPushButton::clicked, this, &DeviceManagerDialog::onChangePermission);
    connect(btnDelete, &QPushButton::clicked, this, &DeviceManagerDialog::onDelete);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    // 设备列表变化（新设备配对）时自动刷新
    if (m_server) {
        connect(m_server, &RemoteServer::deviceListChanged, this, &DeviceManagerDialog::refresh);
    }

    refresh();
}

void DeviceManagerDialog::refresh()
{
    QVector<RemoteDevice> devices;
    if (!m_server || !m_server->listAuthorizedDevices(devices)) {
        m_table->setRowCount(0);
        return;
    }
    m_table->setRowCount(devices.size());
    for (int i = 0; i < devices.size(); ++i) {
        const RemoteDevice &d = devices[i];
        // 设备ID 只显示前 8 位，避免过宽
        const QString idPrefix = d.uuid.left(8) + QStringLiteral("…");
        m_table->setItem(i, 0, new QTableWidgetItem(d.deviceName));
        m_table->setItem(i, 1, new QTableWidgetItem(idPrefix));
        m_table->setItem(i, 2, new QTableWidgetItem(RemoteProtocol::permissionToString(d.permission)));
        m_table->setItem(i, 3,
            new QTableWidgetItem(d.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        m_table->setItem(i, 4,
            new QTableWidgetItem(d.lastSeen.isValid()
                ? d.lastSeen.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                : QStringLiteral("—")));
        m_table->setItem(i, 5, new QTableWidgetItem(
            QStringLiteral("uuid=%1").arg(d.uuid))); // 隐藏完整 uuid 供操作定位
        m_table->item(i, 5)->setData(Qt::UserRole, d.uuid);
        m_table->item(i, 5)->setText(QStringLiteral("—"));
    }
}

void DeviceManagerDialog::onDelete()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    const QString uuid = m_table->item(row, 5)->data(Qt::UserRole).toString();
    const QString name = m_table->item(row, 0)->text();
    if (QMessageBox::question(this, QStringLiteral("删除设备"),
            QStringLiteral("确定删除已授权设备 “%1” 吗？删除后该设备将无法再连接，"
                           "需重新配对。").arg(name)) != QMessageBox::Yes) {
        return;
    }
    if (!m_server->deleteAuthorizedDevice(uuid)) {
        QMessageBox::warning(this, QStringLiteral("删除失败"),
                             QStringLiteral("删除设备记录失败。"));
    }
    refresh();
}

void DeviceManagerDialog::onChangePermission()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    const QString uuid = m_table->item(row, 5)->data(Qt::UserRole).toString();
    QStringList items = {
        RemoteProtocol::permissionToString(RemoteProtocol::Permission::ReadOnly),
        RemoteProtocol::permissionToString(RemoteProtocol::Permission::ReadWrite),
    };
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, QStringLiteral("修改权限"),
        QStringLiteral("选择新的权限级别："), items, 1, false, &ok);
    if (!ok) {
        return;
    }
    m_server->updateDevicePermission(uuid, RemoteProtocol::permissionFromString(chosen));
    refresh();
}

void DeviceManagerDialog::onRename()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    const QString uuid = m_table->item(row, 5)->data(Qt::UserRole).toString();
    const QString oldName = m_table->item(row, 0)->text();
    bool ok = false;
    const QString newName = QInputDialog::getText(this, QStringLiteral("重命名设备"),
        QStringLiteral("设备名称："), QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.trimmed().isEmpty()) {
        return;
    }
    m_server->renameAuthorizedDevice(uuid, newName.trimmed());
    refresh();
}
