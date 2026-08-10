#ifndef PINYIN_H
#define PINYIN_H

#include <QString>

// 拼音工具（搜索用）：内置 pinyin_data.h 字符→拼音表（pypinyin 0.55.0 生成）
namespace Pinyin {

// 汉字转拼音：取每字第一读音、无音调、小写；ü 归一为 v；
// 非中文按原字符保留（ASCII 小写）。如 "测试" -> "ceshi"
QString toPinyin(const QString &text);

// 各汉字音节首字母（非中文跳过）：如 "测试" -> "cs"
QString initials(const QString &text);

} // namespace Pinyin

#endif // PINYIN_H
