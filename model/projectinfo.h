#ifndef PROJECTINFO_H
#define PROJECTINFO_H

#include <QDateTime>
#include <QString>

// 项目基本信息（对应数据库 project 表）
struct ProjectInfo {
    qint64 id = 0;
    QString name;
    QString version;
    QDateTime createTime;
    QDateTime updateTime;
    QDateTime lastOpenTime;
};

#endif // PROJECTINFO_H
