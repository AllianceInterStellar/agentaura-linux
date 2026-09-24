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
/// Qt ships this as the QtWebSockets module, but this machine only has qtbase/qtdeclarative/qtsvg
/// and the available qtwebsockets bottle would drag qtbase to a newer version — which would risk
/// the working builds. Only the client half of the protocol is needed here: text frames, ping/pong,
/// and close. Client→server frames are always masked, as the RFC requires.
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
