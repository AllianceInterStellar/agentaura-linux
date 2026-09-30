// SPDX-License-Identifier: MIT
//
// ChatService speaking the gateway protocol to a scripted gateway on localhost.
#include "MiniWsServer.h"
#include "services/ChatService.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QtTest>

namespace {

/// The gateway side of the protocol, as far as these tests need it: challenge on connect,
/// hello-ok for a good token, a session list, and a record of every request.
struct FakeGateway {
    MiniWsServer ws;
    QString acceptedToken = "tok";
    QString sessionKey = "agent:main:resumed";
    QList<QJsonObject> requests;

    FakeGateway() {
        ws.onUpgraded = [this] {
            ws.sendJson({{"type", "event"}, {"event", "connect.challenge"},
                         {"payload", QJsonObject{{"nonce", "n-1"}}}});
        };
        ws.onText = [this](const QString &text) {
            const QJsonObject req = QJsonDocument::fromJson(text.toUtf8()).object();
            requests << req;
            const QString method = req.value("method").toString();
            const QJsonValue id = req.value("id");
            if (method == "connect") {
                const bool ok = req["params"]["auth"]["token"].toString() == acceptedToken;
                ws.sendJson({{"type", "res"}, {"id", id}, {"ok", ok},
                             {"payload", QJsonObject{{"type", ok ? "hello-ok" : "error"}}}});
            } else if (method == "sessions.list") {
                ws.sendJson({{"type", "res"}, {"id", id}, {"ok", true},
                             {"payload", QJsonObject{{"sessions", QJsonArray{
                                 QJsonObject{{"key", "other:thing"}},
                                 QJsonObject{{"key", sessionKey}}}}}}});
            } else if (method == "chat.send") {
                ws.sendJson({{"type", "res"}, {"id", id}, {"ok", true},
                             {"payload", QJsonObject{{"runId", "run-1"}}}});
            }
        };
    }

    QJsonObject lastRequest(const QString &method) const {
        for (auto it = requests.crbegin(); it != requests.crend(); ++it)
            if (it->value("method").toString() == method) return *it;
        return {};
    }

    void chat(const QString &state, const QString &text, const QString &runId = "run-1") {
        QJsonObject payload{{"sessionKey", sessionKey}, {"runId", runId}, {"state", state}};
        if (state == "error") payload["errorMessage"] = text;
        else payload["message"] = QJsonObject{{"content", QJsonArray{
            QJsonObject{{"type", "text"}, {"text", text}}}}};
        ws.sendJson({{"type", "event"}, {"event", "chat"}, {"payload", payload}});
    }
};

QString httpUrl(const MiniWsServer &ws) {
    QUrl u = ws.url();
    u.setScheme("http");   // the claw record carries http(s); ChatService maps it to ws(s)
    return u.toString();
}

}  // namespace

class TstChatService : public QObject {
    Q_OBJECT
private:
    static bool connectTo(ChatService &chat, FakeGateway &gw, const QString &token = "tok") {
        chat.configure(httpUrl(gw.ws), token);
        chat.connectToGateway();
        return QTest::qWaitFor([&] { return chat.isConnected(); }, 5000);
    }

private slots:
    void authenticatesWithTheToken() {
        FakeGateway gw;
        ChatService chat;
        QVERIFY(connectTo(chat, gw));
        const QJsonObject connect = gw.lastRequest("connect");
        QCOMPARE(connect["params"]["auth"]["token"].toString(), QString("tok"));
        QCOMPARE(connect["params"]["client"]["version"].toString(), QString(AGENTAURA_VERSION));
        QTRY_COMPARE(chat.sessionKey(), gw.sessionKey);   // resumed from sessions.list
    }

    void streamsATurn() {
        FakeGateway gw;
        ChatService chat;
        QSignalSpy deltas(&chat, &ChatService::deltaReceived);
        QSignalSpy done(&chat, &ChatService::messageCompleted);
        QVERIFY(connectTo(chat, gw));
        QTRY_COMPARE(chat.sessionKey(), gw.sessionKey);

        chat.sendChatMessage("hi");
        QTRY_VERIFY(!gw.lastRequest("chat.send").isEmpty());
        const QJsonObject params = gw.lastRequest("chat.send")["params"].toObject();
        QCOMPARE(params["sessionKey"].toString(), gw.sessionKey);
        QCOMPARE(params["message"].toString(), QString("hi"));

        gw.chat("delta", "Hel");
        gw.chat("delta", "Hello");
        gw.chat("final", "Hello world");
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(deltas.count(), 2);
        QCOMPARE(deltas.at(1).at(0).toString(), QString("Hello"));
        QCOMPARE(done.at(0).at(0).toString(), QString("Hello world"));

        // A late tail after final must not reopen the turn.
        gw.chat("delta", "ghost");
        QTest::qWait(100);
        QCOMPARE(deltas.count(), 2);
    }

    void queuesAMessageTypedBeforeConnecting() {
        FakeGateway gw;
        ChatService chat;
        chat.configure(httpUrl(gw.ws), "tok");
        chat.sendChatMessage("early");   // connects by itself, sends once the session is known
        QTRY_VERIFY(!gw.lastRequest("chat.send").isEmpty());
        const QJsonObject params = gw.lastRequest("chat.send")["params"].toObject();
        QCOMPARE(params["message"].toString(), QString("early"));
        QCOMPARE(params["sessionKey"].toString(), gw.sessionKey);
    }

    void ignoresAnotherRunsTail() {
        FakeGateway gw;
        ChatService chat;
        QSignalSpy deltas(&chat, &ChatService::deltaReceived);
        QVERIFY(connectTo(chat, gw));
        QTRY_COMPARE(chat.sessionKey(), gw.sessionKey);
        chat.sendChatMessage("hi");
        QTRY_VERIFY(!gw.lastRequest("chat.send").isEmpty());
        gw.chat("delta", "mine", "run-1");
        gw.chat("delta", "someone else's", "run-2");
        gw.chat("delta", "mine, more", "run-1");
        QTRY_COMPARE(deltas.count(), 2);
        QCOMPARE(deltas.at(1).at(0).toString(), QString("mine, more"));
    }

    void reportsAGatewayError() {
        FakeGateway gw;
        ChatService chat;
        QSignalSpy errors(&chat, &ChatService::errorOccurred);
        QVERIFY(connectTo(chat, gw));
        QTRY_COMPARE(chat.sessionKey(), gw.sessionKey);
        chat.sendChatMessage("hi");
        QTRY_VERIFY(!gw.lastRequest("chat.send").isEmpty());
        gw.chat("error", "model overloaded");
        QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(errors.at(0).at(0).toString(), QString("model overloaded"));
    }

    void serverCloseMidTurnEndsTheTurn() {
        // The regression from the WebSocket close bug: a gateway that closes cleanly mid-turn
        // has to end the turn and the connection at once, not after the 130s watchdog.
        FakeGateway gw;
        ChatService chat;
        QSignalSpy errors(&chat, &ChatService::errorOccurred);
        QVERIFY(connectTo(chat, gw));
        QTRY_COMPARE(chat.sessionKey(), gw.sessionKey);
        chat.sendChatMessage("hi");
        QTRY_VERIFY(!gw.lastRequest("chat.send").isEmpty());

        gw.ws.sendClose();
        QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(errors.at(0).at(0).toString(), QString("Connection lost"));
        QVERIFY(!chat.isConnected());
    }

    void rejectedTokenDisconnects() {
        FakeGateway gw;
        gw.acceptedToken = "the-right-one";
        ChatService chat;
        QSignalSpy errors(&chat, &ChatService::errorOccurred);
        chat.configure(httpUrl(gw.ws), "wrong");
        chat.connectToGateway();
        QTRY_COMPARE(errors.count(), 1);
        QVERIFY(errors.at(0).at(0).toString().contains("rejected the token"));
        QCOMPARE(chat.state(), ChatService::State::Disconnected);
    }

    void flattensMessageContent_data() {
        QTest::addColumn<QJsonValue>("raw");
        QTest::addColumn<QString>("text");
        QTest::addRow("string") << QJsonValue("plain") << "plain";
        QTest::addRow("text field") << QJsonValue(QJsonObject{{"text", "t"}}) << "t";
        QTest::addRow("nested content") << QJsonValue(QJsonObject{{"content", "c"}}) << "c";
        QTest::addRow("blocks") << QJsonValue(QJsonArray{
            QJsonObject{{"type", "text"}, {"text", "a"}},
            QJsonObject{{"type", "tool_use"}, {"name", "x"}},
            QJsonObject{{"type", "text"}, {"text", "b"}}}) << "ab";
        QTest::addRow("null") << QJsonValue() << "";
    }
    void flattensMessageContent() {
        QFETCH(QJsonValue, raw);
        QFETCH(QString, text);
        QCOMPARE(ChatService::parseMessageContent(raw), text);
    }
};

QTEST_GUILESS_MAIN(TstChatService)
#include "tst_chatservice.moc"
