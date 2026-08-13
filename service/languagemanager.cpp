#include "languagemanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace {
// 配置文件键：已保存的语言代码（hfadm.session，与 SessionManager/AppConfig 共用同一文件）
const QString kLanguageKey = QStringLiteral("language");
}

const QString LanguageManager::kZhCN = QStringLiteral("zh_CN");
const QString LanguageManager::kEn = QStringLiteral("en");

LanguageManager *LanguageManager::instance()
{
    static LanguageManager s_instance;
    return &s_instance;
}

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
{
    loadInitial();
}

LanguageManager::~LanguageManager()
{
    // 若在 QApplication 存活期间析构（正常退出时静态单例晚于 qApp，此处判空保护）
    if (m_translator) {
        if (QCoreApplication::instance()) {
            QCoreApplication::instance()->removeTranslator(m_translator);
        }
        delete m_translator;
        m_translator = nullptr;
    }
}

QString LanguageManager::configFilePath()
{
    // 与 SessionManager::sessionFilePath 保持同一配置文件（程序同目录 hfadm.session，
    // QSettings IniFormat；各组件只读写自己维护的键，互不冲突）
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("hfadm.session"));
}

void LanguageManager::loadInitial()
{
    // 1) 优先恢复用户已保存的语言选择（写进配置文件 hfadm.session）
    QSettings settings(configFilePath(), QSettings::IniFormat);
    const QString saved = settings.value(kLanguageKey).toString();
    if (!saved.isEmpty() && install(saved)) {
        m_language = saved;
        return;
    }
    // 2) 无保存或保存的翻译不可用：按系统语言推断（仅区分中文/其他，其他统一走英文）
    const QString systemName = QLocale::system().name(); // 如 zh_CN / en_US / ja_JP
    const QString fallback = systemName.startsWith(QLatin1String("zh"))
        ? kZhCN
        : kEn;
    if (install(fallback)) {
        m_language = fallback;
    }
    // 连兜底语言都不可用（.qm 未嵌入）时保持默认 zh_CN，界面显示源文本（中文）
}

bool LanguageManager::install(const QString &language)
{
    auto *translator = new QTranslator;
    const QString qmPath = QStringLiteral(":/i18n/HFADM_%1.qm").arg(language);
    if (!translator->load(qmPath)) {
        delete translator;
        return false;
    }
    // 注意：installTranslator 是"添加"语义，多个 translator 会叠加查找；
    // 切换语言必须先卸载旧的，否则旧翻译会继续命中
    if (m_translator) {
        QCoreApplication::instance()->removeTranslator(m_translator);
        delete m_translator;
    }
    m_translator = translator;
    // 安装新 translator 会向所有顶层窗口发送 LanguageChange 事件，触发界面重翻译
    QCoreApplication::instance()->installTranslator(m_translator);
    return true;
}

bool LanguageManager::switchTo(const QString &language)
{
    if (language == m_language) {
        return true; // 已是目标语言
    }
    if (!install(language)) {
        return false; // 翻译资源不可用：保留当前语言
    }
    m_language = language;
    // 语言选择持久化到配置文件，下次启动恢复
    QSettings(configFilePath(), QSettings::IniFormat).setValue(kLanguageKey, language);
    emit languageChanged(language);
    return true;
}

QString LanguageManager::menuTitle() const
{
    // 元规则：只有中文显示"语言"，其余语言一律显示"Language"
    return m_language == kZhCN
        ? QStringLiteral("语言")
        : QStringLiteral("Language");
}

QString LanguageManager::displayName(const QString &language)
{
    // 语言名用其本地写法，与界面语言无关（后续新增语言在此补充）
    if (language == kZhCN) {
        return QStringLiteral("简体中文");
    }
    if (language == kEn) {
        return QStringLiteral("English");
    }
    return language; // 未知语言：暂以代码兜底
}
