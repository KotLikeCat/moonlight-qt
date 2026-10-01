#import <AppKit/AppKit.h>

#include <QtTest>

#include "clipboardbundle.h"
#include "clipboardsyncstate.h"

using namespace ClipboardBundle;
using Action = ClipboardSyncState::Action;

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
    void stateUnfocusedHostChangeFetchesFull();
    void stateFocusedTextChangeSchedulesQuickFetch();
    void stateImageOnlyWaitsForFocusLoss();
    void stateQuickFetchLeavesImagePending();
    void stateEmptyQuickFetchKeepsImagePending();
    void stateFullFetchCompletesEvenIfImageDropped();
    void statePushRules();
    void stateHostBurstCoalesces();
    void stateStaleEmptyReplyKeepsNewVersionPending();
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

static ClipboardSyncState focusedState()
{
    ClipboardSyncState state;
    state.onFocusGained(1, false);
    state.onPushSucceeded(1);
    return state;
}

void ClipboardTests::stateUnfocusedHostChangeFetchesFull()
{
    ClipboardSyncState state;
    QCOMPARE(state.onHostChanged(5, FormatText), Action::FetchFull);
    QCOMPARE(state.onFocusLost(), Action::FetchFull);
}

void ClipboardTests::stateFocusedTextChangeSchedulesQuickFetch()
{
    ClipboardSyncState state = focusedState();
    QCOMPARE(state.onHostChanged(5, FormatText | FormatHtml), Action::ScheduleQuickFetch);
    state.onFetchSucceeded(5, FormatText | FormatHtml, 2, false);
    QVERIFY(!state.hostDataMissing());
    QCOMPARE(state.onFocusLost(), Action::None);
}

void ClipboardTests::stateImageOnlyWaitsForFocusLoss()
{
    ClipboardSyncState state = focusedState();
    QCOMPARE(state.onHostChanged(6, FormatPng), Action::None);
    QCOMPARE(state.onFocusLost(), Action::FetchFull);
}

void ClipboardTests::stateQuickFetchLeavesImagePending()
{
    ClipboardSyncState state = focusedState();
    QCOMPARE(state.onHostChanged(7, FormatAll), Action::ScheduleQuickFetch);
    state.onFetchSucceeded(7, FormatText | FormatHtml | FormatRtf, 3, false);
    QVERIFY(state.hostDataMissing());
    QCOMPARE(state.onFocusLost(), Action::FetchFull);
    state.onFetchSucceeded(7, FormatAll, 4, true);
    QVERIFY(!state.hostDataMissing());
}

void ClipboardTests::stateEmptyQuickFetchKeepsImagePending()
{
    ClipboardSyncState state = focusedState();
    QCOMPARE(state.onHostChanged(8, FormatText | FormatPng), Action::ScheduleQuickFetch);
    state.onFetchEmpty(8, false);
    QVERIFY(state.hostDataMissing());
    QCOMPARE(state.onFocusLost(), Action::FetchFull);
    state.onFetchEmpty(8, true);
    QVERIFY(!state.hostDataMissing());
}

void ClipboardTests::stateFullFetchCompletesEvenIfImageDropped()
{
    ClipboardSyncState state;
    QCOMPARE(state.onHostChanged(9, FormatText | FormatPng), Action::FetchFull);
    state.onFetchSucceeded(9, FormatText, 5, true);
    QVERIFY(!state.hostDataMissing());
    QCOMPARE(state.onFocusLost(), Action::None);
}

void ClipboardTests::statePushRules()
{
    ClipboardSyncState state;
    QCOMPARE(state.onFocusGained(10, false), Action::Push);
    state.onPushSucceeded(10);
    state.onFocusLost();
    QCOMPARE(state.onFocusGained(10, false), Action::None);  // unchanged pasteboard
    state.onFocusLost();
    QCOMPARE(state.onFocusGained(11, true), Action::None);   // concealed/transient data
    state.onFocusLost();
    QCOMPARE(state.onHostChanged(12, FormatText), Action::FetchFull);
    state.onFetchSucceeded(12, FormatText, 13, true);
    QCOMPARE(state.onFocusGained(13, false), Action::None);  // our own write
    state.onFocusLost();
    QCOMPARE(state.onFocusGained(14, false), Action::Push);  // user copied something new
}

void ClipboardTests::stateHostBurstCoalesces()
{
    ClipboardSyncState state = focusedState();
    QCOMPARE(state.onHostChanged(20, FormatText), Action::ScheduleQuickFetch);
    QCOMPARE(state.onHostChanged(21, FormatText), Action::ScheduleQuickFetch);
    state.onFetchSucceeded(20, FormatText, 2, false);   // stale response
    QVERIFY(state.hostDataMissing());
    state.onFetchSucceeded(21, FormatText, 3, false);
    QVERIFY(!state.hostDataMissing());
}

void ClipboardTests::stateStaleEmptyReplyKeepsNewVersionPending()
{
    ClipboardSyncState state = focusedState();
    QCOMPARE(state.onHostChanged(30, FormatText), Action::ScheduleQuickFetch);
    QCOMPARE(state.onHostChanged(31, FormatText), Action::ScheduleQuickFetch);
    state.onFetchEmpty(30, false);
    QVERIFY(state.hostDataMissing());
    state.onFetchEmpty(31, false);
    QVERIFY(!state.hostDataMissing());
}

QTEST_GUILESS_MAIN(ClipboardTests)
#include "tst_clipboard.moc"
