#import <AppKit/AppKit.h>

#include <QtTest>

#include "clipboardbundle.h"
#include "clipboardsyncstate.h"
#include "macpasteboard.h"
#include "manifestbuilder.h"
#include "filecodec.h"

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
    void stateGreetingBeforeFirstFocusMacWins();
    void stateGreetingBeforeFirstFocusSensitiveMacFetchesHost();
    void statePushSkippedKeepsHostPending();
    void statePushDoesNotSupersedeNewerHostChange();
    void pasteboardHtmlGetsUtf8Charset();
    void stateHostBurstCoalesces();
    void stateStaleEmptyReplyKeepsNewVersionPending();
    void pasteboardRoundTrip();
    void pasteboardDetectsSensitiveData();
    void pasteboardConvertsTiffToPng();
    void pasteboardIgnoresFileOnlyContent();
    void encodesSharedVectors();
    void buildsTreePreOrder();
    void normalizesToNfc();
    void skipsSymlinksAndDsStore();
    void enforcesLimits();
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
    state.onFocusGained(1, false);
    state.onPushSucceeded(1);
    state.onFocusLost();
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
    state.onFocusGained(1, false);
    state.onPushSucceeded(1);
    state.onFocusLost();
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

void ClipboardTests::stateGreetingBeforeFirstFocusMacWins()
{
    ClipboardSyncState state;
    QCOMPARE(state.onHostChanged(5, FormatText), Action::None);
    QCOMPARE(state.onFocusGained(10, false), Action::Push);
    state.onPushSucceeded(10);
    QVERIFY(!state.hostDataMissing());
    QCOMPARE(state.onFocusLost(), Action::None);
}

void ClipboardTests::stateGreetingBeforeFirstFocusSensitiveMacFetchesHost()
{
    ClipboardSyncState state;
    QCOMPARE(state.onHostChanged(5, FormatText | FormatPng), Action::None);
    QCOMPARE(state.onFocusGained(10, true), Action::ScheduleQuickFetch);
    state.onFetchSucceeded(5, FormatText, 11, false);
    QCOMPARE(state.onFocusLost(), Action::FetchFull);
}

void ClipboardTests::statePushSkippedKeepsHostPending()
{
    ClipboardSyncState state;
    QCOMPARE(state.onHostChanged(6, FormatText), Action::None);
    QCOMPARE(state.onFocusGained(10, false), Action::Push);
    state.onPushSkipped(10);
    QVERIFY(state.hostDataMissing());
    QCOMPARE(state.onFocusLost(), Action::FetchFull);
}

void ClipboardTests::statePushDoesNotSupersedeNewerHostChange()
{
    ClipboardSyncState state;
    QCOMPARE(state.onHostChanged(5, FormatText), Action::None);
    QCOMPARE(state.onFocusGained(10, false), Action::Push);
    QCOMPARE(state.onHostChanged(6, FormatText), Action::ScheduleQuickFetch);
    state.onPushSucceeded(10);
    QVERIFY(state.hostDataMissing());
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

static NSData* toNSData(const QByteArray& bytes)
{
    return [NSData dataWithBytes:bytes.constData() length:(NSUInteger)bytes.size()];
}

void ClipboardTests::pasteboardRoundTrip()
{
    MacPasteboard board(QStringLiteral("com.moonlight.clipboard-tests.roundtrip"));
    const QVector<Item> items {
        {ItemType::Text, QByteArray("Привет\nмир")},
        {ItemType::Html, QByteArray("<b>жирный</b>")},
        {ItemType::Rtf, QByteArray("{\\rtf1\\ansi hello}")},
        {ItemType::Png, QByteArray::fromHex(kPngHex)},
    };
    const long changeCount = board.write(items);
    QCOMPARE(board.changeCount(), changeCount);
    const QVector<Item> back = board.read();
    QCOMPARE(back.size(), 4);
    for (int i = 0; i < 4; i++) {
        QVERIFY(back[i].type == items[i].type);
        if (items[i].type == ItemType::Html) {
            // The writer prepends a charset declaration.
            QVERIFY(back[i].data.endsWith(items[i].data));
        }
        else {
            QCOMPARE(back[i].data, items[i].data);
        }
    }
    QVERIFY(!board.hasSensitiveData());
    board.releaseForTests();
}

void ClipboardTests::pasteboardHtmlGetsUtf8Charset()
{
    const QString name = QStringLiteral("com.moonlight.clipboard-tests.html");
    MacPasteboard board(name);
    NSPasteboard* raw = [NSPasteboard pasteboardWithName:name.toNSString()];
    const QByteArray fragment("<b>жирный</b>");
    board.write({{ItemType::Html, fragment}});
    NSData* stored = [raw dataForType:@"public.html"];
    const QByteArray out((const char*)stored.bytes, (qsizetype)stored.length);
    QVERIFY(out.startsWith(QByteArray("<meta charset=\"utf-8\">")));
    QVERIFY(out.contains(fragment));

    const QByteArray withCharset("<meta http-equiv=\"Content-Type\" content=\"text/html; CHARSET=utf-8\"><i>x</i>");
    board.write({{ItemType::Html, withCharset}});
    stored = [raw dataForType:@"public.html"];
    QCOMPARE(QByteArray((const char*)stored.bytes, (qsizetype)stored.length), withCharset);
    board.releaseForTests();
}

void ClipboardTests::pasteboardDetectsSensitiveData()
{
    NSPasteboard* raw = [NSPasteboard pasteboardWithName:@"com.moonlight.clipboard-tests.sensitive"];
    [raw clearContents];
    [raw setString:@"secret" forType:NSPasteboardTypeString];
    [raw setData:[NSData data] forType:@"org.nspasteboard.ConcealedType"];
    MacPasteboard board(QStringLiteral("com.moonlight.clipboard-tests.sensitive"));
    QVERIFY(board.hasSensitiveData());
    [raw clearContents];
    [raw setString:@"public" forType:NSPasteboardTypeString];
    QVERIFY(!board.hasSensitiveData());
    board.releaseForTests();
}

void ClipboardTests::pasteboardConvertsTiffToPng()
{
    NSBitmapImageRep* rep = [NSBitmapImageRep imageRepWithData:toNSData(QByteArray::fromHex(kPngHex))];
    NSPasteboard* raw = [NSPasteboard pasteboardWithName:@"com.moonlight.clipboard-tests.tiff"];
    [raw clearContents];
    [raw setData:[rep TIFFRepresentation] forType:NSPasteboardTypeTIFF];
    MacPasteboard board(QStringLiteral("com.moonlight.clipboard-tests.tiff"));
    const QVector<Item> items = board.read();
    QCOMPARE(items.size(), 1);
    QVERIFY(items[0].type == ItemType::Png);
    QVERIFY(items[0].data.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8)));
    board.releaseForTests();
}

void ClipboardTests::pasteboardIgnoresFileOnlyContent()
{
    // Finder puts a file URL, the file name as text and an icon; files are not synced yet.
    NSPasteboard* raw = [NSPasteboard pasteboardWithName:@"com.moonlight.clipboard-tests.files"];
    [raw clearContents];
    [raw declareTypes:@[NSPasteboardTypeFileURL, NSPasteboardTypeString] owner:nil];
    [raw setString:@"file:///tmp/example.txt" forType:NSPasteboardTypeFileURL];
    [raw setString:@"example.txt" forType:NSPasteboardTypeString];
    MacPasteboard board(QStringLiteral("com.moonlight.clipboard-tests.files"));
    QVERIFY(board.read().isEmpty());
    board.releaseForTests();
}

static QByteArray testOfferId()
{
    QByteArray id;
    for (int i = 0; i < 16; i++) id.append(char(i));
    return id;
}

static ClipboardFiles::Entry mk(bool dir, quint64 size, qint64 mtime, const QString& rel)
{
    return ClipboardFiles::Entry{dir, size, mtime, rel, QString()};
}

void ClipboardTests::encodesSharedVectors()
{
    QMap<QByteArray, QVector<ClipboardFiles::Entry>> cases;
    cases["single_file"] = {mk(false, 5, 1700000000000LL, "a.txt")};
    cases["cyrillic_tree"] = {mk(true, 0, 1700000000000LL, QString::fromUtf8("\xd0\x9f\xd0\xb0\xd0\xbf\xd0\xba\xd0\xb0")),
                              mk(false, 1048576, 1700000000123LL, QString::fromUtf8("\xd0\x9f\xd0\xb0\xd0\xbf\xd0\xba\xd0\xb0/\xd1\x84\xd0\xb0\xd0\xb9\xd0\xbb \xd0\xb9.txt")),
                              mk(false, 0, 0, "b.bin")};
    cases["over_4gib"] = {mk(false, 5000000000ULL, 1, "big.iso")};

    QFile file(QFINDTESTDATA("files_vectors.txt"));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    int count = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QList<QByteArray> parts = line.split(' ');
        QCOMPARE(parts.size(), 2);
        QVERIFY2(cases.contains(parts[0]), parts[0].constData());
        QCOMPARE(ClipboardFiles::encodeManifest(testOfferId(), cases[parts[0]]).toHex(), parts[1]);
        count++;
    }
    QCOMPARE(count, 3);
}

void ClipboardTests::buildsTreePreOrder()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir root(tmp.path());
    QVERIFY(root.mkpath("top/sub"));
    QFile a(root.filePath("top/a.txt")); QVERIFY(a.open(QIODevice::WriteOnly)); a.write("hello"); a.close();
    QFile b(root.filePath("top/sub/b.txt")); QVERIFY(b.open(QIODevice::WriteOnly)); b.close();
    auto r = ClipboardFiles::buildManifest({root.filePath("top")});
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QStringList rels;
    for (auto& e : r.entries) rels << e.relativePath;
    QCOMPARE(rels, (QStringList{"top", "top/a.txt", "top/sub", "top/sub/b.txt"}));
    QVERIFY(r.entries[0].isDir);
    QVERIFY(!r.entries[1].isDir);
    QCOMPARE(r.entries[1].size, quint64(5));
    QVERIFY(r.entries[1].mtimeMs > 0);
    QVERIFY(r.entries[1].absolutePath.endsWith("top/a.txt"));
}

void ClipboardTests::normalizesToNfc()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString nfd = QString::fromUtf8("\xd0\xb8\xcc\x86.txt");
    const QString nfc = QString::fromUtf8("\xd0\xb9.txt");
    QVERIFY(nfd != nfc);
    QFile f(tmp.path() + "/" + nfd);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    auto r = ClipboardFiles::buildManifest({tmp.path() + "/" + nfd});
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QCOMPARE(r.entries.size(), 1);
    QCOMPARE(r.entries[0].relativePath, nfc);
}

void ClipboardTests::skipsSymlinksAndDsStore()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir root(tmp.path());
    QVERIFY(root.mkpath("top"));
    QFile a(root.filePath("top/a.txt")); QVERIFY(a.open(QIODevice::WriteOnly)); a.close();
    QFile d(root.filePath("top/.DS_Store")); QVERIFY(d.open(QIODevice::WriteOnly)); d.close();
    QVERIFY(QFile::link(root.filePath("top/a.txt"), root.filePath("top/link")));
    auto r = ClipboardFiles::buildManifest({root.filePath("top")});
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QStringList rels;
    for (auto& e : r.entries) rels << e.relativePath;
    QCOMPARE(rels, (QStringList{"top", "top/a.txt"}));
    QVERIFY(r.skipped.size() >= 1);
}

void ClipboardTests::enforcesLimits()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir root(tmp.path());
    QStringList paths;
    for (const char* n : {"1", "2", "3"}) {
        QFile f(root.filePath(n)); QVERIFY(f.open(QIODevice::WriteOnly)); f.close();
        paths << root.filePath(n);
    }
    ClipboardFiles::Limits lim;
    lim.maxEntries = 2;
    QVERIFY(!ClipboardFiles::buildManifest(paths, lim).error.isEmpty());
    QVERIFY(ClipboardFiles::buildManifest(paths).error.isEmpty());
    ClipboardFiles::Limits lim2;
    lim2.maxComponentUtf16 = 0;
    QVERIFY(!ClipboardFiles::buildManifest(paths, lim2).error.isEmpty());
    QCOMPARE(ClipboardFiles::newOfferId().size(), 16);
}

QTEST_GUILESS_MAIN(ClipboardTests)
#include "tst_clipboard.moc"
