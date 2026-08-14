#include "locationbar.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QScrollArea>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>

namespace {

// 面包屑段按钮（链接式）：非当前段
const char *kCrumbButtonStyle = R"(
QToolButton#crumbButton {
    border: none;
    background: transparent;
    color: #1a73e8;
    padding: 2px 5px;
    border-radius: 4px;
}
QToolButton#crumbButton:hover { background: rgba(0, 0, 0, 0.08); }
QToolButton#crumbButton:disabled { color: #9a9a9a; }
)";

// 当前段（路径末级）：主题文字色加粗，点击仍可进入编辑态
const char *kCrumbCurrentStyle = R"(
QToolButton#crumbCurrent {
    border: none;
    background: transparent;
    color: #2c2c2c;
    font-weight: 500;
    padding: 2px 5px;
    border-radius: 4px;
}
QToolButton#crumbCurrent:hover { background: rgba(0, 0, 0, 0.08); }
QToolButton#crumbCurrent:disabled { color: #9a9a9a; }
)";

} // namespace

LocationBar::LocationBar(QWidget *parent)
    : QWidget(parent)
{
    m_stack = new QStackedLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);

    // ---- 显示态：面包屑（可点击段 + 搜索标签 + 右侧空白区）----
    m_displayArea = new QScrollArea(this);
    m_displayArea->setWidgetResizable(true);
    m_displayArea->setFrameShape(QFrame::NoFrame);
    m_displayArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_displayArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_displayArea->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
    m_displayArea->viewport()->setAutoFillBackground(false);

    m_displayHost = new QWidget;
    m_displayHost->setStyleSheet(QStringLiteral("QWidget { background: transparent; }"));
    m_breadcrumbLayout = new QHBoxLayout(m_displayHost);
    m_breadcrumbLayout->setContentsMargins(2, 0, 2, 0);
    m_breadcrumbLayout->setSpacing(1);

    m_searchLabel = new QLabel(m_displayHost);
    m_searchLabel->hide();

    m_clearButton = new QToolButton(m_displayHost);
    m_clearButton->setText(QStringLiteral("✕")); // 符号，不参与翻译
    m_clearButton->setCursor(Qt::PointingHandCursor);
    m_clearButton->setAutoRaise(true);
    m_clearButton->setToolTip(tr("清除搜索"));
    m_clearButton->hide();
    connect(m_clearButton, &QToolButton::clicked, this, [this] {
        clearSearch();
    });

    m_pdfLabel = new QLabel(tr("图纸"), m_displayHost);
    m_pdfLabel->setStyleSheet(QStringLiteral("color: #9a9a9a; padding: 2px 5px;"));
    m_pdfLabel->hide();

    m_spacer = new QWidget(m_displayHost);
    m_spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_breadcrumbLayout->addWidget(m_pdfLabel);
    m_breadcrumbLayout->addStretch(1); // 面包屑段先占位，rebuildBreadcrumb 时插入段按钮
    m_breadcrumbLayout->addWidget(m_searchLabel);
    m_breadcrumbLayout->addWidget(m_clearButton);
    m_breadcrumbLayout->addWidget(m_spacer);

    m_displayArea->setWidget(m_displayHost);
    m_stack->addWidget(m_displayArea);

    // ---- 编辑态：输入框 ----
    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(tr("搜索当前目录（按名称）"));
    m_edit->setAcceptDrops(false); // 避免 PDF 拖到输入框被当作文本，统一交给主窗口导入
    m_edit->setClearButtonEnabled(true);
    m_stack->addWidget(m_edit);
    m_stack->setCurrentWidget(m_displayArea);

    // 输入框信号：文本变化（模式切换/实时搜索）、回车（路径跳转/确认搜索）
    connect(m_edit, &QLineEdit::textChanged, this, &LocationBar::onEditTextChanged);
    connect(m_edit, &QLineEdit::returnPressed, this, &LocationBar::onEditReturnPressed);

    // 输入框事件（文本变化 / 回车 / Esc / 失焦）统一走 eventFilter
    m_edit->installEventFilter(this);
    // 面包屑空白区点击进入编辑态
    m_displayArea->viewport()->installEventFilter(this);
    m_spacer->installEventFilter(this);
    setStyleSheet(QString::fromLatin1(kCrumbButtonStyle) + QString::fromLatin1(kCrumbCurrentStyle));
}

void LocationBar::setPath(const QVector<HFADMNode> &chain)
{
    m_chain = chain;
    rebuildPathText();
    if (!m_editing) {
        rebuildBreadcrumb();
    }
}

void LocationBar::setSearchKeyword(const QString &keyword)
{
    m_searchKeyword = keyword.trimmed();
    updateSearchLabel();
}

void LocationBar::setPdfMode(bool pdf)
{
    m_pdfMode = pdf;
    if (pdf && m_editing) {
        leaveEditMode();
    }
    rebuildBreadcrumb();
}

void LocationBar::setEnabled(bool enabled)
{
    m_enabled = enabled;
    QWidget::setEnabled(enabled);
    if (!enabled && m_editing) {
        leaveEditMode();
    }
    updateSearchLabel();
}

void LocationBar::focusForEditing()
{
    if (!m_enabled || m_pdfMode) {
        return;
    }
    enterEditMode();
}

bool LocationBar::isEditing() const
{
    return m_editing;
}

QString LocationBar::searchText() const
{
    return m_editing ? m_edit->text().trimmed() : m_searchKeyword;
}

bool LocationBar::isSearching() const
{
    return !searchText().isEmpty();
}

void LocationBar::clearSearch()
{
    // 无论编辑态/显示态：丢弃输入框内容与搜索词，强制回显示态（主窗口随之刷新目录）
    if (m_editing) {
        m_editing = false;
        m_stack->setCurrentWidget(m_displayArea);
    }
    const bool hadKeyword = !m_searchKeyword.isEmpty();
    m_searchKeyword.clear();
    updateSearchLabel();
    rebuildBreadcrumb();
    if (hadKeyword) {
        emit clearSearchRequested();
    }
}

void LocationBar::finishPathJump()
{
    if (!m_editing) {
        return;
    }
    m_editing = false;
    m_stack->setCurrentWidget(m_displayArea);
    rebuildBreadcrumb();
}

bool LocationBar::eventFilter(QObject *watched, QEvent *event)
{
    // 编辑态输入框：Esc 退出；失焦退出（保留搜索词、丢弃路径输入）
    if (watched == m_edit) {
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape) {
                leaveEditMode();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            if (m_editing) {
                leaveEditMode();
            }
        }
        return QWidget::eventFilter(watched, event);
    }
    // 显示态空白区（面包屑右侧）点击进入编辑态
    if (m_enabled && !m_pdfMode && !m_editing
        && (watched == m_spacer || watched == m_displayArea->viewport())
        && event->type() == QEvent::MouseButtonPress) {
        enterEditMode();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void LocationBar::changeEvent(QEvent *event)
{
    // 语言切换：QApplication::installTranslator 后自动收到 LanguageChange，重译常驻文本
    if (event->type() == QEvent::LanguageChange) {
        m_pdfLabel->setText(tr("图纸"));
        m_clearButton->setToolTip(tr("清除搜索"));
        // placeholder 按当前内容模式重译；面包屑/搜索标签由 rebuildBreadcrumb 一并刷新
        if (m_editing) {
            m_edit->setPlaceholderText(looksLikePath(m_edit->text())
                ? tr("输入完整路径后回车跳转（如 机型A/部件1）")
                : tr("搜索当前目录（按名称）"));
        } else {
            m_edit->setPlaceholderText(tr("搜索当前目录（按名称）"));
        }
        rebuildBreadcrumb();
    }
    QWidget::changeEvent(event);
}

void LocationBar::enterEditMode()
{
    if (m_editing) {
        m_edit->selectAll();
        m_edit->setFocus();
        return;
    }
    m_editing = true;
    // 初始填充当前路径：blockSignals 避免把路径文本当作搜索词触发 onEditTextChanged
    m_editInitialText = m_pathText;
    m_edit->blockSignals(true);
    m_edit->setText(m_pathText);
    m_edit->blockSignals(false);
    m_edit->selectAll();
    // placeholder 按内容模式手动设置（与 onEditTextChanged 保持一致）
    m_edit->setPlaceholderText(looksLikePath(m_pathText)
        ? tr("输入完整路径后回车跳转（如 机型A/部件1）")
        : tr("搜索当前目录（按名称）"));
    m_stack->setCurrentWidget(m_edit);
    m_edit->setFocus();
}

void LocationBar::leaveEditMode()
{
    if (!m_editing) {
        return;
    }
    m_editing = false;
    const QString text = m_edit->text().trimmed();
    // 失焦/回车保留搜索词，但仅限用户实际输入的内容：
    // 进入编辑态会自动填入当前路径并全选，若未修改（text == m_editInitialText）
    // 直接离开，路径文本不得被当作搜索词——否则根目录（单段路径无分隔符）会误触发
    // 递归搜索并显示「搜索: xxx」标签，导致卡顿与列表被搜索结果覆盖
    if (!text.isEmpty() && !looksLikePath(text) && text != m_editInitialText) {
        // 搜索词保留（回车确认或失焦均保留，与资源管理器一致）
        m_searchKeyword = text;
    }
    m_stack->setCurrentWidget(m_displayArea);
    rebuildBreadcrumb();
}

void LocationBar::rebuildBreadcrumb()
{
    // 清空旧段按钮（保留 stretch / 搜索标签 / 清除按钮 / spacer 等常驻项）
    while (m_breadcrumbLayout->count() > 0) {
        QLayoutItem *item = m_breadcrumbLayout->takeAt(0);
        if (QWidget *w = item->widget()) {
            // 常驻控件不删除，仅移除并放回（stretch 项为 null widget）
            if (w == m_pdfLabel || w == m_searchLabel || w == m_clearButton || w == m_spacer) {
                w->setParent(nullptr);
                delete item;
                continue;
            }
            delete w;
        }
        delete item;
    }

    if (m_pdfMode) {
        m_pdfLabel->show();
        m_breadcrumbLayout->addWidget(m_pdfLabel);
        m_breadcrumbLayout->addStretch(1);
        m_breadcrumbLayout->addWidget(m_spacer);
        return;
    }

    m_pdfLabel->hide();
    const int n = m_chain.size();
    QToolButton *lastButton = nullptr;
    for (int i = 0; i < n; ++i) {
        const HFADMNode &node = m_chain.at(i);
        const bool current = (i == n - 1);
        auto *btn = new QToolButton(m_displayHost);
        btn->setObjectName(current ? QStringLiteral("crumbCurrent") : QStringLiteral("crumbButton"));
        btn->setText(node.name);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setAutoRaise(true);
        btn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);
        btn->setToolTip(current ? tr("点击编辑路径或搜索")
                                : tr("跳转到 %1").arg(node.name));
        const qint64 id = node.id;
        connect(btn, &QToolButton::clicked, this, [this, id, current] {
            if (current) {
                enterEditMode(); // 当前段点击=进入编辑态（浏览器地址栏习惯）
            } else {
                emit segmentClicked(id);
            }
        });
        m_breadcrumbLayout->addWidget(btn);
        lastButton = btn;
        if (!current) {
            auto *sep = new QLabel(QStringLiteral("›"), m_displayHost); // 分隔符符号，不参与翻译（looksLikePath 依赖它）
            sep->setStyleSheet(QStringLiteral("color: #9a9a9a; padding: 0 1px;"));
            sep->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);
            m_breadcrumbLayout->addWidget(sep);
        }
    }
    m_breadcrumbLayout->addStretch(1);
    m_breadcrumbLayout->addWidget(m_searchLabel);
    m_breadcrumbLayout->addWidget(m_clearButton);
    m_breadcrumbLayout->addWidget(m_spacer);
    updateSearchLabel();

    // 滚动到末尾（当前段可见）
    if (lastButton) {
        QPointer<QToolButton> guard(lastButton);
        QTimer::singleShot(0, this, [this, guard] {
            if (guard) {
                m_displayArea->ensureWidgetVisible(guard, 8, 8);
            }
        });
    }
}

void LocationBar::rebuildPathText()
{
    QStringList names;
    for (const HFADMNode &node : m_chain) {
        names.append(node.name);
    }
    m_pathText = names.join(QStringLiteral("/"));
}

void LocationBar::updateSearchLabel()
{
    if (m_searchKeyword.isEmpty()) {
        m_searchLabel->hide();
        m_clearButton->hide();
    } else {
        m_searchLabel->setText(tr("搜索: %1").arg(m_searchKeyword));
        m_searchLabel->setStyleSheet(QStringLiteral("color: #d97706; padding: 2px 4px;"));
        m_searchLabel->show();
        m_clearButton->show();
    }
}

void LocationBar::onEditTextChanged(const QString &text)
{
    const bool path = looksLikePath(text);
    m_edit->setPlaceholderText(path
        ? tr("输入完整路径后回车跳转（如 机型A/部件1）")
        : tr("搜索当前目录（按名称）"));
    if (!path) {
        emit searchTextChanged(text);
    }
}

void LocationBar::onEditReturnPressed()
{
    const QString text = m_edit->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    if (looksLikePath(text)) {
        emit pathSubmitRequested(text);
        return; // 跳转成功/失败由主窗口决定是否退出编辑态
    }
    // 关键词搜索已实时生效，回车=确认并退回显示态
    leaveEditMode();
}

bool LocationBar::looksLikePath(const QString &text)
{
    return text.contains(QLatin1Char('/'))
        || text.contains(QStringLiteral("›")) // 分隔符符号固定，不参与翻译
        || text.contains(QLatin1Char('>'));
}
