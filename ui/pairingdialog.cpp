#include "pairingdialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

RemoteProtocol::PairingResult showPairingDialog(QWidget *parent,
                                                const RemoteProtocol::PairingRequest &req)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("新客户端请求连接"));
    dlg.setMinimumWidth(420);

    auto *layout = new QVBoxLayout(&dlg);

    auto *title = new QLabel(
        QStringLiteral("客户端 “%1” 请求连接。\n来源 IP：%2\n\n请核对显示的配对口令，"
                       "并选择授权权限：")
            .arg(req.deviceName.isEmpty() ? QStringLiteral("(未知设备)") : req.deviceName,
                 req.peerAddress),
        &dlg);
    title->setWordWrap(true);
    layout->addWidget(title);

    // 大字号醒目显示口令
    auto *pinLabel = new QLabel(req.pin, &dlg);
    QFont f = pinLabel->font();
    f.setPointSize(36);
    f.setBold(true);
    pinLabel->setFont(f);
    pinLabel->setAlignment(Qt::AlignCenter);
    pinLabel->setStyleSheet(QStringLiteral("color:#c0392b; padding:12px;"));
    layout->addWidget(pinLabel);

    // 权限选择
    auto *permBox = new QGroupBox(QStringLiteral("授权权限"), &dlg);
    auto *permLayout = new QVBoxLayout(permBox);
    auto *rbRead = new QRadioButton(QStringLiteral("只读（仅查看，不可修改）"), permBox);
    auto *rbWrite = new QRadioButton(QStringLiteral("可写（查看并修改）"), permBox);
    rbWrite->setChecked(true); // 默认可写
    permLayout->addWidget(rbRead);
    permLayout->addWidget(rbWrite);
    layout->addWidget(permBox);

    auto *buttonBox = new QDialogButtonBox(&dlg);
    auto *allowBtn = buttonBox->addButton(QStringLiteral("允许"), QDialogButtonBox::AcceptRole);
    buttonBox->addButton(QStringLiteral("拒绝"), QDialogButtonBox::RejectRole);
    allowBtn->setDefault(true);
    layout->addWidget(buttonBox);

    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    RemoteProtocol::PairingResult result;
    result.accepted = (dlg.exec() == QDialog::Accepted);
    result.permission = rbWrite->isChecked() ? RemoteProtocol::Permission::ReadWrite
                                             : RemoteProtocol::Permission::ReadOnly;
    return result;
}
