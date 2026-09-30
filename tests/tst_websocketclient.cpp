// SPDX-License-Identifier: MIT
//
// WebSocketClient against a real (local) server: the connection lifecycle its owner relies on.
#include "MiniWsServer.h"
#include "services/WebSocketClient.h"

#include <QSignalSpy>
#include <QtTest>

using namespace WebSocketFrame;

class TstWebSocketClient : public QObject {
    Q_OBJECT
private:
    struct Rig {
        MiniWsServer server;
        WebSocketClient client;
        QSignalSpy connected{&client, &WebSocketClient::connected};
        QSignalSpy disconnected{&client, &WebSocketClient::disconnected};
        QSignalSpy errors{&client, &WebSocketClient::errorOccurred};
        QSignalSpy texts{&client, &WebSocketClient::textMessageReceived};
    };

    static bool open(Rig &rig) {
        rig.client.open(rig.server.url());
        return QTest::qWaitFor([&] { return rig.connected.count() == 1; }, 5000);
    }

private slots:
    void exchangesText() {
        Rig rig;
        QVERIFY(open(rig));
        QVERIFY(rig.client.isConnected());

        rig.server.sendText("hello");
        QTRY_COMPARE(rig.texts.count(), 1);
        QCOMPARE(rig.texts.at(0).at(0).toString(), QString("hello"));

        rig.client.sendText(QString::fromUtf8("héllo ✓"));
        QTRY_COMPARE(rig.server.received.size(), 1);
        QCOMPARE(rig.server.received.at(0), QString::fromUtf8("héllo ✓"));
    }

    void serverCloseReportsDisconnected() {
        // The regression: a close frame from the server parked the client in Closing, and the
        // TCP disconnect that followed was then not reported at all.
        Rig rig;
        QVERIFY(open(rig));
        rig.server.sendClose();
        QTRY_COMPARE(rig.disconnected.count(), 1);
        QVERIFY(rig.server.clientSentClose);   // the close was answered, as the RFC requires
        QVERIFY(!rig.client.isConnected());
        QCOMPARE(rig.errors.count(), 0);       // an orderly close is not an error
    }

    void droppedConnectionReportsDisconnected() {
        Rig rig;
        QVERIFY(open(rig));
        rig.server.dropConnection();
        QTRY_COMPARE(rig.disconnected.count(), 1);
    }

    void ownCloseIsSilent() {
        Rig rig;
        QVERIFY(open(rig));
        rig.client.close();
        QTest::qWait(200);
        QCOMPARE(rig.disconnected.count(), 0);
        QCOMPARE(rig.errors.count(), 0);
    }

    void reassemblesFragments() {
        Rig rig;
        QVERIFY(open(rig));
        rig.server.sendFrame(OpText, "Hel", /*fin=*/false);
        rig.server.sendFrame(OpContinuation, "lo, ", false);
        rig.server.sendFrame(OpContinuation, "world", true);
        QTRY_COMPARE(rig.texts.count(), 1);
        QCOMPARE(rig.texts.at(0).at(0).toString(), QString("Hello, world"));
    }

    void answersPing() {
        Rig rig;
        QVERIFY(open(rig));
        rig.server.sendFrame(OpPing, "are you there");
        QTRY_COMPARE(rig.server.pongs.size(), 1);
        QCOMPARE(rig.server.pongs.at(0), QByteArray("are you there"));
    }

    void dropsAnOversizedFrame() {
        Rig rig;
        QVERIFY(open(rig));
        QByteArray header;
        header.append(char(0x81));
        header.append(char(127));
        const quint64 len = quint64(1) << 40;
        for (int i = 7; i >= 0; --i) header.append(char((len >> (i * 8)) & 0xFF));
        rig.server.sendRaw(header);
        QTRY_COMPARE(rig.errors.count(), 1);
        QVERIFY(rig.errors.at(0).at(0).toString().contains("too large"));
        QCOMPARE(rig.disconnected.count(), 1);
        QVERIFY(!rig.client.isConnected());
    }

    void refusesARejectedUpgrade() {
        Rig rig;
        rig.server.handshake = MiniWsServer::Handshake::Reject403;
        rig.client.open(rig.server.url());
        QTRY_VERIFY(rig.errors.count() >= 1);
        QVERIFY(rig.errors.at(0).at(0).toString().contains("403"));
        QCOMPARE(rig.connected.count(), 0);
    }

    void refusesABadAcceptKey() {
        Rig rig;
        rig.server.handshake = MiniWsServer::Handshake::BadAcceptKey;
        rig.client.open(rig.server.url());
        QTRY_COMPARE(rig.errors.count(), 1);
        QVERIFY(rig.errors.at(0).at(0).toString().contains("Sec-WebSocket-Accept"));
        QCOMPARE(rig.connected.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TstWebSocketClient)
#include "tst_websocketclient.moc"
