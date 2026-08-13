#ifndef LANGUAGEMANAGER_H
#define LANGUAGEMANAGER_H

#include <QObject>
#include <QString>

class QTranslator;

// 应用语言管理：负责加载/切换 Qt 原生翻译（QTranslator + .qm，资源路径 :/i18n/），
// 并把用户选择持久化到配置文件（程序同目录 hfadm.session，QSettings IniFormat），
// 程序重启后保持上次选择的语言。
//
// 语言代码约定（与 .qm 文件名 HFADM_<code>.qm 对应）：
//   zh_CN 简体中文
//   en    English
// 后续新增语言只需：① 增加 .ts 翻译文件并加入 CMake；② 在此补充 displayName。
//
// 切换语言流程：switchTo() → 卸载旧 translator 并安装新 translator。
// QCoreApplication::installTranslator 会自动向所有顶层窗口发送 LanguageChange 事件，
// 各窗口在 changeEvent 中执行 retranslateUi / 手动重翻译即可，无需手动逐控件刷新。
class LanguageManager : public QObject
{
    Q_OBJECT

public:
    // 支持的语种代码
    static const QString kZhCN;   // 简体中文
    static const QString kEn;     // English

    // 应用级单例（main 中在 MainWindow 构造前实例化，保证首窗口即按已保存语言显示）
    static LanguageManager *instance();

    // 当前生效的语言代码（zh_CN / en ...）
    QString currentLanguage() const { return m_language; }

    // 切换到指定语言：成功加载并安装对应 .qm 后持久化、发 languageChanged，返回 true；
    // .qm 缺失或加载失败时保持当前语言不变并返回 false
    bool switchTo(const QString &language);

    // 语言菜单标题（元界面规则，不参与翻译）：
    // 仅当前语言为中文时显示"语言"，其余语言一律显示"Language"（为后续更多语言预留）
    QString menuTitle() const;

    // 语言的显示名（用各语言自身的本地写法，菜单项固定显示，不随界面语言变化）
    static QString displayName(const QString &language);

signals:
    void languageChanged(const QString &language);

private:
    explicit LanguageManager(QObject *parent = nullptr);
    ~LanguageManager() override;

    // 启动初始化：优先恢复已保存选择；无保存时按系统语言推断（zh* 开头 → zh_CN，其余 → en）
    void loadInitial();
    // 配置文件路径：程序同目录 hfadm.session（与 SessionManager 保持一致）
    QString configFilePath();
    // 安装指定语言的 translator（先卸载旧的再装新的）；成功返回 true
    bool install(const QString &language);

    QString m_language = kZhCN;
    QTranslator *m_translator = nullptr;
};

#endif // LANGUAGEMANAGER_H
