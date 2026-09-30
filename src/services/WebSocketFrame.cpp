#include "services/WebSocketFrame.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace WebSocketFrame {

namespace {
// RFC 6455 §1.3 — the magic GUID the server appends to the client key.
const char kWsGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

Result fail(QString *error, const QString &why) {
    if (error) *error = why;
    return Result::Error;
}
}  // namespace

Result parse(QByteArray &buffer, Frame &out, QString *error) {
    if (buffer.size() < 2) return Result::NeedMore;

    const quint8 b0 = static_cast<quint8>(buffer.at(0));
    const quint8 b1 = static_cast<quint8>(buffer.at(1));
    const bool fin = b0 & 0x80;
    const quint8 opcode = b0 & 0x0F;
    const bool masked = b1 & 0x80;   // servers must NOT mask, but tolerate it
    quint64 len = b1 & 0x7F;

    qint64 offset = 2;
    if (len == 126) {
        if (buffer.size() < offset + 2) return Result::NeedMore;
        len = (quint64(quint8(buffer.at(2))) << 8) | quint8(buffer.at(3));
        offset += 2;
    } else if (len == 127) {
        if (buffer.size() < offset + 8) return Result::NeedMore;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | quint8(buffer.at(offset + i));
        offset += 8;
    }

    // Checked before waiting for the payload: the length is the peer's claim, and waiting for a
    // claimed 2^63 bytes means buffering whatever it sends for ever.
    if (len > quint64(kMaxMessageBytes))
        return fail(error, QStringLiteral("WebSocket frame too large (%1 bytes)").arg(len));
    // Control frames are never fragmented and carry at most 125 bytes (§5.5).
    if (opcode >= 0x8 && (!fin || len > 125))
        return fail(error, QStringLiteral("Malformed WebSocket control frame"));

    char mask[4] = {0, 0, 0, 0};
    if (masked) {
        if (buffer.size() < offset + 4) return Result::NeedMore;
        for (int i = 0; i < 4; ++i) mask[i] = buffer.at(offset + i);
        offset += 4;
    }

    if (quint64(buffer.size() - offset) < len) return Result::NeedMore;   // still arriving

    QByteArray payload = buffer.mid(offset, qsizetype(len));
    buffer.remove(0, offset + qsizetype(len));
    if (masked) {
        for (qsizetype i = 0; i < payload.size(); ++i) payload[i] = char(payload[i] ^ mask[i % 4]);
    }

    out.opcode = opcode;
    out.fin = fin;
    out.payload = payload;
    return Result::Frame;
}

QByteArray encode(quint8 opcode, const QByteArray &payload, bool mask) {
    QByteArray frame;
    frame.append(static_cast<char>(0x80 | opcode));   // FIN + opcode

    const quint8 maskBit = mask ? 0x80 : 0x00;
    const quint64 n = quint64(payload.size());
    if (n < 126) {
        frame.append(static_cast<char>(maskBit | n));
    } else if (n <= 0xFFFF) {
        frame.append(static_cast<char>(maskBit | 126));
        frame.append(static_cast<char>((n >> 8) & 0xFF));
        frame.append(static_cast<char>(n & 0xFF));
    } else {
        frame.append(static_cast<char>(maskBit | 127));
        for (int i = 7; i >= 0; --i) frame.append(static_cast<char>((n >> (i * 8)) & 0xFF));
    }

    if (!mask) return frame + payload;

    // Every client frame carries a fresh 4-byte mask.
    char key[4];
    for (char &k : key) k = static_cast<char>(QRandomGenerator::global()->bounded(256));
    frame.append(key, 4);
    QByteArray masked = payload;
    for (qsizetype i = 0; i < masked.size(); ++i) masked[i] = char(masked[i] ^ key[i % 4]);
    return frame + masked;
}

QByteArray acceptKey(const QByteArray &clientKey) {
    return QCryptographicHash::hash(clientKey + kWsGuid, QCryptographicHash::Sha1).toBase64();
}

}  // namespace WebSocketFrame
