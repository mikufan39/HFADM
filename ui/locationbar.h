#ifndef LOCATIONBAR_H
#define LOCATIONBAR_H

#include "model/hfdadnode.h"

#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedLayout;
class QToolButton;

// 浏览器式地址栏：显示态=路径面包屑（每段可点击跳转），编辑态=输入框（搜索 / 输入路径跳转）。
//   显示态：面包屑展示从机型根到当前目录的完整链，点击任意段跳转；点击右侧空白进入编辑态；
//           搜索中时在面包屑右侧显示「搜索: xxx」标签与清除按钮。
//   编辑态：进入时填入当前路径并全选（浏览器式，直接输入即覆盖）；
//           输入含分隔符（/ › >）视为路径输入（回车跳转，placeholder 同步提示），
//           否则为关键词搜索（实时生效）；Esc / 失焦退回显示态（搜索词保留，路径输入丢弃；
//           未修改的自动填入路径不会当作搜索词）。
// 主窗口通过信号对接：segmentClicked / searchTextChanged / pathSubmitRequested / clearSearchRequested。
class LocationBar : public QWidget
{
    Q_OBJECT

public:
    explicit LocationBar(QWidget *parent = nullptr);

    // 显示态：设置从机型根到当前目录的完整链（含两端）；链为空表示无可显示路径（清空面包屑）
    void setPath(const QVector<HFADMNode> &chain);
    // 搜索中：面包屑右侧显示「搜索: xxx」标签与清除按钮（keyword 为空则隐藏）
    void setSearchKeyword(const QString &keyword);
    // 无目录上下文（PDF 标签等）：显示占位文本、禁止编辑
    void setPdfMode(bool pdf);
    // 整体可用性（项目未打开时禁用，含面包屑与编辑态）；QWidget::setEnabled 非虚，此处为隐藏实现
    void setEnabled(bool enabled);

    // 进入编辑态：填入当前路径文本并全选，聚焦输入框（Ctrl+F / Ctrl+L / 点击空白）
    void focusForEditing();
    bool isEditing() const;
    // 当前搜索词：编辑态取输入框内容，显示态取搜索标签内容（均去首尾空白）
    QString searchText() const;
    bool isSearching() const;
    // 清除搜索词并回显示态（主窗口随之刷新目录）
    void clearSearch();
    // 路径跳转成功后调用：退出编辑态并刷新面包屑（保留搜索词不变）
    void finishPathJump();

signals:
    // 面包屑段被点击：请求跳转到该目录
    void segmentClicked(qint64 nodeId);
    // 编辑态输入变化（非路径内容）→ 主窗口实时刷新目录
    void searchTextChanged(const QString &text);
    // 编辑态回车且内容为路径 → 主窗口解析并跳转（text 为输入原文）
    void pathSubmitRequested(const QString &text);
    // 清除按钮被点击 / clearSearch() 外部调用 → 主窗口清搜索并刷新
    void clearSearchRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    // 语言切换：重译 PDF 占位、清除按钮提示、输入框 placeholder 与搜索标签
    void changeEvent(QEvent *event) override;

private:
    void enterEditMode();
    void leaveEditMode();
    void rebuildBreadcrumb();
    void rebuildPathText();
    void updateSearchLabel();
    void onEditTextChanged(const QString &text);
    void onEditReturnPressed();
    // 输入文本是否被识别为路径（含 / › > 分隔符）
    static bool looksLikePath(const QString &text);

    QStackedLayout *m_stack = nullptr;
    // 显示态
    QScrollArea *m_displayArea = nullptr;
    QWidget *m_displayHost = nullptr;
    QHBoxLayout *m_breadcrumbLayout = nullptr;
    QWidget *m_spacer = nullptr;        // 面包屑右侧空白占位（点击进入编辑态）
    QLabel *m_searchLabel = nullptr;    // 「搜索: xxx」
    QToolButton *m_clearButton = nullptr;
    QLabel *m_pdfLabel = nullptr;       // PDF 占位文本
    // 编辑态
    QLineEdit *m_edit = nullptr;

    QVector<HFADMNode> m_chain;         // 显示态路径链（数据源）
    QString m_searchKeyword;            // 当前搜索词
    QString m_pathText;                 // 编辑态初始路径文本（重建面包屑时刷新）
    QString m_editInitialText;          // 最近一次进入编辑态时输入框的文本（失焦判断是否修改过）
    bool m_editing = false;
    bool m_pdfMode = false;
    bool m_enabled = true;
};

#endif // LOCATIONBAR_H
