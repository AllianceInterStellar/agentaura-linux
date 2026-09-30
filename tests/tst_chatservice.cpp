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
    QJsonArray history;           ///< what chat.history answers with
    bool historyFails = false;

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
            } else if (method == "chat.history") {
                if (historyFails)
                    ws.sendJson({{"type", "res"}, {"id", id}, {"ok", false},
                                 {"error", QJsonObject{{"message", "unknown method"}}}});
                else
                    ws.sendJson({{"type", "res"}, {"id", id}, {"ok", true},
                                 {"payload", QJsonObject{{"sessionKey", sessionKey},
                                                         {"messages", history}}}});
            } else if (method == "chat.send") {
                ws.sendJson({{"type", "res"}, {"id", id}, {"ok", true},
                             {"payload", QJsonObject{{"runId", "run-1"}}}});
            }
        };
    }

    int count(const QString &method) const {
        int n = 0;
        for (const QJsonObject &r : requests) n += (r.value("method").toString() == method);
        return n;
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
        chat.setReconnectDelays({50});
        QSignalSpy lost(&chat, &ChatService::connectionError);
        QSignalSpy retries(&chat, &ChatService::reconnectScheduled);
        chat.configure(httpUrl(gw.ws), "wrong");
        chat.connectToGateway();
        QTRY_COMPARE(lost.count(), 1);
        QVERIFY(lost.at(0).at(0).toString().contains("rejected the token"));
        QCOMPARE(lost.at(0).at(1).toBool(), false);   // the same token would be refused again
        QCOMPARE(chat.state(), ChatService::State::Disconnected);
        QTest::qWait(300);
        QCOMPARE(retries.count(), 0);
        QCOMPARE(gw.ws.connectionCount, 1);
    }

    void reconnectsAfterADrop() {
        FakeGateway gw;
        ChatService chat;
        chat.setReconnectDelays({50, 100});
        QSignalSpy lost(&chat, &ChatService::connectionError);
        QSignalSpy retries(&chat, &ChatService::reconnectScheduled);
        QSignalSpy turnErrors(&chat, &ChatService::errorOccurred);
        QVERIFY(connectTo(chat, gw));

        gw.ws.dropConnection();   // no turn in flight
        QTRY_COMPARE(lost.count(), 1);
        QCOMPARE(lost.at(0).at(1).toBool(), true);
        QCOMPARE(retries.count(), 1);
        QCOMPARE(turnErrors.count(), 0);   // nothing for the transcript
        QTRY_VERIFY(chat.isConnected());
        QCOMPARE(gw.ws.connectionCount, 2);
    }

    void backsOffWhileTheGatewayIsDown() {
        FakeGateway gw;
        ChatService chat;
        chat.setReconnectDelays({30, 60, 90});
        QSignalSpy retries(&chat, &ChatService::reconnectScheduled);
        QVERIFY(connectTo(chat, gw));

        gw.ws.stopListening();
        gw.ws.dropConnection();
        QTRY_VERIFY(retries.count() >= 4);
        QCOMPARE(retries.at(0).at(0).toInt(), 30);
        QCOMPARE(retries.at(1).at(0).toInt(), 60);
        QCOMPARE(retries.at(2).at(0).toInt(), 90);
        QCOMPARE(retries.at(3).at(0).toInt(), 90);   // the last delay repeats

        QVERIFY(gw.ws.listenAgain());
        QTRY_VERIFY(chat.isConnected());
        // Connected again: the next drop starts from the shortest delay.
        const int before = retries.count();
        gw.ws.dropConnection();
        QTRY_COMPARE(retries.count(), before + 1);
        QCOMPARE(retries.last().at(0).toInt(), 30);
    }

    void doesNotReconnectAfterDisconnecting() {
        FakeGateway gw;
        ChatService chat;
        chat.setReconnectDelays({20});
        QSignalSpy retries(&chat, &ChatService::reconnectScheduled);
        QVERIFY(connectTo(chat, gw));
        chat.disconnectFromGateway();
        QTest::qWait(200);
        QCOMPARE(retries.count(), 0);
        QCOMPARE(gw.ws.connectionCount, 1);
    }

    void queuedMessageSurvivesAReconnect() {
        FakeGateway gw;
        ChatService chat;
        chat.setReconnectDelays({50});
        QVERIFY(connectTo(chat, gw));
        QTRY_COMPARE(chat.sessionKey(), gw.sessionKey);
        gw.ws.stopListening();
        gw.ws.dropConnection();
        QTRY_VERIFY(!chat.isConnected());

        chat.sendChatMessage("while down");   // waits for the connection to come back
        QTest::qWait(150);
        QCOMPARE(gw.count("chat.send"), 0);
        QVERIFY(gw.ws.listenAgain());
        QTRY_COMPARE(gw.count("chat.send"), 1);
        QCOMPARE(gw.lastRequest("chat.send").value("params")["message"].toString(), QString("while down"));
    }

    void loadsHistoryOnce() {
        FakeGateway gw;
        gw.history = QJsonArray{
            QJsonObject{{"role", "user"}, {"content", "earlier question"}},
            QJsonObject{{"role", "assistant"}, {"content", QJsonArray{
                QJsonObject{{"type", "text"}, {"text", "earlier answer"}}}}},
            QJsonObject{{"role", "toolResult"}, {"content", "ls output"}},
            QJsonObject{{"role", "assistant"}, {"content", QJsonArray{
                QJsonObject{{"type", "tool_use"}, {"name", "bash"}}}}},   // no text: skipped
        };
        ChatService chat;
        chat.setReconnectDelays({50});
        QSignalSpy history(&chat, &ChatService::historyLoaded);
        QVERIFY(connectTo(chat, gw));
        QTRY_COMPARE(history.count(), 1);
        QCOMPARE(gw.lastRequest("chat.history").value("params")["sessionKey"].toString(), gw.sessionKey);

        const auto entries = history.at(0).at(0).value<QList<ChatHistoryEntry>>();
        QCOMPARE(entries.size(), 2);
        QVERIFY(entries.at(0).fromUser);
        QCOMPARE(entries.at(0).text, QString("earlier question"));
        QVERIFY(!entries.at(1).fromUser);
        QCOMPARE(entries.at(1).text, QString("earlier answer"));

        // After a reconnect the transcript on screen is the live one: no second history.
        gw.ws.dropConnection();
        QTRY_COMPARE(gw.ws.connectionCount, 2);
        QTRY_VERIFY(chat.isConnected());
        QTRY_COMPARE(gw.count("sessions.list"), 2);
        QTest::qWait(100);
        QCOMPARE(gw.count("chat.history"), 1);
        QCOMPARE(history.count(), 1);
    }

    void failedHistoryDoesNotBlockTheQueue() {
        FakeGateway gw;
        gw.historyFails = true;
        ChatService chat;
        QSignalSpy history(&chat, &ChatService::historyLoaded);
        chat.configure(httpUrl(gw.ws), "tok");
        chat.sendChatMessage("hello");
        QTRY_COMPARE(gw.count("chat.send"), 1);
        QTest::qWait(100);
        QCOMPARE(history.count(), 0);
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

Q_DECLARE_METATYPE(QList<ChatHistoryEntry>)

QTEST_GUILESS_MAIN(TstChatService)
#include "tst_chatservice.moc"
