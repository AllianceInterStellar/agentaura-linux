#pragma once

#include <QDialog>

#include "models/Claw.h"
#include "services/ChatService.h"

class QBoxLayout;
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
    /// What a bubble holds: the user's own words (plain text), the agent's reply (Markdown), or
    /// a notice from the app itself such as a failed turn (plain text, on the agent's side).
    enum class Bubble { User, Agent, Notice };

    /// A bubble in its row; `at` is the layout index to insert it at, -1 for the end.
    QLabel *addBubble(const QString &text, Bubble kind, int at = -1);
    static void setBubbleText(QLabel *bubble, const QString &text, Bubble kind);
    void showHistory(const QList<ChatHistoryEntry> &entries);
    void setStatus(const QString &text, const QColor &color);
    void setComposerEnabled(bool enabled);
    void scrollToBottom();
    void onSend();

    Claw m_claw;
    ChatService *m_chat;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_messagesWidget = nullptr;
    QVBoxLayout *m_messagesLayout = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_reconnectButton = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_sendButton = nullptr;

    QLabel *m_streamingBubble = nullptr;   ///< agent bubble being filled by deltas
    bool m_turnActive = false;             ///< a reply is in flight; deltas belong to it
    bool m_hasLiveMessages = false;        ///< anything sent or received in this window
};
