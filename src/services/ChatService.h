#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include "services/WebSocketClient.h"

/// One message from the conversation's past, as chat.history reports it.
struct ChatHistoryEntry {
    bool fromUser = false;
    QString text;
};

/// Talks the OpenClaw gateway protocol (v3) over WebSocketClient.
///
/// Frame shapes, mirrored from the Android client so both stay on the same wire format:
///   - server → `{"type":"event","event":"connect.challenge","payload":{"nonce":...}}`
///   - client → `{"type":"req","id":N,"method":"connect","params":{...,"auth":{"token":...}}}`
///   - server → `{"type":"res","id":N,"ok":true,"payload":{"type":"hello-ok",...}}`
///   - chat streams back as `event:"chat"` payloads carrying state=delta|final|error|aborted.
///
/// A connection that drops — the gateway restarting, a network blip, a laptop waking up — is
/// re-established by itself with a growing delay, until the owner calls disconnectFromGateway()
/// or the gateway refuses the token (retrying a refused token would only be refused again).
class ChatService : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Authenticating, Connected };
    Q_ENUM(State)

    explicit ChatService(QObject *parent = nullptr);

    void configure(const QString &gatewayUrl, const QString &token);
    void connectToGateway();
    void disconnectFromGateway();
    /// Skip the wait before the next automatic attempt (a "Reconnect" button).
    void reconnectNow();

    State state() const { return m_state; }
    bool isConnected() const { return m_state == State::Connected; }
    QString sessionKey() const { return m_sessionKey; }

    /// Queues the prompt if the socket is still coming up, so the UI never has to gate on connect.
    void sendChatMessage(const QString &message, const QString &model = QString());

    /// The waits between reconnect attempts, in ms; the last one repeats. Tests shorten them.
    void setReconnectDelays(const QList<int> &delaysMs) { m_reconnectDelays = delaysMs; }

signals:
    void stateChanged(ChatService::State state);
    /// Cumulative assistant text for the in-flight turn (not an incremental chunk).
    void deltaReceived(const QString &text);
    void messageCompleted(const QString &text);
    /// The turn in flight failed (the gateway's error, a refused send, a timeout, the connection
    /// dropping under it). Always ends the turn.
    void errorOccurred(const QString &message);
    /// The connection itself failed or dropped. No turn is involved unless errorOccurred also
    /// fires; `willRetry` says whether an automatic reconnect is scheduled (see reconnectScheduled).
    void connectionError(const QString &message, bool willRetry);
    /// The next reconnect attempt starts in `delayMs`.
    void reconnectScheduled(int delayMs);
    /// The conversation so far, oldest first — delivered once, after the first connection that
    /// could fetch it.
    void historyLoaded(const QList<ChatHistoryEntry> &entries);
    /// How many turns this agent is running, counting every client's (gateway broadcast).
    void runStatusChanged(int runningCount);

public:
    /// Sessions this agent is working in right now — shared by every client (see run.status).
    QSet<QString> runningSessionIds() const { return m_runningSessionIds; }
    int runningAgentCount() const { return m_runningAgentCount; }
    /// Sessions another program has open: visible, but not ours to stop.
    QSet<QString> externalSessionIds() const { return m_externalSessionIds; }

    /// Flattens gateway message content (string | {content|text} | content-block array) to text.
    static QString parseMessageContent(const QJsonValue &raw);
    /// The user and assistant messages of a chat.history payload, oldest first.
    static QList<ChatHistoryEntry> parseHistory(const QJsonValue &payload);

private:
    QSet<QString> m_runningSessionIds;
    int m_runningAgentCount = 0;
    QSet<QString> m_externalSessionIds;
    void onSocketConnected();
    void onSocketText(const QString &raw);
    void onSocketClosed();
    void onSocketError(const QString &message);

    void setState(State s);
    void sendAuth();
    int sendRequest(const QString &method, const QJsonObject &params);
    void handleEvent(const QJsonObject &frame);
    void handleResponse(const QJsonObject &frame);
    void handleChatPayload(const QJsonObject &payload);
    void flushPendingMessage();

    /// End the in-flight turn with an error (no-op when no turn is running). Every path that
    /// kills a turn without a chat error frame goes through here, so the composer is always
    /// handed back to the user.
    void failTurn(const QString &message);
    /// The connection is gone: end any turn, go Disconnected, report, and retry if allowed.
    void connectionLost(const QString &message);
    void scheduleReconnect();

    WebSocketClient m_ws;
    QString m_gatewayUrl;
    QString m_token;
    QString m_sessionKey = "agent:main:main";
    State m_state = State::Disconnected;

    int m_reqCounter = 0;
    int m_sessionsListReqId = -1;   ///< id of the in-flight sessions.list, -1 when none
    int m_historyReqId = -1;        ///< id of the in-flight chat.history, -1 when none
    int m_chatSendReqId = -1;       ///< id of the in-flight chat.send, -1 when none
    bool m_historyDelivered = false;
    QString m_pendingMessage;       ///< prompt typed before the socket finished connecting
    QString m_pendingModel;

    QString m_activeRunId;          ///< first runId seen this turn; later runs are ignored
    QString m_lastContent;
    bool m_awaitingReply = false;
    QTimer m_connectTimeout;
    QTimer m_turnTimeout;           ///< outlasts the server-side timeoutMs, so a silent gateway
                                    ///< still ends the turn instead of wedging the composer

    bool m_autoReconnect = false;   ///< on while the owner wants a connection
    int m_reconnectAttempt = 0;
    QList<int> m_reconnectDelays{1000, 2000, 5000, 10000, 30000};
    QTimer m_reconnectTimer;
};
