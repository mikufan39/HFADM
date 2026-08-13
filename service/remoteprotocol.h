#ifndef REMOTEPROTOCOL_H
#define REMOTEPROTOCOL_H

#include "model/component.h"
#include "model/drawing.h"
#include "model/hfdadnode.h"
#include "model/part.h"
#include "ui/nodetablemodel.h"

#include <QJsonObject>
#include <QString>

// 远程访问协议（第二版）：首次配对口令 + AES-128-GCM 加密会话
//   帧格式：单行 JSON + '\n'
//   控制帧（明文，仅握手阶段）：
//     请求： {"type":"request","cmd":"<connect|auth|authResp>","id":N,"body":{...}}
//     响应： {"type":"response","id":N,"success":bool,"body":{...}}
//   业务帧（加密，握手成功后）：
//     请求： {"type":"request","cmd":"<业务类型>","id":N,"body":{"nonce","ciphertext","tag"}}
//            其中 body 解密后为业务参数 JSON
//     响应： {"type":"response","id":N,"body":{"nonce","ciphertext","tag"}}
//            其中 body 解密后为 {"success":bool,"data":{...},"message":"..."}
//   防重放：业务帧 id 必须严格递增，AAD 绑定 id。
// 所有读写操作均由服务端（开放方）执行，客户端只发请求、收结果；
// 服务端处理写请求前切换到自己绑定的项目上下文，处理完恢复，保证本地 UI 不错库。
namespace RemoteProtocol {

// 端口统一 312
constexpr quint16 kPort = 312;
constexpr int kProtocolVersion = 2;
// 单请求同步等待超时（毫秒，业务请求）
constexpr int kRequestTimeoutMs = 5000;
// 连接/握手超时（毫秒）：TCP 建立连接、配对（connect/auth/authResp）等待服务端响应的时限，
// 与配对口令 60 秒有效时间一致
constexpr int kConnectTimeoutMs = 60000;
// 客户端心跳发送间隔（毫秒）
constexpr int kHeartbeatIntervalMs = 15000;
// 静默超时：超过该时长未收到对方任何帧，判定断线（毫秒）
constexpr int kSilenceTimeoutMs = 60000;

// ---- 请求类型常量（业务命令，加密传输时作为 envelope 的 cmd 字段）----
inline constexpr const char *kReqHello = "hello";                          // 握手：返回机型信息
inline constexpr const char *kReqListDir = "listDir";                      // 目录项列表
inline constexpr const char *kReqSearch = "search";                        // 递归搜索
inline constexpr const char *kReqGetNode = "getNode";                      // 单个节点
inline constexpr const char *kReqGetPath = "getPath";                      // 祖先路径（响应含 chain 完整链）
inline constexpr const char *kReqResolvePath = "resolvePath";              // 按名称路径解析节点（地址栏跳转）
inline constexpr const char *kReqCreateComponent = "createComponent";      // 新建部件
inline constexpr const char *kReqCreatePart = "createPart";                // 新建零件
inline constexpr const char *kReqRenameNode = "renameNode";                // 重命名
inline constexpr const char *kReqUpdatePartNo = "updatePartNo";            // 改图号段
inline constexpr const char *kReqUpdatePartAttrs = "updatePartAttributes"; // 零件材质/数量
inline constexpr const char *kReqUpdateComponentQty = "updateComponentQuantity"; // 部件数量
inline constexpr const char *kReqUpdateNodeRemark = "updateNodeRemark";    // 更新节点备注
inline constexpr const char *kReqDeleteNode = "deleteNode";                // 删除（物理删除，不可恢复）
inline constexpr const char *kReqMoveNode = "moveNode";                    // 移动（剪切粘贴）
inline constexpr const char *kReqCopyNode = "copyNode";                    // 复制
inline constexpr const char *kReqIsPartNoOccupied = "isPartNoOccupied";    // 图号段占用检查
inline constexpr const char *kReqIsValidPartNo = "isValidPartNoFormat";    // 图号段格式校验
inline constexpr const char *kReqComputeFullPartNo = "computeFullPartNo";  // 完整图号
inline constexpr const char *kReqLoadPart = "loadPart";                    // 零件属性
inline constexpr const char *kReqLoadComponent = "loadComponent";          // 部件属性
inline constexpr const char *kReqImportPdf = "importPdf";                  // 导入 PDF（base64）
inline constexpr const char *kReqSetCurrentDrawing = "setCurrentDrawing";  // 设为当前版本
inline constexpr const char *kReqDeleteDrawing = "deleteDrawing";          // 删除图纸（物理删除）
inline constexpr const char *kReqGetDrawingFile = "getDrawingFile";        // 拉取图纸文件（base64）
inline constexpr const char *kReqHeartbeat = "heartbeat";                  // 心跳

// ---- 控制消息命令常量（握手阶段，明文传输）----
inline constexpr const char *kCmdConnect = "connect";      // 首次配对请求
inline constexpr const char *kCmdAuth = "auth";            // 二次连接认证请求
inline constexpr const char *kCmdAuthResp = "authResp";    // 客户端对挑战的加密响应
inline constexpr const char *kCmdCancelPair = "cancelPair"; // 客户端取消配对（关闭服务端挂起的确认弹窗）

// ---- 权限级别 ----
//   ReadOnly/ReadWrite：正常授权（受限/完全访问）
//   Admin：旧版本遗留的管理权限（行为等价 ReadWrite，新 UI 不再提供选择，
//          仅用于兼容历史设备记录，读取时视为完全访问权限）
//   Denied：不允许的连接（黑名单）。设备管理界面可将设备改为该权限以禁止连接，
//          或改回正常权限以解除禁止；被禁止的设备在配对/认证/业务阶段一律拒绝
enum class Permission { ReadOnly = 0, ReadWrite = 1, Admin = 2, Denied = 3 };
// 是否为黑名单（不允许连接）状态
inline bool isDenied(Permission p) { return p == Permission::Denied; }
// UI 显示名：受限的访问权限 / 完全访问权限 / 不允许的连接
QString permissionToString(Permission p);
Permission permissionFromString(const QString &s);
// 该业务命令是否为写操作（用于只读权限拦截）
bool isWriteCommand(const QString &cmd);
// 写操作幂等键：命令 + 参数规范化（按键排序 JSON）后 SHA-256。
// 客户端对写命令自动计算并随请求发送；服务端按 (设备uuid, 幂等键) 去重。
// 用户重试相同操作（参数不变）→ 相同键 → 服务端命中返回首次结果，不重复执行。
QString computeIdempotencyKey(const QString &cmd, const QJsonObject &params);

// ---- 配对请求/结果（服务端弹窗确认用）----
struct PairingRequest {
    QString uuid;          // 客户端设备唯一 ID
    QString deviceName;    // 设备名称
    QString pin;           // 4 位口令
    QString peerAddress;   // 客户端 IP（展示用）
};
struct PairingResult {
    bool accepted = false;
    Permission permission = Permission::ReadOnly;
    // 拒绝且不再提示：服务端将该设备 uuid 记入黑名单，之后直接拒绝不再弹窗
    bool neverAskAgain = false;
};

// ---- 加密封装 ----
// 将明文 payload 用 AES-128-GCM 加密为 body{nonce,ciphertext,tag}，AAD 绑定 id
//   key: 16 字节会话密钥；id: 帧序号（同时作为 AAD 与防重放依据）
bool encryptBody(const QByteArray &key, qint64 id, const QJsonObject &payload, QJsonObject &body);
// 解密 body 取 payload；失败返回 false
bool decryptBody(const QByteArray &key, qint64 id, const QJsonObject &body, QJsonObject &payload);

// ---- 序列化：节点 ----
QJsonObject nodeToJson(const HFADMNode &node);
HFADMNode nodeFromJson(const QJsonObject &obj);

// ---- 序列化：图纸 ----
QJsonObject drawingToJson(const Drawing &drawing);
Drawing drawingFromJson(const QJsonObject &obj);

// ---- 序列化：零件 / 部件属性 ----
QJsonObject partToJson(const Part &part);
Part partFromJson(const QJsonObject &obj);
QJsonObject componentToJson(const Component &component);
Component componentFromJson(const QJsonObject &obj);

// ---- 序列化：目录项（节点/图纸混合行）----
QJsonObject directoryItemToJson(const DirectoryItem &item);
DirectoryItem directoryItemFromJson(const QJsonObject &obj);

// ---- 序列化：时间 ----
QString dateTimeToString(const QDateTime &dt);
QDateTime dateTimeFromString(const QString &text);

} // namespace RemoteProtocol

#endif // REMOTEPROTOCOL_H
