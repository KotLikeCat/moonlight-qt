#import <AppKit/AppKit.h>

#include <QtTest>

#include "clipboardbundle.h"

using namespace ClipboardBundle;

static const char* kPngHex =
    "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489"
    "0000000d4944415478da636460f85f0f0002870180eb47ba920000000049454e44ae426082";

class ClipboardTests : public QObject
{
    Q_OBJECT

private slots:
    void bundleSharedVectors();
    void bundleRejectsInputOverLimit();
    void bundleFitToLimitDropsImageFirst();
};

void ClipboardTests::bundleSharedVectors()
{
    QFile file(QFINDTESTDATA("vectors.txt"));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    int count = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        const QList<QByteArray> parts = line.split(' ');
        QCOMPARE(parts.size(), 3);
        const QByteArray name = parts[0];
        const QByteArray expect = parts[1];
        const QByteArray data = QByteArray::fromHex(parts[2]);
        const DecodeResult result = decode(data, 1024);
        if (expect == "ok" || expect == "ok_lossy") {
            QVERIFY2(result.error == DecodeError::None, name.constData());
            const QByteArray reencoded = encode(result.items);
            if (expect == "ok") {
                QVERIFY2(reencoded == data, name.constData());
            }
            else {
                QVERIFY2(reencoded != data, name.constData());
            }
        }
        else {
            QVERIFY2(expect == errorName(result.error), name.constData());
            QVERIFY2(result.items.isEmpty(), name.constData());
        }
        count++;
    }
    QCOMPARE(count, 12);
}

void ClipboardTests::bundleRejectsInputOverLimit()
{
    const QByteArray data = encode({{ItemType::Text, QByteArray(100, 'a')}});
    QVERIFY(decode(data, 50).error == DecodeError::TooLarge);
    QVERIFY(decode(data, data.size()).error == DecodeError::None);
}

void ClipboardTests::bundleFitToLimitDropsImageFirst()
{
    QVector<Item> items {{ItemType::Text, QByteArray("hello")}, {ItemType::Png, QByteArray(1000, 'p')}};
    QVERIFY(fitToLimit(items, 100));
    QCOMPARE(items.size(), 1);
    QVERIFY(items[0].type == ItemType::Text);

    QVector<Item> big {{ItemType::Text, QByteArray(1000, 't')}};
    QVERIFY(!fitToLimit(big, 100));

    QVector<Item> small {{ItemType::Text, QByteArray("t")}, {ItemType::Png, QByteArray::fromHex(kPngHex)}};
    QVERIFY(fitToLimit(small, 1024));
    QCOMPARE(small.size(), 2);
    QCOMPARE(maskOf(small), FormatText | FormatPng);
}

QTEST_GUILESS_MAIN(ClipboardTests)
#include "tst_clipboard.moc"
