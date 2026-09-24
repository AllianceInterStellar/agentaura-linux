#include "services/ChatService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

ChatService::ChatService(QObject *parent) : QObject(parent) {
    connect(&m_ws, &WebSocketClient::connected, this, &ChatService::onSocketConnected);
    connect(&m_ws, &WebSocketClient::textMessageReceived, this, &ChatService::onSocketText);
    connect(&m_ws, &WebSocketClient::disconnected, this, &ChatService::onSocketClosed);
    connect(&m_ws, &WebSocketClient::errorOccurred, this, [this](const QString &e) {
        setState(State::Disconnected);
        m_turnTimeout.stop();
        m_awaitingReply = false;
        emit errorOccurred(e);
    });

    m_connectTimeout.setSingleShot(true);
    m_connectTimeout.setInterval(15000);
    connect(&m_connectTimeout, &QTimer::timeout, this, [this]() {
        if (m_state != State::Connected) {
            m_ws.close();
            setState(State::Disconnected);
            emit errorOccurred("Gateway connection timed out");
        }
    });

    // Idle watchdog, NOT a total-turn deadline: it is restarted on every delta (see below), so a
    // long answer that keeps streaming is never cut off, and it fires only when the gateway has
    // gone silent. As a total deadline it killed working turns that simply ran past 130s.
    m_turnTimeout.setSingleShot(true);
    m_turnTimeout.setInterval(130000);
    connect(&m_turnTimeout, &QTimer::timeout, this, [this]() {
        failTurn(QStringLiteral("The agent didn't reply — the request timed out"));
    });
}

void ChatService::configure(const QString &gatewayUrl, const QString &token) {
    m_gatewayUrl = gatewayUrl;
    m_token = token;
}

void ChatService::connectToGateway() {
    if (m_gatewayUrl.isEmpty()) {
        emit errorOccurred("No gateway URL for this instance");
        return;
    }
    if (m_state == State::Connecting || m_state == State::Authenticating || m_state == State::Connected)
        return;

    QString wsUrl = m_gatewayUrl;
    wsUrl.replace("https://", "wss://").replace("http://", "ws://");

    setState(State::Connecting);
    m_connectTimeout.start();
    // The gateway checks Origin; it only accepts the web console's.
    m_ws.open(QUrl(wsUrl), "https://clawhost.cloud");
}

void ChatService::disconnectFromGateway() {
    m_connectTimeout.stop();
    m_turnTimeout.stop();
    m_ws.close();
    m_awaitingReply = false;
    m_pendingMessage.clear();
    setState(State::Disconnected);
}

void ChatService::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

void ChatService::onSocketConnected() {
    // Nothing to do yet — the gateway opens with connect.challenge, and we authenticate then.
    setState(State::Authenticating);
}

void ChatService::onSocketClosed() {
    m_connectTimeout.stop();
    // A clean close mid-turn carries no chat error frame, so nothing else would ever end the
    // turn and the composer would stay disabled for the life of the window.
    failTurn(QStringLiteral("Connection lost"));
    setState(State::Disconnected);
}

void ChatService::failTurn(const QString &message) {
    m_turnTimeout.stop();
    m_chatSendReqId = -1;
    if (!m_awaitingReply) return;
    m_awaitingReply = false;
    m_activeRunId.clear();
    emit errorOccurred(message);
}

void ChatService::sendAuth() {
    QJsonObject client{
        {"id", "gateway-client"},
        {"displayName", "clawhost-chat"},
        {"version", "1.0.0"},
        {"platform", "desktop"},
        {"mode", "ui"},
        {"instanceId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
    };
    QJsonObject params{
        {"minProtocol", 3},
        {"maxProtocol", 3},
        {"client", client},
        {"role", "operator"},
        {"scopes", QJsonArray{"operator.admin", "operator.read", "operator.write"}},
        {"caps", QJsonArray{"tool-events"}},
        {"auth", QJsonObject{{"token", m_token}}},
    };
    sendRequest("connect", params);
}

int ChatService::sendRequest(const QString &method, const QJsonObject &params) {
    const int id = ++m_reqCounter;
    const QJsonObject frame{
        {"type", "req"},
        {"id", id},
        {"method", method},
        {"params", params},
    };
    m_ws.sendText(QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
    return id;
}

void ChatService::onSocketText(const QString &raw) {
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isObject()) return;
    const QJsonObject frame = doc.object();

    const QString type = frame.value("type").toString();
    if (type == "event") handleEvent(frame);
    else if (type == "res") handleResponse(frame);
}

void ChatService::handleEvent(const QJsonObject &frame) {
    const QString event = frame.value("event").toString();
    const QJsonObject payload = frame.value("payload").toObject();

    if (event == "connect.challenge") {
        setState(State::Authenticating);
        sendAuth();
    } else if (event == "chat") {
        handleChatPayload(payload);
    } else if (event == "run.status") {
        // Who is working right now, on THIS agent, from ANY client. The gateway broadcasts this
        // to every open socket, so a turn started on the phone is visible here and the other way
        // round. Without it this client knew only about its own turns and showed the agent idle
        // while it was busy.
        QSet<QString> running;
        const QJsonArray runArr = payload.value("running").toArray();
        for (const QJsonValue &v : runArr) {
            const QString id = v.toString();
            if (!id.isEmpty()) running.insert(id);
        }
        m_runningSessionIds = running;
        m_runningAgentCount = payload.contains("runningCount")
            ? payload.value("runningCount").toInt()
            : payload.value("count").toInt(running.size());
        // Conversations another program has open — a terminal, an editor sidebar, Devin. The
        // gateway can see these but cannot stop them: every abort path needs a process it
        // spawned. An older gateway sends no such key, and absent must not read as "none".
        if (payload.contains("external")) {
            QSet<QString> ext;
            const QJsonArray extArr = payload.value("external").toArray();
            for (const QJsonValue &v : extArr) {
                const QString id = v.toString();
                if (!id.isEmpty()) ext.insert(id);
            }
            m_externalSessionIds = ext;
        }
        emit runStatusChanged(m_runningAgentCount);
    }
}

void ChatService::handleChatPayload(const QJsonObject &payload) {
    if (payload.value("sessionKey").toString() != m_sessionKey) return;

    // The gateway can emit a trailing delta after it has already sent final/error. Without this
    // gate the turn would look reopened and the UI would grow a phantom assistant message.
    if (!m_awaitingReply) return;

    const QString runId = payload.value("runId").toString();
    if (m_activeRunId.isEmpty()) m_activeRunId = runId;
    if (!runId.isEmpty() && runId != m_activeRunId) return;   // a stale run's tail

    const QString state = payload.value("state").toString();
    if (state == "delta") {
        const QString text = parseMessageContent(payload.value("message"));
        if (!text.isEmpty()) {
            m_lastContent = text;
            emit deltaReceived(text);
        }
        // Progress proves the gateway is alive — restart the silence watchdog.
        if (m_awaitingReply) m_turnTimeout.start();
    } else if (state == "final" || state == "aborted") {
        const QString text = parseMessageContent(payload.value("message"));
        if (!text.isEmpty()) m_lastContent = text;
        m_awaitingReply = false;
        m_activeRunId.clear();
        m_turnTimeout.stop();
        emit messageCompleted(m_lastContent);
    } else if (state == "error") {
        const QString msg = payload.value("errorMessage").toString();
        failTurn(msg.isEmpty() ? QStringLiteral("Unknown error") : msg);
    }
}

void ChatService::handleResponse(const QJsonObject &frame) {
    const bool ok = frame.value("ok").toBool();

    if (m_state == State::Authenticating) {
        const QJsonObject payload = frame.value("payload").toObject();
        if (ok && payload.value("type").toString() == "hello-ok") {
            m_connectTimeout.stop();
            // Ask for the session list BEFORE announcing Connected. stateChanged is emitted
            // synchronously, and a slot that sends a message right away would otherwise find
            // m_sessionsListReqId still unset, skip the queue, and talk to the default session
            // key instead of the agent's resumed one.
            m_sessionsListReqId = sendRequest("sessions.list", QJsonObject());
            setState(State::Connected);
            return;
        }
        if (!ok) {
            m_connectTimeout.stop();
            m_ws.close();
            setState(State::Disconnected);
            emit errorOccurred("Gateway rejected the token — try Reconnect on the instance");
            return;
        }
    }

    if (m_sessionsListReqId != -1 && frame.value("id").toInt() == m_sessionsListReqId) {
        m_sessionsListReqId = -1;
        if (ok) {
            const QJsonValue payload = frame.value("payload");
            QJsonArray sessions = payload.isArray() ? payload.toArray()
                                                    : payload.toObject().value("sessions").toArray();
            const QString prefix = "agent:main:";
            for (const QJsonValue &v : sessions) {
                const QJsonObject s = v.toObject();
                QString key = s.value("key").toString();
                if (key.isEmpty()) key = s.value("sessionKey").toString();
                if (key.startsWith(prefix)) { m_sessionKey = key; break; }
            }
        }
        // Whether or not the lookup found anything, the default key is valid — flush the queue.
        if (!m_pendingMessage.isEmpty()) {
            const QString msg = m_pendingMessage;
            const QString model = m_pendingModel;
            m_pendingMessage.clear();
            m_pendingModel.clear();
            sendChatMessage(msg, model);
        }
        return;
    }

    if (m_chatSendReqId != -1 && frame.value("id").toInt() == m_chatSendReqId) {
        m_chatSendReqId = -1;
        // A refused chat.send streams nothing back, so no chat event will ever close this turn.
        if (!ok) {
            const QString msg = frame.value("error").toObject().value("message").toString();
            failTurn(msg.isEmpty() ? QStringLiteral("The gateway rejected the message") : msg);
        }
    }
}

void ChatService::sendChatMessage(const QString &message, const QString &model) {
    if (message.trimmed().isEmpty()) return;

    // Hold the prompt until the handshake and session lookup finish; the queue is flushed
    // once sessions.list answers, so the user can type immediately after opening chat.
    if (m_state != State::Connected || m_sessionsListReqId != -1) {
        m_pendingMessage = message;
        m_pendingModel = model;
        if (m_state == State::Disconnected) connectToGateway();
        return;
    }

    m_activeRunId.clear();
    m_lastContent.clear();
    m_awaitingReply = true;

    QJsonObject params{
        {"sessionKey", m_sessionKey},
        {"message", message},
        {"deliver", true},
        {"timeoutMs", 120000},
        {"idempotencyKey", QUuid::createUuid().toString(QUuid::WithoutBraces)},
    };
    if (!model.isEmpty()) params["model"] = model;
    m_chatSendReqId = sendRequest("chat.send", params);
    m_turnTimeout.start();
}

QString ChatService::parseMessageContent(const QJsonValue &raw) {
    if (raw.isString()) return raw.toString();

    if (raw.isObject()) {
        const QJsonObject o = raw.toObject();
        if (o.contains("content")) return parseMessageContent(o.value("content"));
        if (o.contains("text")) return o.value("text").toString();
        return {};
    }

    if (raw.isArray()) {
        QString out;
        for (const QJsonValue &v : raw.toArray()) {
            const QJsonObject block = v.toObject();
            if (block.value("type").toString() == "text") out += block.value("text").toString();
        }
        return out;
    }

    return {};
}
