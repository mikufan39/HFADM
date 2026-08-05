#ifndef COMPONENT_H
#define COMPONENT_H

#include <QString>

// 部件属性（对应数据库 component 表，与零件 part 表模式一致）
struct Component {
    qint64 id = 0;
    qint64 nodeId = 0;   // 对应 node 表 id（type = Component）
    int quantity = 1;    // 装配数量
};

#endif // COMPONENT_H
