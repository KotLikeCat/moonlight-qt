#include "clipboardbundle.h"

#include <algorithm>
#include <bitset>
#include <cstring>

namespace ClipboardBundle {

static const char kMagic[4] = {'M', 'L', 'C', 'B'};
static constexpr quint8 kVersion = 1;
static constexpr qsizetype kHeaderSize = 6;
static constexpr qsizetype kItemHeaderSize = 5;

static quint32 readLe32(const char* p)
{
    const auto* u = reinterpret_cast<const unsigned char*>(p);
    return quint32(u[0]) | (quint32(u[1]) << 8) | (quint32(u[2]) << 16) | (quint32(u[3]) << 24);
}

quint32 maskOf(ItemType type)
{
    return 1u << (static_cast<quint8>(type) - 1u);
}

quint32 maskOf(const QVector<Item>& items)
{
    quint32 mask = 0;
    for (const Item& item : items) {
        mask |= maskOf(item.type);
    }
    return mask;
}

qsizetype encodedSize(const QVector<Item>& items)
{
    qsizetype size = kHeaderSize;
    for (const Item& item : items) {
        size += kItemHeaderSize + item.data.size();
    }
    return size;
}

QByteArray encode(const QVector<Item>& items)
{
    QByteArray out;
    out.reserve(encodedSize(items));
    out.append(kMagic, 4);
    out.append(char(kVersion));
    out.append(char(items.size()));
    for (const Item& item : items) {
        out.append(char(static_cast<quint8>(item.type)));
        const quint32 length = quint32(item.data.size());
        for (int shift = 0; shift < 32; shift += 8) {
            out.append(char((length >> shift) & 0xFF));
        }
        out.append(item.data);
    }
    return out;
}

DecodeResult decode(const QByteArray& data, qsizetype maxBytes)
{
    DecodeResult result;
    auto fail = [&result](DecodeError error) {
        result.error = error;
        result.items.clear();
        return result;
    };

    if (data.size() > maxBytes) {
        return fail(DecodeError::TooLarge);
    }
    if (data.size() < kHeaderSize) {
        return fail(DecodeError::Truncated);
    }
    if (memcmp(data.constData(), kMagic, 4) != 0) {
        return fail(DecodeError::BadMagic);
    }
    if (quint8(data[4]) != kVersion) {
        return fail(DecodeError::BadVersion);
    }
    const int count = quint8(data[5]);
    if (count > MaxItems) {
        return fail(DecodeError::TooManyItems);
    }

    qsizetype pos = kHeaderSize;
    std::bitset<256> seen;
    for (int i = 0; i < count; i++) {
        if (data.size() - pos < kItemHeaderSize) {
            return fail(DecodeError::Truncated);
        }
        const quint8 type = quint8(data[pos]);
        const quint32 length = readLe32(data.constData() + pos + 1);
        pos += kItemHeaderSize;
        if (qsizetype(length) > data.size() - pos) {
            return fail(DecodeError::Truncated);
        }
        if (seen.test(type)) {
            return fail(DecodeError::DuplicateType);
        }
        seen.set(type);
        if (type >= 1 && type <= 4) {
            result.items.append({static_cast<ItemType>(type), data.mid(pos, qsizetype(length))});
        }
        pos += qsizetype(length);
    }
    if (pos != data.size()) {
        return fail(DecodeError::TrailingBytes);
    }
    return result;
}

bool fitToLimit(QVector<Item>& items, qsizetype maxBytes)
{
    if (encodedSize(items) <= maxBytes) {
        return true;
    }
    items.erase(std::remove_if(items.begin(), items.end(),
                               [](const Item& item) { return item.type == ItemType::Png; }),
                items.end());
    return encodedSize(items) <= maxBytes;
}

const char* errorName(DecodeError error)
{
    switch (error) {
    case DecodeError::None: return "ok";
    case DecodeError::BadMagic: return "bad_magic";
    case DecodeError::BadVersion: return "bad_version";
    case DecodeError::TooManyItems: return "too_many_items";
    case DecodeError::Truncated: return "truncated";
    case DecodeError::TrailingBytes: return "trailing_bytes";
    case DecodeError::DuplicateType: return "duplicate_type";
    case DecodeError::TooLarge: return "too_large";
    }
    return "unknown";
}

}
