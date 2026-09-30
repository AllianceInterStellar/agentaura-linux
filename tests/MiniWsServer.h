// SPDX-License-Identifier: MIT
//
// A WebSocket server small enough to read in one sitting, for driving WebSocketClient and
// ChatService in tests: one client at a time, text frames, ping/close, and knobs for the ways a
// real server misbehaves. It uses the same WebSocketFrame code as the client, but on the server
// side of the rules (unmasked frames out, masked frames in).
#pragma once

#include "services/WebSocketFrame.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <functional>

class MiniWsServer {
public:
    /// What the next handshake answers with.
    enum class Handshake { Accept, Reject403, BadAcceptKey };

    MiniWsServer() {
        m_server.listen(QHostAddress::LocalHost);
        m_port = m_server.serverPort();
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
            while (QTcpSocket *s = m_server.nextPendingConnection()) {
                if (m_client) m_client->deleteLater();
                m_client = s;
                m_buffer.clear();
                m_upgraded = false;
                ++connectionCount;
                QObject::connect(s, &QTcpSocket::readyRead, &m_server, [this, s]() { onReadyRead(s); });
            }
        });
    }

    QUrl url(const QString &scheme = "ws") const {
        return QUrl(QStringLiteral("%1://127.0.0.1:%2/gw").arg(scheme).arg(m_port));
    }

    bool upgraded() const { return m_upgraded; }

    void sendFrame(quint8 opcode, const QByteArray &payload, bool fin = true) {
        QByteArray f = WebSocketFrame::encode(opcode, payload, /*mask=*/false);
        if (!fin) f[0] = char(quint8(f[0]) & 0x7F);
        sendRaw(f);
    }
    void sendText(const QString &text) { sendFrame(WebSocketFrame::OpText, text.toUtf8()); }
    void sendJson(const QJsonObject &obj) {
        sendText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    }
    void sendClose() { sendFrame(WebSocketFrame::OpClose, QByteArray("\x03\xe8", 2)); }
    void sendRaw(const QByteArray &bytes) {
        if (m_client) {
            m_client->write(bytes);
            m_client->flush();
        }
    }
    /// Drop the TCP connection with no close frame, as a crashed server or a lost network does.
    void dropConnection() {
        if (m_client) m_client->abort();
    }
    /// Stop accepting connections, then take them again on the same port — a gateway restarting.
    void stopListening() { m_server.close(); }
    bool listenAgain() { return m_server.listen(QHostAddress::LocalHost, m_port); }

    Handshake handshake = Handshake::Accept;
    int connectionCount = 0;
    QStringList received;          ///< text frames from the client, in order
    QList<QByteArray> pongs;       ///< pong payloads from the client
    bool clientSentClose = false;
    std::function<void()> onUpgraded;
    std::function<void(const QString &)> onText;

private:
    void onReadyRead(QTcpSocket *s) {
        if (s != m_client) return;
        m_buffer += s->readAll();
        if (!m_upgraded) {
            const int end = m_buffer.indexOf("\r\n\r\n");
            if (end < 0) return;
            const QByteArray head = m_buffer.left(end);
            m_buffer.remove(0, end + 4);
            QByteArray key;
            for (const QByteArray &line : head.split('\n')) {
                const int colon = line.indexOf(':');
                if (colon > 0 && line.left(colon).trimmed().toLower() == "sec-websocket-key")
                    key = line.mid(colon + 1).trimmed();
            }
            if (handshake == Handshake::Reject403) {
                s->write("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
                s->disconnectFromHost();
                return;
            }
            const QByteArray accept = handshake == Handshake::BadAcceptKey
                                          ? QByteArray("bm90IHRoZSByaWdodCBrZXk=")
                                          : WebSocketFrame::acceptKey(key);
            s->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                     "Connection: Upgrade\r\nsec-websocket-accept: " + accept + "\r\n\r\n");
            m_upgraded = true;
            if (onUpgraded) onUpgraded();
        }
        for (;;) {
            WebSocketFrame::Frame f;
            if (WebSocketFrame::parse(m_buffer, f) != WebSocketFrame::Result::Frame) return;
            switch (f.opcode) {
            case WebSocketFrame::OpText: {
                const QString text = QString::fromUtf8(f.payload);
                received << text;
                if (onText) onText(text);
                break;
            }
            case WebSocketFrame::OpPong: pongs << f.payload; break;
            case WebSocketFrame::OpClose: clientSentClose = true; break;
            default: break;
            }
            if (s != m_client) return;   // a callback replaced the connection
        }
    }

    QTcpServer m_server;
    QTcpSocket *m_client = nullptr;
    QByteArray m_buffer;
    bool m_upgraded = false;
    quint16 m_port = 0;
};
