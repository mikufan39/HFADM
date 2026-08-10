#include "pinyin.h"

#include "pinyin_data.h"

namespace {

// 二分查找：返回字符对应拼音（首读音、已归一化），未收录返回 nullptr
const char *lookupPinyin(unsigned int cp)
{
    std::size_t lo = 0;
    std::size_t hi = kPinyinTableSize;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        const PinyinEntry &entry = kPinyinTable[mid];
        if (entry.cp < cp) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < kPinyinTableSize && kPinyinTable[lo].cp == cp) {
        return kPinyinTable[lo].py;
    }
    return nullptr;
}

} // namespace

namespace Pinyin {

QString toPinyin(const QString &text)
{
    QString out;
    out.reserve(text.size() * 3);
    for (const QChar &ch : text) {
        const ushort u = ch.unicode();
        if (u < 128) {
            out.append(ch.toLower());
            continue;
        }
        const char *py = lookupPinyin(u);
        if (py) {
            out.append(QString::fromUtf8(py));
        } else {
            out.append(ch); // 未收录字符（如符号/其他文字）原样保留
        }
    }
    return out;
}

QString initials(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar &ch : text) {
        const ushort u = ch.unicode();
        if (u < 128) {
            continue; // 非中文（字母/数字/符号）不产生首字母
        }
        const char *py = lookupPinyin(u);
        if (py && *py) {
            out.append(QChar::fromLatin1(*py));
        }
    }
    return out;
}

} // namespace Pinyin
