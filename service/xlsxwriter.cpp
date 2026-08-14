#include "xlsxwriter.h"

#include <QFile>
#include <QtCore/private/qzipwriter_p.h>

namespace {

// XML 转义：& < > "；剥离非法控制字符（<0x20 且非 \t\r\n，Excel 无法保存）
QString xmlEscape(const QString &input)
{
    QString out;
    out.reserve(input.size());
    for (const QChar ch : input) {
        const ushort u = ch.unicode();
        if (u < 0x20 && u != '\t' && u != '\n' && u != '\r') {
            continue;
        }
        switch (u) {
        case '&':  out += QStringLiteral("&amp;");  break;
        case '<':  out += QStringLiteral("&lt;");   break;
        case '>':  out += QStringLiteral("&gt;");   break;
        case '"':  out += QStringLiteral("&quot;"); break;
        default:   out += ch;                       break;
        }
    }
    return out;
}

// 1 起始列号 → Excel 列字母（1→A，26→Z，27→AA）
QString columnLetter(int col)
{
    QString s;
    while (col > 0) {
        const int m = (col - 1) % 26;
        s.prepend(QChar(static_cast<ushort>(u'A' + m)));
        col = (col - 1) / 26;
    }
    return s;
}

// 每列显示宽度（与 BOM 列顺序对应，仅影响显示观感）
const double kColumnWidths[] = { 6.0, 24.0, 20.0, 8.0, 7.0, 14.0, 22.0, 11.0, 11.0 };
constexpr int kMaxColumnWidths = static_cast<int>(sizeof(kColumnWidths) / sizeof(kColumnWidths[0]));

// ---- 各 xlsx 组成部分（最小可用集合，XML 均为固定模板）----

QString contentTypesXml()
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
        "</Types>");
}

QString rootRelsXml()
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>");
}

QString workbookXml()
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"BOM\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
        "</workbook>");
}

QString workbookRelsXml()
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        "</Relationships>");
}

// 最小样式表：字体 2 个（常规/加粗）、填充 2 个（none/gray125，xlsx 规范要求）、
// cellXfs 2 个（s=0 常规、s=1 表头加粗）
QString stylesXml()
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
        "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill></fills>"
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"2\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
        "</cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "</styleSheet>");
}

QString sheetXml(const QVector<QVector<QString>> &rows, const QVector<int> &numericCols)
{
    QString sheet;
    sheet += QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");
    // 列宽（仅前 kMaxColumnWidths 列有效）
    sheet += QStringLiteral("<cols>");
    const int colCount = rows.isEmpty() ? 0 : rows.first().size();
    for (int c = 0; c < colCount; ++c) {
        const double width = c < kMaxColumnWidths ? kColumnWidths[c] : 12.0;
        sheet += QStringLiteral("<col min=\"%1\" max=\"%1\" width=\"%2\" customWidth=\"1\"/>")
                     .arg(c + 1)
                     .arg(QString::number(width, 'f', 1));
    }
    sheet += QStringLiteral("</cols>");

    sheet += QStringLiteral("<sheetData>");
    for (int r = 0; r < rows.size(); ++r) {
        const int rowNum = r + 1;
        sheet += QStringLiteral("<row r=\"%1\">").arg(rowNum);
        for (int c = 0; c < rows.at(r).size(); ++c) {
            const QString ref = columnLetter(c + 1) + QString::number(rowNum);
            const QString text = rows.at(r).at(c);
            if (r == 0) {
                // 表头：加粗（s=1）
                sheet += QStringLiteral("<c r=\"%1\" t=\"inlineStr\" s=\"1\"><is><t xml:space=\"preserve\">%2</t></is></c>")
                             .arg(ref, xmlEscape(text));
            } else if (text.isEmpty()) {
                sheet += QStringLiteral("<c r=\"%1\"/>").arg(ref);
            } else if (numericCols.contains(c + 1)) {
                // 数字单元格（层级/数量/张数等）
                sheet += QStringLiteral("<c r=\"%1\" t=\"n\"><v>%2</v></c>").arg(ref, text);
            } else {
                sheet += QStringLiteral("<c r=\"%1\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%2</t></is></c>")
                             .arg(ref, xmlEscape(text));
            }
        }
        sheet += QStringLiteral("</row>");
    }
    sheet += QStringLiteral("</sheetData></worksheet>");
    return sheet;
}

} // namespace

bool XlsxWriter::write(const QString &filePath,
                       const QVector<QVector<QString>> &rows,
                       const QVector<int> &numericCols,
                       QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    QZipWriter zip(&file);
    zip.setCompressionPolicy(QZipWriter::AlwaysCompress);
    zip.addFile(QStringLiteral("[Content_Types].xml"), contentTypesXml().toUtf8());
    zip.addFile(QStringLiteral("_rels/.rels"), rootRelsXml().toUtf8());
    zip.addFile(QStringLiteral("xl/workbook.xml"), workbookXml().toUtf8());
    zip.addFile(QStringLiteral("xl/_rels/workbook.xml.rels"), workbookRelsXml().toUtf8());
    zip.addFile(QStringLiteral("xl/worksheets/sheet1.xml"), sheetXml(rows, numericCols).toUtf8());
    zip.addFile(QStringLiteral("xl/styles.xml"), stylesXml().toUtf8());
    zip.close();

    if (zip.status() != QZipWriter::NoError) {
        if (error) {
            *error = QStringLiteral("写入 Excel 文件失败（zip 打包错误）");
        }
        return false;
    }
    return true;
}
