// 远程访问协议端到端测试（第二版：配对 + AES-128-GCM 加密会话）
// 本机回环 127.0.0.1 走服务端/客户端全流程：
//   首次连接→配对(自动批准)→加密业务全流程→断开→二次连接→认证(挑战-响应)→验证
#include "database/databasemanager.h"
#include "model/component.h"
#include "model/drawing.h"
#include "model/hfdadnode.h"
#include "model/part.h"
#include "service/clientcredentialstore.h"
#include "service/drawingservice.h"
#include "service/nodeservice.h"
#include "service/projectservice.h"
#include "service/remoteclient.h"
#include "service/remoteserver.h"
#include "service/remoteprotocol.h"
#include "ui/nodetablemodel.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            qInfo() << "[PASS]" << msg;                                        \
        } else {                                                              \
            qCritical() << "[FAIL]" << msg;                                    \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // 清理可能残留的 127.0.0.1 凭证，确保首次走配对流程
    ClientCredentialStore::clear(QStringLiteral("127.0.0.1"));

    QTemporaryDir tmpDir;
    const QString projectPath = tmpDir.filePath(QStringLiteral("TestProject"));

    ProjectService projectService;
    if (!projectService.createProject(projectPath, QStringLiteral("测试机型"))) {
        qCritical() << "createProject 失败:" << projectService.lastError();
        return 1;
    }
    DatabaseManager *db = projectService.databaseManager();
    NodeService nodeService(db);
    nodeService.setProjectPath(projectPath); // 删除节点时清理图纸文件需项目路径
    DrawingService drawingService(db);

    // 查询机型根节点 id
    qint64 rootId = 0;
    {
        QSqlQuery q(db->database());
        if (q.exec(QStringLiteral("SELECT id FROM node WHERE type=1 AND deleted=0 LIMIT 1"))
            && q.next()) {
            rootId = q.value(0).toLongLong();
        }
    }
    CHECK(rootId != 0, "机型根节点存在");

    // ---- 服务端：配对解析器自动批准为可写权限（测试无 GUI）----
    RemoteServer server(&projectService, &nodeService, &drawingService);
    server.setPairingResolver([](const RemoteProtocol::PairingRequest &) {
        RemoteProtocol::PairingResult r;
        r.accepted = true;
        r.permission = RemoteProtocol::Permission::ReadWrite;
        return r;
    });
    QString err;
    CHECK(server.start(projectPath, QStringLiteral("测试机型"), &err),
          QStringLiteral("服务端开放 312 成功（%1）").arg(err));
    if (!server.isRunning()) {
        return 1;
    }
    CHECK(!RemoteServer::localAddresses().isEmpty(), "枚举到局域网 IPv4 地址");

    // ---- 客户端首次连接：走配对流程 ----
    RemoteClient client;
    CHECK(client.connectTo(QStringLiteral("127.0.0.1"), &err),
          QStringLiteral("客户端首次连接(配对)成功（%1）").arg(err));
    if (!client.isConnected()) {
        return 1;
    }
    CHECK(client.projectName() == QStringLiteral("测试机型"), "握手获取机型名正确");
    CHECK(client.rootNodeId() == rootId, "握手返回机型根节点 id 正确");
    CHECK(client.permission() == RemoteProtocol::Permission::ReadWrite, "配对权限为可写");

    // 列表根目录（机型根节点下）应为空，未建任何子节点
    QVector<DirectoryItem> items;
    CHECK(client.listDir(rootId, items, &err), "listDir 根目录");
    CHECK(items.isEmpty(), "根目录（机型下）初始为空");

    // 新建部件（加密写操作）
    CHECK(client.createComponent(rootId, QStringLiteral("机翼"), QStringLiteral("100"), 2, &err),
          QStringLiteral("createComponent（%1）").arg(err));
    CHECK(client.listDir(rootId, items, &err) && items.size() == 1, "建后目录 1 项");
    const qint64 nodeId = items.first().node.id;
    CHECK(items.first().node.type == NodeType::Component
              && items.first().node.name == QStringLiteral("机翼"),
          "部件节点数据正确");
    CHECK(items.first().quantity == 2, "部件数量为 2");
    CHECK(items.first().fullPartNo == QStringLiteral("测试机型.100"), "部件完整图号正确");

    // 图号占用 / 格式 / 完整图号（加密读操作）
    CHECK(client.isPartNoOccupied(NodeType::Component, QStringLiteral("100"), rootId, 0, &err),
          "图号段占用检查命中");
    CHECK(!client.isPartNoOccupied(NodeType::Component, QStringLiteral("200"), rootId, 0, &err),
          "未占用段检查通过");
    QString full;
    CHECK(client.computeFullPartNo(nodeId, full, &err) && full == QStringLiteral("测试机型.100"),
          "computeFullPartNo 正确");

    // 新建零件 + 属性
    qint64 partId = 0;
    CHECK(client.createPart(nodeId, QStringLiteral("翼肋"), QStringLiteral("001"),
                            QStringLiteral("铝合金"), 4, &partId, &err),
          QStringLiteral("createPart（%1）").arg(err));
    HFADMNode part;
    CHECK(client.getNode(partId, part, &err) && part.name == QStringLiteral("翼肋"),
          "getNode 零件");
    Part partAttr;
    CHECK(client.loadPart(partId, partAttr, &err) && partAttr.material == QStringLiteral("铝合金")
              && partAttr.quantity == 4,
          "loadPart 属性正确");

    // 重命名
    CHECK(client.renameNode(nodeId, QStringLiteral("机翼组"), &err), "renameNode");
    CHECK(client.getNode(nodeId, part, &err) && part.name == QStringLiteral("机翼组"),
          "重命名生效");

    // 属性更新（零件材质/数量）
    CHECK(client.updatePartAttributes(partId, QStringLiteral("碳纤维"), 8, &err),
          "updatePartAttributes");
    CHECK(client.loadPart(partId, partAttr, &err) && partAttr.quantity == 8,
          "零件数量更新为 8");

    // 祖先路径（搜索定位）
    QString path;
    CHECK(client.getPath(partId, rootId, path, &err) && path == QStringLiteral("机翼组"),
          "getPath 祖先路径");

    // 递归搜索
    QVector<DirectoryItem> found;
    CHECK(client.search(rootId, QStringLiteral("翼肋"), found, &err) && found.size() == 1,
          "递归搜索命中零件");

    // 删除
    CHECK(client.deleteNode(partId, &err), "deleteNode 零件");
    CHECK(client.deleteNode(nodeId, &err), "deleteNode 部件");
    CHECK(client.listDir(rootId, items, &err) && items.isEmpty(), "删除后目录为空");

    // 断开
    client.disconnectFrom();
    CHECK(!client.isConnected(), "客户端已断开");

    // ---- 二次连接：走认证流程（挑战-响应，凭证已存）----
    RemoteClient client2;
    CHECK(client2.connectTo(QStringLiteral("127.0.0.1"), &err),
          QStringLiteral("客户端二次连接(认证)成功（%1）").arg(err));
    CHECK(client2.isConnected(), "二次连接已建立");
    CHECK(client2.projectName() == QStringLiteral("测试机型"), "认证后机型名正确");
    CHECK(client2.rootNodeId() == rootId, "认证后根节点 id 正确");
    // 认证后加密读操作应正常
    CHECK(client2.listDir(rootId, items, &err) && items.isEmpty(), "认证后 listDir 正常");
    client2.disconnectFrom();

    // 关闭
    server.stop();
    CHECK(!server.isRunning(), "服务端已停止");

    // 清理测试凭证
    ClientCredentialStore::clear(QStringLiteral("127.0.0.1"));

    qInfo() << "失败数:" << g_failures;

    // 结果落盘（Windows 下控制台日志可能被吞，提供硬证据）
    {
        QFile result(QStringLiteral("remote_test_result.txt"));
        if (result.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            result.write(QStringLiteral("failures=%1\n").arg(g_failures).toUtf8());
        }
    }
    return g_failures == 0 ? 0 : 1;
}
