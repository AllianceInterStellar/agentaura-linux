#pragma once

#include <QByteArray>
#include <QString>

/// RFC 6455 framing, with no socket attached: WebSocketClient feeds it bytes and the tests feed it
/// hand-built frames, so the rules that decide what the client accepts are checked on their own.
namespace WebSocketFrame {

enum Opcode : quint8 {
    OpContinuation = 0x0,
    OpText = 0x1,
    OpBinary = 0x2,
    OpClose = 0x8,
    OpPing = 0x9,
    OpPong = 0xA,
};

/// The largest message the client will hold, whether it arrives in one frame or in fragments.
/// A chat reply is a few hundred KB at most; a length field claiming more is a broken or hostile
/// peer, and honouring it would mean buffering up to 2^63 bytes on its word.
constexpr qint64 kMaxMessageBytes = 16 * 1024 * 1024;

struct Frame {
    quint8 opcode = 0;
    bool fin = true;
    QByteArray payload;   ///< already unmasked
};

enum class Result { Frame, NeedMore, Error };

/// Take one complete frame off the front of `buffer`. On `NeedMore` the buffer is untouched; on
/// `Error` `error` says why and the connection has to be dropped (the stream can no longer be
/// trusted to be in step).
Result parse(QByteArray &buffer, Frame &out, QString *error = nullptr);

/// One unfragmented frame. Client frames must be masked (RFC 6455 §5.3), server frames must not.
QByteArray encode(quint8 opcode, const QByteArray &payload, bool mask);

/// base64(sha1(key + GUID)) — the Sec-WebSocket-Accept a server has to answer `clientKey` with.
QByteArray acceptKey(const QByteArray &clientKey);

}  // namespace WebSocketFrame
