#ifndef PART_H
#define PART_H

#include <QString>

// 零件属性（对应数据库 part 表，属性固定，不支持动态属性）
struct Part {
    qint64 id = 0;
    qint64 nodeId = 0;   // 对应 node 表 id（type = Part）
    QString material;    // 材质
    int quantity = 1;    // 装配数量
};

#endif // PART_H
