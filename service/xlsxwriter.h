#ifndef XLSXWRITER_H
#define XLSXWRITER_H

#include <QString>
#include <QVector>

// 极简 xlsx 写出器：零第三方依赖，把二维表格写为标准 .xlsx（Excel/WPS 可打开）。
// 内部用 Qt 私有 QZipWriter 打包最小 xlsx 文件集：
//   [Content_Types].xml / _rels/.rels / xl/workbook.xml / xl/_rels/workbook.xml.rels
//   / xl/worksheets/sheet1.xml / xl/styles.xml（表头加粗）
// 单元格采用 inlineStr（免 sharedStrings 表）；纯业务解耦，可独立复用。
class XlsxWriter
{
public:
    // rows：二维表格（首行为表头）；numericCols：按 1 起始列号指定写为数字单元格
    // （其余列写文本；空文本写空单元格）。成功返回 true，失败置 error。
    static bool write(const QString &filePath,
                      const QVector<QVector<QString>> &rows,
                      const QVector<int> &numericCols,
                      QString *error = nullptr);
};

#endif // XLSXWRITER_H
