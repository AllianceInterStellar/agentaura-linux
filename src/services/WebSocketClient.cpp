#include "services/WebSocketClient.h"
#include "services/WebSocketFrame.h"

#include <QRandomGenerator>
#include <QSslSocket>
#include <QTcpSocket>

using namespace WebSocketFrame;

WebSocketClient::WebSocketClient(QObject *parent) : QObject(parent) {}

WebSocketClient::~WebSocketClient() {
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
}

void WebSocketClient::open(const QUrl &url, const QString &origin) {
    close();

    m_url = url;
    m_origin = origin;
    m_secure = (url.scheme().compare("wss", Qt::CaseInsensitive) == 0);
    m_buffer.clear();
    m_fragment.clear();
    m_fragmentOpcode = 0;
    m_state = Connecting;

    if (m_secure) {
        auto *ssl = new QSslSocket(this);
        m_socket = ssl;
        connect(ssl, &QSslSocket::encrypted, this, &WebSocketClient::onSocketConnected);
    } else {
        m_socket = new QTcpSocket(this);
        connect(m_socket, &QTcpSocket::connected, this, &WebSocketClient::onSocketConnected);
    }

    connect(m_socket, &QTcpSocket::readyRead, this, &WebSocketClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &WebSocketClient::onSocketError);
    connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
        // Closing counts as "was up": a server-initiated close parks the state there before the
        // TCP connection goes, and leaving it out meant a close frame never reached the owner —
        // the chat kept claiming to be connected until its turn timed out.
        const bool wasUp = (m_state == Established || m_state == Closing);
        m_state = Idle;
        if (wasUp) emit disconnected();
    });

    const quint16 port = url.port(m_secure ? 443 : 80);
    if (m_secure) {
        static_cast<QSslSocket *>(m_socket)->connectToHostEncrypted(url.host(), port);
    } else {
        m_socket->connectToHost(url.host(), port);
    }
}

void WebSocketClient::onSocketConnected() {
    // Client key is 16 random bytes, base64'd (RFC 6455 §4.1).
    QByteArray raw(16, 0);
    for (int i = 0; i < 16; ++i) raw[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    const QByteArray key = raw.toBase64();
    m_expectedAccept = acceptKey(key);

    QString path = m_url.path(QUrl::FullyEncoded);
    if (path.isEmpty()) path = "/";
    if (m_url.hasQuery()) path += "?" + m_url.query(QUrl::FullyEncoded);

    const quint16 port = m_url.port(m_secure ? 443 : 80);
    const bool defaultPort = (m_secure && port == 443) || (!m_secure && port == 80);
    const QString host = defaultPort ? m_url.host() : QString("%1:%2").arg(m_url.host()).arg(port);

    QByteArray req;
    req += "GET " + path.toUtf8() + " HTTP/1.1\r\n";
    req += "Host: " + host.toUtf8() + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + key + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    if (!m_origin.isEmpty()) req += "Origin: " + m_origin.toUtf8() + "\r\n";
    req += "\r\n";

    m_socket->write(req);
    m_state = HandshakeSent;
}

void WebSocketClient::onReadyRead() {
    m_buffer += m_socket->readAll();

    if (m_state == HandshakeSent) {
        if (!readHandshake()) return;   // wait for the rest of the response headers
    }
    if (m_state == Established || m_state == Closing) parseFrames();
}

bool WebSocketClient::readHandshake() {
    const int end = m_buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        // Headers that never end are not a WebSocket server either.
        if (m_buffer.size() > 64 * 1024) fatal("WebSocket handshake response too large");
        return false;
    }

    const QByteArray header = m_buffer.left(end);
    m_buffer.remove(0, end + 4);

    const QList<QByteArray> lines = header.split('\n');
    // "HTTP/1.1 101 Switching Protocols" — the status is the second token, not just any "101".
    const QList<QByteArray> status = lines.value(0).trimmed().split(' ');
    if (status.size() < 2 || !status.at(0).startsWith("HTTP/") || status.at(1) != "101") {
        fatal("WebSocket upgrade rejected: " + QString::fromUtf8(lines.value(0)).trimmed());
        return false;
    }

    // Verify Sec-WebSocket-Accept so we don't talk framing to something that isn't a WS server.
    // Header names are case-insensitive (RFC 7230 §3.2).
    QByteArray accept;
    for (const QByteArray &line : lines) {
        const int colon = line.indexOf(':');
        if (colon <= 0) continue;
        if (line.left(colon).trimmed().toLower() == "sec-websocket-accept") {
            accept = line.mid(colon + 1).trimmed();
            break;
        }
    }
    if (accept != m_expectedAccept) {
        fatal("WebSocket handshake failed (bad Sec-WebSocket-Accept)");
        return false;
    }

    m_state = Established;
    emit connected();
    return true;
}

void WebSocketClient::parseFrames() {
    // m_socket is checked on every pass: a slot connected to textMessageReceived may close or
    // reopen this client, and the buffer then belongs to a connection that no longer exists.
    while (m_socket && (m_state == Established || m_state == Closing)) {
        Frame frame;
        QString error;
        const Result r = parse(m_buffer, frame, &error);
        if (r == Result::NeedMore) return;
        if (r == Result::Error) {
            fatal(error);
            return;
        }

        switch (frame.opcode) {
        case OpPing:
            writeFrame(OpPong, frame.payload);
            break;
        case OpPong:
            break;
        case OpClose:
            if (m_state == Established) writeFrame(OpClose, QByteArray());
            m_state = Closing;
            m_socket->disconnectFromHost();
            return;
        case OpContinuation:
            if (m_fragmentOpcode == 0) {
                fatal("Unexpected WebSocket continuation frame");
                return;
            }
            if (m_fragment.size() + frame.payload.size() > kMaxMessageBytes) {
                fatal("WebSocket message too large");
                return;
            }
            m_fragment += frame.payload;
            if (frame.fin) {
                const bool text = (m_fragmentOpcode == OpText);
                const QByteArray message = m_fragment;
                m_fragment.clear();
                m_fragmentOpcode = 0;
                if (text) emit textMessageReceived(QString::fromUtf8(message));
            }
            break;
        case OpText:
        case OpBinary:
            if (frame.fin) {
                if (frame.opcode == OpText) emit textMessageReceived(QString::fromUtf8(frame.payload));
            } else {
                m_fragmentOpcode = frame.opcode;
                m_fragment = frame.payload;
            }
            break;
        default:
            break;
        }
    }
}

void WebSocketClient::writeFrame(quint8 opcode, const QByteArray &payload) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
    m_socket->write(encode(opcode, payload, /*mask=*/true));
}

void WebSocketClient::sendText(const QString &text) {
    if (m_state != Established) {
        emit errorOccurred("Not connected");
        return;
    }
    writeFrame(OpText, text.toUtf8());
}

void WebSocketClient::close() {
    if (m_socket) {
        if (m_state == Established) writeFrame(OpClose, QByteArray());
        // A close we asked for is not a dropped connection: detach first so abort() does not
        // report it as one.
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_state = Idle;
    m_buffer.clear();
    m_fragment.clear();
    m_fragmentOpcode = 0;
}

void WebSocketClient::fatal(const QString &message) {
    // Tear down as close() does, then report: a peer that broke the protocol gets no more reads.
    const bool wasUp = (m_state == Established || m_state == Closing);
    close();
    emit errorOccurred(message);
    if (wasUp) emit disconnected();
}

void WebSocketClient::onSocketError(QAbstractSocket::SocketError error) {
    if (!m_socket) return;
    // The server closing the connection is how every session ends, not an error; `disconnected`
    // reports it.
    if (error == QAbstractSocket::RemoteHostClosedError &&
        (m_state == Established || m_state == Closing))
        return;
    emit errorOccurred(m_socket->errorString());
    m_state = Idle;
}
