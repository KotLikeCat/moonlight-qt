#pragma once

#include <QByteArray>
#include <QVector>

// Wire container ("MLCB" v1) shared with the Vibepollo host for clipboard sync.
namespace ClipboardBundle {

enum class ItemType : quint8 {
    Text = 1,   // UTF-8, "\n" line endings
    Html = 2,   // UTF-8 HTML
    Rtf = 3,    // RTF bytes
    Png = 4,    // PNG image
};

struct Item {
    ItemType type;
    QByteArray data;
};

constexpr quint32 FormatText = 0x1;
constexpr quint32 FormatHtml = 0x2;
constexpr quint32 FormatRtf = 0x4;
constexpr quint32 FormatPng = 0x8;
constexpr quint32 FormatAll = 0xF;
constexpr int MaxItems = 8;
constexpr qsizetype MaxBytes = 32 * 1024 * 1024;

enum class DecodeError {
    None,
    BadMagic,
    BadVersion,
    TooManyItems,
    Truncated,
    TrailingBytes,
    DuplicateType,
    TooLarge,
};

struct DecodeResult {
    DecodeError error = DecodeError::None;
    QVector<Item> items; // known types only, in wire order; empty on error
};

quint32 maskOf(ItemType type);
quint32 maskOf(const QVector<Item>& items);
qsizetype encodedSize(const QVector<Item>& items);
QByteArray encode(const QVector<Item>& items);
DecodeResult decode(const QByteArray& data, qsizetype maxBytes = MaxBytes);
// Drops the PNG item if the encoded bundle exceeds maxBytes. Returns false if it is still too large.
bool fitToLimit(QVector<Item>& items, qsizetype maxBytes);
const char* errorName(DecodeError error);

}
