#include "remoteprotocol.h"

#include "crypto.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace RemoteProtocol {

QString permissionToString(Permission p)
{
    switch (p) {
    case Permission::ReadOnly:  return QStringLiteral("受限的访问权限");
    case Permission::ReadWrite: return QStringLiteral("完全访问权限");
    case Permission::Admin:     return QStringLiteral("完全访问权限"); // 旧数据兼容：管理权限无独立行为
    case Permission::Denied:    return QStringLiteral("不允许的连接");
    }
    return QStringLiteral("受限的访问权限");
}

Permission permissionFromString(const QString &s)
{
    if (s == QLatin1String("完全访问权限") || s == QLatin1String("可写")
        || s == QLatin1String("ReadWrite") || s == QLatin1String("管理员")
        || s == QLatin1String("Admin")) {
        return Permission::ReadWrite;
    }
    if (s == QLatin1String("不允许的连接") || s == QLatin1String("Denied"))
        return Permission::Denied;
    return Permission::ReadOnly;
}

bool isWriteCommand(const QString &cmd)
{
    // 写操作集合：变更数据的命令。其余（查询/读取/心跳/握手）视为只读
    static const QSet<QString> writeCmds = {
        QLatin1String(kReqCreateComponent),
        QLatin1String(kReqCreatePart),
        QLatin1String(kReqRenameNode),
        QLatin1String(kReqUpdatePartNo),
        QLatin1String(kReqUpdatePartAttrs),
        QLatin1String(kReqUpdateComponentQty),
        QLatin1String(kReqDeleteNode),
        QLatin1String(kReqMoveNode),
        QLatin1String(kReqCopyNode),
        QLatin1String(kReqImportPdf),
        QLatin1String(kReqSetCurrentDrawing),
        QLatin1String(kReqDeleteDrawing),
    };
    return writeCmds.contains(cmd);
}

bool encryptBody(const QByteArray &key, qint64 id, const QJsonObject &payload, QJsonObject &body)
{
    const QByteArray plain = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QByteArray nonce = AesGcm::generateNonce();
    const QByteArray aad = QByteArray::number(id);
    QByteArray cipher, tag;
    if (!AesGcm::encrypt(key, nonce, plain, aad, cipher, tag)) {
        return false;
    }
    body = QJsonObject{
        {QStringLiteral("nonce"),      QString::fromLatin1(nonce.toBase64())},
        {QStringLiteral("ciphertext"), QString::fromLatin1(cipher.toBase64())},
        {QStringLiteral("tag"),        QString::fromLatin1(tag.toBase64())},
    };
    return true;
}

bool decryptBody(const QByteArray &key, qint64 id, const QJsonObject &body, QJsonObject &payload)
{
    const QByteArray nonce = QByteArray::fromBase64(body.value(QStringLiteral("nonce")).toString().toLatin1());
    const QByteArray cipher = QByteArray::fromBase64(body.value(QStringLiteral("ciphertext")).toString().toLatin1());
    const QByteArray tag = QByteArray::fromBase64(body.value(QStringLiteral("tag")).toString().toLatin1());
    const QByteArray aad = QByteArray::number(id);
    QByteArray plain;
    if (!AesGcm::decrypt(key, nonce, cipher, aad, tag, plain)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(plain);
    if (!doc.isObject()) {
        return false;
    }
    payload = doc.object();
    return true;
}


QString dateTimeToString(const QDateTime &dt)
{
    return dt.isValid() ? dt.toString(Qt::ISODate) : QString();
}

QDateTime dateTimeFromString(const QString &text)
{
    const QDateTime dt = QDateTime::fromString(text, Qt::ISODate);
    return dt.isValid() ? dt : QDateTime();
}

QJsonObject nodeToJson(const HFADMNode &node)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), node.id);
    obj.insert(QStringLiteral("parentId"), node.parentId);
    obj.insert(QStringLiteral("name"), node.name);
    obj.insert(QStringLiteral("type"), static_cast<int>(node.type));
    obj.insert(QStringLiteral("partNo"), node.partNo);
    obj.insert(QStringLiteral("createTime"), dateTimeToString(node.createTime));
    obj.insert(QStringLiteral("updateTime"), dateTimeToString(node.updateTime));
    obj.insert(QStringLiteral("deleted"), node.deleted);
    return obj;
}

HFADMNode nodeFromJson(const QJsonObject &obj)
{
    HFADMNode node;
    node.id = obj.value(QStringLiteral("id")).toVariant().toLongLong();
    node.parentId = obj.value(QStringLiteral("parentId")).toVariant().toLongLong();
    node.name = obj.value(QStringLiteral("name")).toString();
    node.type = static_cast<NodeType>(obj.value(QStringLiteral("type")).toInt());
    node.partNo = obj.value(QStringLiteral("partNo")).toString();
    node.createTime = dateTimeFromString(obj.value(QStringLiteral("createTime")).toString());
    node.updateTime = dateTimeFromString(obj.value(QStringLiteral("updateTime")).toString());
    node.deleted = obj.value(QStringLiteral("deleted")).toBool();
    return node;
}

QJsonObject drawingToJson(const Drawing &drawing)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), drawing.id);
    obj.insert(QStringLiteral("partId"), drawing.partId);
    obj.insert(QStringLiteral("partNodeId"), drawing.partNodeId);
    obj.insert(QStringLiteral("fileName"), drawing.fileName);
    obj.insert(QStringLiteral("filePath"), drawing.filePath);
    obj.insert(QStringLiteral("version"), drawing.version);
    obj.insert(QStringLiteral("isCurrent"), drawing.isCurrent);
    obj.insert(QStringLiteral("createTime"), dateTimeToString(drawing.createTime));
    obj.insert(QStringLiteral("deleted"), drawing.deleted);
    return obj;
}

Drawing drawingFromJson(const QJsonObject &obj)
{
    Drawing drawing;
    drawing.id = obj.value(QStringLiteral("id")).toVariant().toLongLong();
    drawing.partId = obj.value(QStringLiteral("partId")).toVariant().toLongLong();
    drawing.partNodeId = obj.value(QStringLiteral("partNodeId")).toVariant().toLongLong();
    drawing.fileName = obj.value(QStringLiteral("fileName")).toString();
    drawing.filePath = obj.value(QStringLiteral("filePath")).toString();
    drawing.version = obj.value(QStringLiteral("version")).toString();
    drawing.isCurrent = obj.value(QStringLiteral("isCurrent")).toBool();
    drawing.createTime = dateTimeFromString(obj.value(QStringLiteral("createTime")).toString());
    drawing.deleted = obj.value(QStringLiteral("deleted")).toBool();
    return drawing;
}

QJsonObject partToJson(const Part &part)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), part.id);
    obj.insert(QStringLiteral("nodeId"), part.nodeId);
    obj.insert(QStringLiteral("material"), part.material);
    obj.insert(QStringLiteral("quantity"), part.quantity);
    return obj;
}

Part partFromJson(const QJsonObject &obj)
{
    Part part;
    part.id = obj.value(QStringLiteral("id")).toVariant().toLongLong();
    part.nodeId = obj.value(QStringLiteral("nodeId")).toVariant().toLongLong();
    part.material = obj.value(QStringLiteral("material")).toString();
    part.quantity = obj.value(QStringLiteral("quantity")).toInt();
    return part;
}

QJsonObject componentToJson(const Component &component)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), component.id);
    obj.insert(QStringLiteral("nodeId"), component.nodeId);
    obj.insert(QStringLiteral("quantity"), component.quantity);
    return obj;
}

Component componentFromJson(const QJsonObject &obj)
{
    Component component;
    component.id = obj.value(QStringLiteral("id")).toVariant().toLongLong();
    component.nodeId = obj.value(QStringLiteral("nodeId")).toVariant().toLongLong();
    component.quantity = obj.value(QStringLiteral("quantity")).toInt();
    return component;
}

QJsonObject directoryItemToJson(const DirectoryItem &item)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("kind"), item.kind == DirectoryItem::Kind::Drawing ? 1 : 0);
    obj.insert(QStringLiteral("node"), nodeToJson(item.node));
    obj.insert(QStringLiteral("drawing"), drawingToJson(item.drawing));
    obj.insert(QStringLiteral("fullPartNo"), item.fullPartNo);
    obj.insert(QStringLiteral("pathHint"), item.pathHint);
    obj.insert(QStringLiteral("quantity"), item.quantity);
    obj.insert(QStringLiteral("partWithoutDrawing"), item.partWithoutDrawing);
    return obj;
}

DirectoryItem directoryItemFromJson(const QJsonObject &obj)
{
    DirectoryItem item;
    item.kind = obj.value(QStringLiteral("kind")).toInt() == 1
        ? DirectoryItem::Kind::Drawing
        : DirectoryItem::Kind::Node;
    item.node = nodeFromJson(obj.value(QStringLiteral("node")).toObject());
    item.drawing = drawingFromJson(obj.value(QStringLiteral("drawing")).toObject());
    item.fullPartNo = obj.value(QStringLiteral("fullPartNo")).toString();
    item.pathHint = obj.value(QStringLiteral("pathHint")).toString();
    item.quantity = obj.value(QStringLiteral("quantity")).toInt(-1);
    item.partWithoutDrawing = obj.value(QStringLiteral("partWithoutDrawing")).toBool();
    return item;
}

} // namespace RemoteProtocol
