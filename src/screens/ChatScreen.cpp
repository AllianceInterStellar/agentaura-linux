#include "screens/ChatScreen.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "services/ChatFormat.h"
#include "theme/AppColors.h"

ChatScreen::ChatScreen(const Claw &claw, QWidget *parent)
    : QDialog(parent), m_claw(claw), m_chat(new ChatService(this)) {
    setWindowTitle(tr("Chat — %1").arg(claw.name));
    resize(720, 640);
    setStyleSheet(AppColors::globalStyleSheet());

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- header -------------------------------------------------------------
    auto *header = new QWidget(this);
    header->setStyleSheet("background-color: #141416; border-bottom: 1px solid #2A2A2E;");
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 14, 20, 14);

    auto *title = new QLabel(claw.name, header);
    title->setTextFormat(Qt::PlainText);
    title->setStyleSheet("font-size: 15px; font-weight: bold; border: none;");
    headerLayout->addWidget(title);
    headerLayout->addStretch();

    m_status = new QLabel(header);
    m_status->setStyleSheet("font-size: 12px; border: none;");
    headerLayout->addWidget(m_status);

    m_reconnectButton = new QPushButton(tr("Reconnect"), header);
    m_reconnectButton->setCursor(Qt::PointingHandCursor);
    m_reconnectButton->setStyleSheet(AppColors::outlineButtonStyle());
    m_reconnectButton->setVisible(false);
    connect(m_reconnectButton, &QPushButton::clicked, m_chat, &ChatService::reconnectNow);
    headerLayout->addWidget(m_reconnectButton);
    root->addWidget(header);

    // ---- message list -------------------------------------------------------
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_messagesWidget = new QWidget(m_scroll);
    m_messagesLayout = new QVBoxLayout(m_messagesWidget);
    m_messagesLayout->setContentsMargins(20, 20, 20, 20);
    m_messagesLayout->setSpacing(12);
    m_messagesLayout->addStretch();
    m_scroll->setWidget(m_messagesWidget);
    root->addWidget(m_scroll, 1);

    // ---- composer -----------------------------------------------------------
    auto *composer = new QWidget(this);
    composer->setStyleSheet("background-color: #141416; border-top: 1px solid #2A2A2E;");
    auto *composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(16, 12, 16, 12);
    composerLayout->setSpacing(10);

    m_input = new QLineEdit(composer);
    m_input->setPlaceholderText(tr("Message your agent…"));
    m_input->setStyleSheet(AppColors::inputStyle());
    composerLayout->addWidget(m_input, 1);

    m_sendButton = new QPushButton(tr("Send"), composer);
    m_sendButton->setStyleSheet(AppColors::buttonStyle());
    m_sendButton->setCursor(Qt::PointingHandCursor);
    composerLayout->addWidget(m_sendButton);
    root->addWidget(composer);

    connect(m_sendButton, &QPushButton::clicked, this, &ChatScreen::onSend);
    connect(m_input, &QLineEdit::returnPressed, this, &ChatScreen::onSend);

    // ---- service wiring -----------------------------------------------------
    connect(m_chat, &ChatService::stateChanged, this, [this](ChatService::State s) {
        m_reconnectButton->setVisible(s == ChatService::State::Disconnected);
        switch (s) {
        case ChatService::State::Disconnected:
            // connectionError / reconnectScheduled say why and what happens next.
            break;
        case ChatService::State::Connecting:
            setStatus(tr("Connecting…"), AppColors::warning); break;
        case ChatService::State::Authenticating:
            setStatus(tr("Authenticating…"), AppColors::warning); break;
        case ChatService::State::Connected:
            setStatus(tr("Connected"), AppColors::success); break;
        }
    });

    connect(m_chat, &ChatService::connectionError, this, [this](const QString &msg, bool willRetry) {
        // In the status line, not the transcript: while the gateway is down every retry fails,
        // and a bubble per attempt would bury the conversation.
        if (!willRetry) setStatus(tr("Disconnected — %1").arg(msg), AppColors::error);
        m_status->setToolTip(msg);
    });

    connect(m_chat, &ChatService::reconnectScheduled, this, [this](int delayMs) {
        const int seconds = qMax(1, (delayMs + 999) / 1000);
        setStatus(tr("Connection lost — retrying in %ns", nullptr, seconds), AppColors::warning);
    });

    connect(m_chat, &ChatService::historyLoaded, this, &ChatScreen::showHistory);

    connect(m_chat, &ChatService::deltaReceived, this, [this](const QString &text) {
        // Deltas only belong to a turn we started; anything else is a late tail from a run
        // that already finished and must not grow the transcript.
        if (!m_turnActive) return;
        if (!m_streamingBubble) m_streamingBubble = addBubble(text, Bubble::Agent);
        else setBubbleText(m_streamingBubble, text, Bubble::Agent);
        scrollToBottom();
    });

    connect(m_chat, &ChatService::messageCompleted, this, [this](const QString &text) {
        m_turnActive = false;
        if (!text.isEmpty()) {
            if (!m_streamingBubble) addBubble(text, Bubble::Agent);
            else setBubbleText(m_streamingBubble, text, Bubble::Agent);
        }
        m_streamingBubble = nullptr;
        setComposerEnabled(true);
        scrollToBottom();
    });

    connect(m_chat, &ChatService::errorOccurred, this, [this](const QString &msg) {
        m_turnActive = false;
        setComposerEnabled(true);
        addBubble("⚠️ " + msg, Bubble::Notice);
        // A late delta must not land in the notice or in the reply it interrupted.
        m_streamingBubble = nullptr;
        scrollToBottom();
    });

    setStatus(tr("Connecting…"), AppColors::warning);
    m_chat->configure(claw.gatewayUrl(), claw.gatewayToken);
    m_chat->connectToGateway();
    m_input->setFocus();
}

void ChatScreen::setStatus(const QString &text, const QColor &color) {
    m_status->setText("● " + text);
    m_status->setStyleSheet(QString("font-size: 12px; border: none; color: %1;").arg(color.name()));
}

void ChatScreen::setComposerEnabled(bool enabled) {
    m_sendButton->setEnabled(enabled);
    m_input->setEnabled(enabled);
    if (enabled) m_input->setFocus();
}

void ChatScreen::setBubbleText(QLabel *bubble, const QString &text, Bubble kind) {
    if (kind == Bubble::Agent) bubble->setText(ChatFormat::markdownToHtml(text));
    else bubble->setText(text);
}

QLabel *ChatScreen::addBubble(const QString &text, Bubble kind, int at) {
    const bool fromUser = (kind == Bubble::User);
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);

    auto *bubble = new QLabel(m_messagesWidget);
    bubble->setWordWrap(true);
    // Never AutoText: the format is decided here, not guessed from the content. Replies are
    // Markdown rendered with HTML disabled (ChatFormat); everything else is shown verbatim.
    if (kind == Bubble::Agent) {
        bubble->setTextFormat(Qt::RichText);
        bubble->setTextInteractionFlags(Qt::TextBrowserInteraction);
        bubble->setOpenExternalLinks(true);
    } else {
        bubble->setTextFormat(Qt::PlainText);
        bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    bubble->setMaximumWidth(520);
    bubble->setStyleSheet(
        fromUser
            ? "background-color: #EF5350; color: white; border-radius: 14px; padding: 10px 14px; font-size: 13px;"
            : "background-color: #141416; color: #FFFFFF; border: 1px solid #2A2A2E;"
              "border-radius: 14px; padding: 10px 14px; font-size: 13px;");
    setBubbleText(bubble, text, kind);

    if (fromUser) { row->addStretch(); row->addWidget(bubble); }
    else          { row->addWidget(bubble); row->addStretch(); }

    // By default, before the trailing stretch so messages stay top-aligned as the list grows.
    m_messagesLayout->insertLayout(at < 0 ? m_messagesLayout->count() - 1 : at, row);
    return bubble;
}

void ChatScreen::showHistory(const QList<ChatHistoryEntry> &entries) {
    if (entries.isEmpty()) return;
    // Above whatever was already said in this window: history can arrive after the user has
    // sent their first message, and must not end up below it.
    int at = 0;
    for (const ChatHistoryEntry &e : entries)
        addBubble(e.text, e.fromUser ? Bubble::User : Bubble::Agent, at++);

    auto *divider = new QLabel(tr("Earlier messages above"), m_messagesWidget);
    divider->setAlignment(Qt::AlignCenter);
    divider->setStyleSheet("font-size: 11px; color: #48484A; background: transparent; border: none;");
    m_messagesLayout->insertWidget(at, divider);

    if (!m_hasLiveMessages) scrollToBottom();
}

void ChatScreen::scrollToBottom() {
    // Defer: the layout hasn't been recomputed yet at the moment a bubble is added.
    QTimer::singleShot(0, this, [this]() {
        m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
    });
}

void ChatScreen::onSend() {
    const QString text = m_input->text().trimmed();
    if (text.isEmpty()) return;

    addBubble(text, Bubble::User);
    m_hasLiveMessages = true;
    m_streamingBubble = nullptr;
    m_turnActive = true;
    m_input->clear();
    setComposerEnabled(false);
    scrollToBottom();

    m_chat->sendChatMessage(text);
}
