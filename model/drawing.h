#ifndef DRAWING_H
#define DRAWING_H

#include <QDateTime>
#include <QString>

// 图纸文件及版本（对应数据库 drawing 表）
// 只保存文件索引（文件名/路径/版本），PDF 实体保存在项目 files/ 目录
// 版本为字母（A/B/C...），文件名形如 {完整图号}{版本字母}_{零件名}.pdf
struct Drawing {
    qint64 id = 0;
    qint64 partId = 0;       // 所属零件（part 表 id）
    qint64 partNodeId = 0;   // 所属零件的节点 id（查询辅助，非表字段）
    QString fileName;        // 文件名称
    QString filePath;        // 文件路径（files 目录下）
    QString version;         // 版本字母（A/B/C...）
    bool isCurrent = true;   // 是否为当前版本
    QDateTime createTime;
    bool deleted = false;
};

#endif // DRAWING_H
