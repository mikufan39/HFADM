#ifndef XLSXWRITER_H
#define XLSXWRITER_H

#include <QString>
#include <QVector>

// 极简 xlsx 写出器：零第三方依赖，把二维表格写为标准 .xlsx（Excel/WPS 可打开）。
// 内部用 Qt 私有 QZipWriter 打包最小 xlsx 文件集：
//   [Content_Types].xml / _rels/.rels / xl/workbook.xml / xl/_rels/workbook.xml.rels
//   / xl/worksheets/sheet1.xml / xl/styles.xml
// 单元格采用 inlineStr（免 sharedStrings 表）；纯业务解耦，可独立复用。
class XlsxWriter
{
public:
    // 行类型：控制合并与样式。
    //   Title  标题行：合并全列，14 号白字 + 品牌青底（#39C5BB）
    //   Info   信息行：合并全列，10 号浅灰底，左对齐（机型/导出时间/统计等元信息）
    //   Header 表头行：加粗 + 浅青底（#E0F7F4）+ 边框 + 居中
    //   Data   数据行：细边框；文本列左对齐、数字列居中
    //   Total  汇总行：合并全列，加粗 + 浅灰底（#F2F2F2）+ 居中
    enum class RowKind { Data, Header, Title, Info, Total };

    struct Sheet {
        QVector<QVector<QString>> rows;  // 全部行（角色由 rowKinds 决定，首行不必是表头）
        QVector<RowKind> rowKinds;       // 每行类型；长度不足按 Data 补齐
        QVector<int> numericCols;        // 1 起始列号：Data 行写为数字单元格（其余列写文本）
        QVector<double> columnWidths;    // 每列显示宽度；缺失列兜底 12.0
        int freezeRows = 0;              // 冻结前 N 行（>0 时启用冻结窗格）
        bool autoFilter = false;         // 从表头行起启用自动筛选（需存在 Header 行）
    };

    // 成功返回 true，失败置 error。
    static bool write(const QString &filePath, const Sheet &sheet,
                      QString *error = nullptr);
};

#endif // XLSXWRITER_H
