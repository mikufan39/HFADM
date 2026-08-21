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

// 合并行的样式：Title=2、Info=3、Total=6（见 stylesXml 的 cellXfs 索引）
int mergedRowStyle(XlsxWriter::RowKind kind)
{
    switch (kind) {
    case XlsxWriter::RowKind::Title:  return 2;
    case XlsxWriter::RowKind::Info:   return 3;
    case XlsxWriter::RowKind::Total:  return 6;
    default:                          return 0;
    }
}

bool isMergedRow(XlsxWriter::RowKind kind)
{
    return kind == XlsxWriter::RowKind::Title
        || kind == XlsxWriter::RowKind::Info
        || kind == XlsxWriter::RowKind::Total;
}

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

// 样式表：字体 4 个（常规/加粗/标题白字/信息小字）、填充 5 个（none/gray125 + 表头浅青/标题品牌青/汇总浅灰）、
// 边框 2 个（无/全细框）、cellXfs 7 个：
//   s0 常规兜底          s1 表头（加粗+浅青底+边框+居中）
//   s2 标题（14号白字+品牌青底+边框+居中）  s3 信息（10号+浅灰底+左对齐）
//   s4 数据文本（边框+左对齐）  s5 数据数字（边框+居中）  s6 汇总（加粗+浅灰底+边框+居中）
QString stylesXml()
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"4\">"
        "<font><sz val=\"11\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"14\"/><color rgb=\"FFFFFFFF\"/><name val=\"Calibri\"/></font>"
        "<font><sz val=\"10\"/><name val=\"Calibri\"/></font>"
        "</fonts>"
        "<fills count=\"5\">"
        "<fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFE0F7F4\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF39C5BB\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFF2F2F2\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "</fills>"
        "<borders count=\"2\">"
        "<border><left/><right/><top/><bottom/><diagonal/></border>"
        "<border><left style=\"thin\"/><right style=\"thin\"/><top style=\"thin\"/><bottom style=\"thin\"/><diagonal/></border>"
        "</borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"7\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"3\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"3\" fillId=\"4\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyAlignment=\"1\"><alignment horizontal=\"left\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"left\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"4\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "</cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "</styleSheet>");
}

QString sheetXml(const XlsxWriter::Sheet &sheet)
{
    // 列数取所有行中的最大宽度（标题/信息/汇总行通常只有 1 列，表头/数据行才是全宽）
    int colCount = 1;
    for (const auto &row : sheet.rows) {
        colCount = qMax(colCount, row.size());
    }

    // 行类型补齐（缺省视为 Data）
    QVector<XlsxWriter::RowKind> kinds = sheet.rowKinds;
    kinds.resize(sheet.rows.size());

    // 表头行号与最后一个数据行号（1 起始），供自动筛选 ref 定位
    int headerRow = -1;
    int lastDataRow = -1;
    for (int r = 0; r < kinds.size(); ++r) {
        if (kinds.at(r) == XlsxWriter::RowKind::Header && headerRow < 0) {
            headerRow = r + 1;
        }
        if (kinds.at(r) == XlsxWriter::RowKind::Data) {
            lastDataRow = r + 1;
        }
    }

    QString sheetOut;
    sheetOut += QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");

    // 冻结窗格：固定前 freezeRows 行（标题/信息/表头），滚动时表头常驻
    if (sheet.freezeRows > 0) {
        sheetOut += QStringLiteral(
            "<sheetViews><sheetView tabSelected=\"1\" workbookViewId=\"0\">"
            "<pane ySplit=\"%1\" topLeftCell=\"A%2\" activePane=\"bottomLeft\" state=\"frozen\"/>"
            "<selection pane=\"bottomLeft\" activeCell=\"A%2\" sqref=\"A%2\"/>"
            "</sheetView></sheetViews>")
                        .arg(sheet.freezeRows)
                        .arg(sheet.freezeRows + 1);
    } else {
        sheetOut += QStringLiteral(
            "<sheetViews><sheetView tabSelected=\"1\" workbookViewId=\"0\"/></sheetViews>");
    }

    // 列宽
    sheetOut += QStringLiteral("<cols>");
    for (int c = 0; c < colCount; ++c) {
        double width = 12.0;
        if (c < sheet.columnWidths.size() && sheet.columnWidths.at(c) > 0.0) {
            width = sheet.columnWidths.at(c);
        }
        sheetOut += QStringLiteral("<col min=\"%1\" max=\"%1\" width=\"%2\" customWidth=\"1\"/>")
                        .arg(c + 1)
                        .arg(QString::number(width, 'f', 1));
    }
    sheetOut += QStringLiteral("</cols>");

    // 数据区
    sheetOut += QStringLiteral("<sheetData>");
    for (int r = 0; r < sheet.rows.size(); ++r) {
        const XlsxWriter::RowKind kind = kinds.at(r);
        sheetOut += QStringLiteral("<row r=\"%1\">").arg(r + 1);

        if (isMergedRow(kind)) {
            // 合并行（标题/信息/汇总）：仅输出首格，合并区域由 mergeCells 定义
            const QString text = sheet.rows.at(r).value(0);
            const int s = mergedRowStyle(kind);
            if (text.isEmpty()) {
                sheetOut += QStringLiteral("<c r=\"A%1\" s=\"%2\"/>").arg(r + 1).arg(s);
            } else {
                sheetOut += QStringLiteral("<c r=\"A%1\" s=\"%2\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%3</t></is></c>")
                                .arg(r + 1).arg(s).arg(xmlEscape(text));
            }
        } else {
            for (int c = 0; c < sheet.rows.at(r).size(); ++c) {
                const QString ref = columnLetter(c + 1) + QString::number(r + 1);
                const QString text = sheet.rows.at(r).at(c);
                if (kind == XlsxWriter::RowKind::Header) {
                    // 表头：加粗 + 底纹 + 边框 + 居中（s=1）
                    sheetOut += QStringLiteral("<c r=\"%1\" s=\"1\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%2</t></is></c>")
                                    .arg(ref, xmlEscape(text));
                } else if (text.isEmpty()) {
                    // 空单元格也带边框（s=4）
                    sheetOut += QStringLiteral("<c r=\"%1\" s=\"4\"/>").arg(ref);
                } else if (sheet.numericCols.contains(c + 1)) {
                    // 数字单元格（序号/层级/数量/张数等，居中 s=5）
                    sheetOut += QStringLiteral("<c r=\"%1\" s=\"5\" t=\"n\"><v>%2</v></c>").arg(ref, text);
                } else {
                    sheetOut += QStringLiteral("<c r=\"%1\" s=\"4\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%2</t></is></c>")
                                    .arg(ref, xmlEscape(text));
                }
            }
        }
        sheetOut += QStringLiteral("</row>");
    }
    sheetOut += QStringLiteral("</sheetData>");

    // 自动筛选：表头行起，覆盖到最后一个数据行（无数据行则仅表头行）
    if (sheet.autoFilter && headerRow > 0) {
        const int lastRow = lastDataRow > 0 ? lastDataRow : headerRow;
        sheetOut += QStringLiteral("<autoFilter ref=\"A%1:%2%3\"/>")
                        .arg(headerRow)
                        .arg(columnLetter(colCount))
                        .arg(lastRow);
    }

    // 合并单元格（标题/信息/汇总行各合并全列）
    // 注意：ref 中行号用独立占位符，避免与列字母占位符粘连（如 %2%1 会被误解析为 %21）
    int mergeCount = 0;
    QString merges;
    for (int r = 0; r < kinds.size(); ++r) {
        if (isMergedRow(kinds.at(r))) {
            merges += QStringLiteral("<mergeCell ref=\"A%1:%2%3\"/>")
                          .arg(r + 1)
                          .arg(columnLetter(colCount))
                          .arg(r + 1);
            ++mergeCount;
        }
    }
    if (mergeCount > 0) {
        sheetOut += QStringLiteral("<mergeCells count=\"%1\">%2</mergeCells>").arg(mergeCount).arg(merges);
    }

    sheetOut += QStringLiteral("</worksheet>");
    return sheetOut;
}

} // namespace

bool XlsxWriter::write(const QString &filePath, const Sheet &sheet, QString *error)
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
    zip.addFile(QStringLiteral("xl/worksheets/sheet1.xml"), sheetXml(sheet).toUtf8());
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
