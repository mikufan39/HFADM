# HFADM — 无人机硬件装配数据管理系统

面向无人机硬件装配场景的 Windows 桌面数据管理系统。以「项目 → 机型 → 部件 → 零件」的树形结构组织装配数据，集中管理零件属性与图纸资料，并支持局域网内远程访问与协作。

## ✨ 主要功能

- **树形结构管理**：项目 → 机型 → 部件 → 零件，节点无限层级；支持新建、重命名、复制、剪切粘贴、删除
- **零件属性**：图号（自动组合完整图号）、名称、材质、数量
- **图纸管理**：每个零件可挂多份图纸，多版本历史保留、当前版标记、导入 / 导出 / 预览 / 删除
- **PDF 内置预览**：基于 Qt PDF 的缩放、翻页、跳页查看，无需外部阅读器
- **局域网远程访问**（协议 v2）：AES-128-GCM 加密会话、首次配对口令（PIN）、连接状态机、设备授权管理与黑名单
- **操作日志**：关键操作全量记录
- **界面体验**：资源管理器式浏览标签页、最近项目、会话恢复、本地 ↔ 远程剪贴板（跨域互拒）、删除进度弹窗

## 🛠 技术栈

| 项目 | 说明 |
|---|---|
| 语言 / 框架 | C++17 / Qt 6.11.1 Widgets（开发环境） |
| 构建 | CMake ≥ 3.19 + MinGW 13.1 |
| 数据库 | SQLite（WAL 模式，外键开启） |
| PDF | Qt PDF / PdfWidgets |
| 加密 | AES-128-GCM，Windows 端走 BCrypt/CNG 原生 API |
| 其他 | Qt Network（远程 TCP）、Qt Svg（图标）、Qt Linguist（中文本地化） |
| 平台 | Windows 单机 |

## 🏗 架构

采用分层架构，UI 不直接接触数据库：

```
UI ──> Service ──> Model ──> Database
```

- **UI**（`mainwindow` + `ui/`）：主窗口、表格模型、浏览标签、右键菜单、各业务对话框
- **Service**（`service/`）：项目 / 节点 / 图纸业务逻辑；远程协议、服务端、客户端；加密、凭据存储、应用配置
- **Model**（`model/`）：纯数据结构（Node / Part / Drawing / ProjectInfo / RemoteDevice）
- **Database**（`database/`）：`DatabaseManager` 统一管理连接与全部 SQL，`DrawingRepository` 图纸仓储

设计原则：**DB 管关系、文件管文件** —— 数据库只存关系与元数据，图纸 PDF 平铺在项目 `files/` 目录下。

## 🗄 数据模型（7 张表）

| 表 | 说明 |
|---|---|
| `project` | 项目信息（一项目一机型） |
| `node` | 树节点（type：1 机型 / 2 部件 / 3 零件，parent_id 无限级，含图号唯一索引） |
| `part` | 零件属性（材质、数量） |
| `component` | 部件属性（数量） |
| `drawing` | 图纸记录（版本、is_current 当前版标记） |
| `operation_log` | 操作日志 |
| `remote_device` | 远程访问已授权设备（uuid、AES 密钥、权限） |

## 🔧 构建

前置：Qt 6（≥ 6.5，含 Pdf / PdfWidgets 模块）、CMake ≥ 3.19、MinGW 工具链。

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64
cmake --build build -j2
```

产物：`build/HFADM.exe`（Windows 可执行文件图标经 `icon.rc.in` → windres 嵌入）。

## 📦 发布部署

- **动态部署**：`windeployqt --release --no-translations` 收集 Qt 动态库与插件
- **自包含单文件**：可用 Enigma Virtual Box 等工具将动态库打包进 exe（发布产物 `HFADM_boxed.exe`）

## 🌐 远程访问

- 服务端监听 `0.0.0.0:312`，一次绑定一个打开中的机型
- 客户端仅允许 `192.168.0.0/16` 网段 IPv4
- 首次连接需配对口令（PIN），配对成功后设备密钥持久化；后续连接按设备 uuid 校验
- 会话 AES-128-GCM 加密；15s 心跳，60s 静默自动断开
- 设备管理对话框：查看已授权设备、授权 / 拉黑

## 📁 目录结构

```
HFADM/
├── main.cpp               # 入口（应用图标、中文本地化）
├── mainwindow.*           # 主窗口
├── database/              # 数据库连接与 SQL 仓储
├── service/               # 业务服务 + 远程协议/加密
├── model/                 # 数据结构
├── ui/                    # 界面组件与对话框
├── assets/                # 图标、欢迎页资源
└── HFADM_zh_CN.ts         # 中文本地化翻译源
```

## 📄 说明

- 删除为**物理删除**（节点 + 子级 + 图纸记录 + 磁盘文件），操作前有带统计的确认弹窗，不可恢复
- 本项目为个人学习项目（Qt/C++），架构与协议设计随版本演进
