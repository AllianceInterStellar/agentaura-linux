#pragma once

#include <QDialog>

#include "models/Claw.h"
#include "services/ChatService.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

/// Chat window for one instance. Streams from the gateway via ChatService instead of
/// handing the URL to a browser, so the desktop app is a real client like web/iOS/Android.
class ChatScreen : public QDialog {
    Q_OBJECT
public:
    explicit ChatScreen(const Claw &claw, QWidget *parent = nullptr);

private:
    void addMessage(const QString &text, bool fromUser);
    void setStatus(const QString &text, const QColor &color);
    void scrollToBottom();
    void onSend();

    Claw m_claw;
    ChatService *m_chat;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_messagesWidget = nullptr;
    QVBoxLayout *m_messagesLayout = nullptr;
    QLabel *m_status = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_sendButton = nullptr;

    QLabel *m_streamingBubble = nullptr;   ///< assistant bubble being filled by deltas
    bool m_turnActive = false;             ///< a reply is in flight; deltas belong to it
};
