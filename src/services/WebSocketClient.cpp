#include "services/WebSocketClient.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QTcpSocket>

namespace {
// RFC 6455 §1.3 — the magic GUID the server appends to the client key.
const char kWsGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

enum Opcode : quint8 {
    OpContinuation = 0x0,
    OpText = 0x1,
    OpBinary = 0x2,
    OpClose = 0x8,
    OpPing = 0x9,
    OpPong = 0xA,
};
}  // namespace

WebSocketClient::WebSocketClient(QObject *parent) : QObject(parent) {}

WebSocketClient::~WebSocketClient() {
    if (m_socket) m_socket->abort();
}

void WebSocketClient::open(const QUrl &url, const QString &origin) {
    close();

    m_url = url;
    m_origin = origin;
    m_secure = (url.scheme().compare("wss", Qt::CaseInsensitive) == 0);
    m_buffer.clear();
    m_fragment.clear();
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
        const bool wasUp = (m_state == Established);
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

    m_expectedAccept =
        QCryptographicHash::hash(key + kWsGuid, QCryptographicHash::Sha1).toBase64();

    QString path = m_url.path();
    if (path.isEmpty()) path = "/";
    if (m_url.hasQuery()) path += "?" + m_url.query();

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
    if (end < 0) return false;

    const QByteArray header = m_buffer.left(end);
    m_buffer.remove(0, end + 4);

    const QList<QByteArray> lines = header.split('\n');
    if (lines.isEmpty() || !lines.first().contains("101")) {
        emit errorOccurred("WebSocket upgrade rejected: " + QString::fromUtf8(lines.value(0)).trimmed());
        m_socket->abort();
        m_state = Idle;
        return false;
    }

    // Verify Sec-WebSocket-Accept so we don't talk framing to something that isn't a WS server.
    QByteArray accept;
    for (const QByteArray &line : lines) {
        const QByteArray l = line.trimmed();
        if (l.startsWith("Sec-WebSocket-Accept:") || l.startsWith("sec-websocket-accept:")) {
            accept = l.mid(l.indexOf(':') + 1).trimmed();
            break;
        }
    }
    if (accept != m_expectedAccept) {
        emit errorOccurred("WebSocket handshake failed (bad Sec-WebSocket-Accept)");
        m_socket->abort();
        m_state = Idle;
        return false;
    }

    m_state = Established;
    emit connected();
    return true;
}

void WebSocketClient::parseFrames() {
    forever {
        if (m_buffer.size() < 2) return;

        const quint8 b0 = static_cast<quint8>(m_buffer.at(0));
        const quint8 b1 = static_cast<quint8>(m_buffer.at(1));
        const bool fin = b0 & 0x80;
        const quint8 opcode = b0 & 0x0F;
        const bool masked = b1 & 0x80;         // servers must NOT mask, but tolerate it
        quint64 len = b1 & 0x7F;

        int offset = 2;
        if (len == 126) {
            if (m_buffer.size() < offset + 2) return;
            len = (static_cast<quint8>(m_buffer.at(2)) << 8) | static_cast<quint8>(m_buffer.at(3));
            offset += 2;
        } else if (len == 127) {
            if (m_buffer.size() < offset + 8) return;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | static_cast<quint8>(m_buffer.at(offset + i));
            offset += 8;
        }

        QByteArray mask;
        if (masked) {
            if (m_buffer.size() < offset + 4) return;
            mask = m_buffer.mid(offset, 4);
            offset += 4;
        }

        if (static_cast<quint64>(m_buffer.size()) < offset + len) return;   // frame still arriving

        QByteArray payload = m_buffer.mid(offset, static_cast<int>(len));
        m_buffer.remove(0, offset + static_cast<int>(len));

        if (masked) {
            for (int i = 0; i < payload.size(); ++i)
                payload[i] = payload[i] ^ mask[i % 4];
        }

        switch (opcode) {
        case OpPing:
            writeFrame(OpPong, payload);
            break;
        case OpPong:
            break;
        case OpClose:
            m_state = Closing;
            writeFrame(OpClose, QByteArray());
            m_socket->disconnectFromHost();
            return;
        case OpContinuation:
            m_fragment += payload;
            if (fin) {
                if (m_fragmentOpcode == OpText) emit textMessageReceived(QString::fromUtf8(m_fragment));
                m_fragment.clear();
                m_fragmentOpcode = 0;
            }
            break;
        case OpText:
        case OpBinary:
            if (fin) {
                if (opcode == OpText) emit textMessageReceived(QString::fromUtf8(payload));
            } else {
                m_fragmentOpcode = opcode;
                m_fragment = payload;
            }
            break;
        default:
            break;
        }
    }
}

void WebSocketClient::writeFrame(quint8 opcode, const QByteArray &payload) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;

    QByteArray frame;
    frame.append(static_cast<char>(0x80 | opcode));   // FIN + opcode

    const int n = payload.size();
    if (n < 126) {
        frame.append(static_cast<char>(0x80 | n));    // MASK + length
    } else if (n <= 0xFFFF) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((n >> 8) & 0xFF));
        frame.append(static_cast<char>(n & 0xFF));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            frame.append(static_cast<char>((static_cast<quint64>(n) >> (i * 8)) & 0xFF));
    }

    // Every client frame must carry a fresh 4-byte mask.
    char mask[4];
    for (int i = 0; i < 4; ++i) mask[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    frame.append(mask, 4);

    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i) masked[i] = masked[i] ^ mask[i % 4];
    frame += masked;

    m_socket->write(frame);
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
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_state = Idle;
    m_buffer.clear();
    m_fragment.clear();
}

void WebSocketClient::onSocketError(QAbstractSocket::SocketError) {
    if (!m_socket) return;
    emit errorOccurred(m_socket->errorString());
    m_state = Idle;
}
