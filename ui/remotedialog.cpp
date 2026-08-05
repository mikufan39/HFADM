#include "remotedialog.h"

#include <QAbstractSocket>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// 仅接受局域网 192.168.0.0/16 段的 IPv4 地址
bool isPrivateIPv4(const QString &text)
{
    QHostAddress addr;
    if (!addr.setAddress(text.trimmed())) {
        return false;
    }
    if (addr.protocol() != QAbstractSocket::IPv4Protocol) {
        return false;
    }
    const quint32 v = addr.toIPv4Address();
    return (v & 0xFFFF0000u) == 0xC0A80000u; // 192.168.0.0/16
}

} // namespace

bool showRemoteConnectDialog(QWidget *parent, QString &address)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("连接到远程"));
    dlg.setMinimumWidth(440);

    auto *layout = new QVBoxLayout(&dlg);
    auto *hint = new QLabel(
        QStringLiteral("输入目标机器的局域网 IP 地址（仅支持 IPv4 局域网地址，端口固定 312）："),
        &dlg);
    hint->setWordWrap(true);
    auto *edit = new QLineEdit(&dlg);
    edit->setPlaceholderText(QStringLiteral("例如 192.168.1.100"));
    edit->setClearButtonEnabled(true);
    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("连接"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    layout->addWidget(hint);
    layout->addWidget(edit);
    layout->addWidget(buttonBox);

    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    while (dlg.exec() == QDialog::Accepted) {
        const QString text = edit->text().trimmed();
        if (!isPrivateIPv4(text)) {
            QMessageBox::warning(&dlg, QStringLiteral("地址无效"),
                                 QStringLiteral("请输入局域网 192.168.x.x 段的 IPv4 地址。"));
            continue;
        }
        address = text;
        return true;
    }
    return false;
}
