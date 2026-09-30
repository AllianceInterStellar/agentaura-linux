#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

class QSslSocket;
class QTcpSocket;

/// Minimal RFC 6455 WebSocket client built on QSslSocket/QTcpSocket.
///
/// Qt ships this as the QtWebSockets module; doing without it keeps the package's dependencies to
/// qtbase. Only the client half of the protocol is needed here: text frames, ping/pong, and close.
/// The framing rules themselves live in WebSocketFrame, where the tests reach them.
///
/// Signals: `disconnected` fires once for a connection that was up and then went away, whoever
/// ended it — except a close() the owner asked for, which is silent. `errorOccurred` reports a
/// connection that failed to come up, or a peer that broke the protocol (followed by
/// `disconnected` if it had been up).
class WebSocketClient : public QObject {
    Q_OBJECT
public:
    explicit WebSocketClient(QObject *parent = nullptr);
    ~WebSocketClient() override;

    /// ws:// and wss:// are both accepted; `origin` is sent as the Origin header.
    void open(const QUrl &url, const QString &origin = QString());
    void sendText(const QString &text);
    void close();
    bool isConnected() const { return m_state == Established; }

signals:
    void connected();
    void disconnected();
    void textMessageReceived(const QString &message);
    void errorOccurred(const QString &message);

private:
    enum State { Idle, Connecting, HandshakeSent, Established, Closing };

    void onSocketConnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

    bool readHandshake();      ///< consumes the HTTP 101 response; false while incomplete
    void parseFrames();        ///< drains complete frames out of m_buffer
    void writeFrame(quint8 opcode, const QByteArray &payload);
    void fatal(const QString &message);   ///< drop the connection over a protocol violation

    QTcpSocket *m_socket = nullptr;   ///< a QSslSocket when the scheme is wss
    bool m_secure = false;
    QUrl m_url;
    QString m_origin;
    QByteArray m_expectedAccept;      ///< base64(sha1(key + RFC GUID))
    QByteArray m_buffer;
    QByteArray m_fragment;            ///< accumulates continuation frames
    quint8 m_fragmentOpcode = 0;
    State m_state = Idle;
};
