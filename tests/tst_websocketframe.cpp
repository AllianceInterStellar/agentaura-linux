// SPDX-License-Identifier: MIT
//
// RFC 6455 framing as the client applies it: what it accepts, what it refuses, what it sends.
#include "services/WebSocketFrame.h"

#include <QtTest>

using namespace WebSocketFrame;

class TstWebSocketFrame : public QObject {
    Q_OBJECT
private slots:
    void acceptKeyMatchesRfcExample() {
        // RFC 6455 §1.3.
        QCOMPARE(acceptKey("dGhlIHNhbXBsZSBub25jZQ=="), QByteArray("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    }

    void roundTrip_data() {
        QTest::addColumn<int>("size");
        QTest::addColumn<bool>("mask");
        for (int size : {0, 1, 125, 126, 65535, 65536, 200000}) {
            QTest::addRow("%d unmasked", size) << size << false;
            QTest::addRow("%d masked", size) << size << true;
        }
    }
    void roundTrip() {
        QFETCH(int, size);
        QFETCH(bool, mask);
        QByteArray payload(size, 'x');
        for (int i = 0; i < size; ++i) payload[i] = char(i * 7);
        QByteArray wire = encode(OpText, payload, mask);
        QCOMPARE(bool(quint8(wire.at(1)) & 0x80), mask);

        Frame f;
        QCOMPARE(parse(wire, f), Result::Frame);
        QCOMPARE(f.opcode, quint8(OpText));
        QVERIFY(f.fin);
        QCOMPARE(f.payload, payload);
        QVERIFY(wire.isEmpty());
    }

    void waitsForEveryPartOfAFrame() {
        const QByteArray whole = encode(OpText, QByteArray(300, 'a'), true);
        for (int cut = 0; cut < whole.size(); ++cut) {
            QByteArray part = whole.left(cut);
            Frame f;
            QCOMPARE(parse(part, f), Result::NeedMore);
            QCOMPARE(part.size(), cut);   // untouched until the frame is complete
        }
    }

    void takesOneFrameAtATime() {
        QByteArray wire = encode(OpText, "one", false) + encode(OpText, "two", false);
        Frame f;
        QCOMPARE(parse(wire, f), Result::Frame);
        QCOMPARE(f.payload, QByteArray("one"));
        QCOMPARE(parse(wire, f), Result::Frame);
        QCOMPARE(f.payload, QByteArray("two"));
        QCOMPARE(parse(wire, f), Result::NeedMore);
    }

    void refusesAnOversizedLengthBeforeItsPayload() {
        // A 64-bit length claiming 1 TiB, and no payload at all: the claim alone is refused,
        // rather than the client buffering and waiting for it.
        QByteArray wire;
        wire.append(char(0x81));
        wire.append(char(127));
        const quint64 len = quint64(1) << 40;
        for (int i = 7; i >= 0; --i) wire.append(char((len >> (i * 8)) & 0xFF));
        Frame f;
        QString error;
        QCOMPARE(parse(wire, f, &error), Result::Error);
        QVERIFY(error.contains("too large"));
    }

    void acceptsTheLargestAllowedMessage() {
        QByteArray wire = encode(OpText, QByteArray(int(kMaxMessageBytes), 'z'), false);
        Frame f;
        QCOMPARE(parse(wire, f), Result::Frame);
        QCOMPARE(f.payload.size(), qsizetype(kMaxMessageBytes));
    }

    void refusesMalformedControlFrames() {
        Frame f;
        QByteArray longPing = encode(OpPing, QByteArray(126, 'p'), false);
        QCOMPARE(parse(longPing, f), Result::Error);

        QByteArray fragmentedClose = encode(OpClose, QByteArray(), false);
        fragmentedClose[0] = char(quint8(fragmentedClose[0]) & 0x7F);
        QCOMPARE(parse(fragmentedClose, f), Result::Error);
    }

    void reportsFragments() {
        QByteArray wire = encode(OpText, "par", false);
        wire[0] = char(quint8(wire[0]) & 0x7F);
        Frame f;
        QCOMPARE(parse(wire, f), Result::Frame);
        QVERIFY(!f.fin);
        QCOMPARE(f.opcode, quint8(OpText));
    }

    void masksDifferentlyEachTime() {
        const QByteArray payload(64, 'm');
        QVERIFY(encode(OpText, payload, true) != encode(OpText, payload, true));
    }
};

QTEST_GUILESS_MAIN(TstWebSocketFrame)
#include "tst_websocketframe.moc"
