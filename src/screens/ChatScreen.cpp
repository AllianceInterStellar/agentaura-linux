#include "screens/ChatScreen.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "theme/AppColors.h"

ChatScreen::ChatScreen(const Claw &claw, QWidget *parent)
    : QDialog(parent), m_claw(claw), m_chat(new ChatService(this)) {
    setWindowTitle(QString("Chat — %1").arg(claw.name));
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
    title->setStyleSheet("font-size: 15px; font-weight: bold; border: none;");
    headerLayout->addWidget(title);
    headerLayout->addStretch();

    m_status = new QLabel(header);
    m_status->setStyleSheet("font-size: 12px; border: none;");
    headerLayout->addWidget(m_status);
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
    m_input->setPlaceholderText("Message your agent…");
    m_input->setStyleSheet(AppColors::inputStyle());
    composerLayout->addWidget(m_input, 1);

    m_sendButton = new QPushButton("Send", composer);
    m_sendButton->setStyleSheet(AppColors::buttonStyle());
    m_sendButton->setCursor(Qt::PointingHandCursor);
    composerLayout->addWidget(m_sendButton);
    root->addWidget(composer);

    connect(m_sendButton, &QPushButton::clicked, this, &ChatScreen::onSend);
    connect(m_input, &QLineEdit::returnPressed, this, &ChatScreen::onSend);

    // ---- service wiring -----------------------------------------------------
    connect(m_chat, &ChatService::stateChanged, this, [this](ChatService::State s) {
        switch (s) {
        case ChatService::State::Disconnected:
            setStatus("Disconnected", AppColors::textMuted); break;
        case ChatService::State::Connecting:
            setStatus("Connecting…", AppColors::warning); break;
        case ChatService::State::Authenticating:
            setStatus("Authenticating…", AppColors::warning); break;
        case ChatService::State::Connected:
            setStatus("Connected", AppColors::success); break;
        }
    });

    connect(m_chat, &ChatService::deltaReceived, this, [this](const QString &text) {
        // Deltas only belong to a turn we started; anything else is a late tail from a run
        // that already finished and must not grow the transcript.
        if (!m_turnActive) return;
        if (!m_streamingBubble) addMessage(text, false);
        else m_streamingBubble->setText(text);
        scrollToBottom();
    });

    connect(m_chat, &ChatService::messageCompleted, this, [this](const QString &text) {
        m_turnActive = false;
        if (!text.isEmpty()) {
            if (!m_streamingBubble) addMessage(text, false);
            else m_streamingBubble->setText(text);
        }
        m_streamingBubble = nullptr;
        m_sendButton->setEnabled(true);
        m_input->setEnabled(true);
        scrollToBottom();
    });

    connect(m_chat, &ChatService::errorOccurred, this, [this](const QString &msg) {
        m_turnActive = false;
        m_sendButton->setEnabled(true);
        m_input->setEnabled(true);
        addMessage("⚠️ " + msg, false);
        // Clear AFTER adding: addMessage() claims the new bubble as the streaming target, and a
        // late delta must not overwrite the error text.
        m_streamingBubble = nullptr;
        scrollToBottom();
    });

    setStatus("Connecting…", AppColors::warning);
    m_chat->configure(claw.gatewayUrl(), claw.gatewayToken);
    m_chat->connectToGateway();
    m_input->setFocus();
}

void ChatScreen::setStatus(const QString &text, const QColor &color) {
    m_status->setText("● " + text);
    m_status->setStyleSheet(QString("font-size: 12px; border: none; color: %1;").arg(color.name()));
}

void ChatScreen::addMessage(const QString &text, bool fromUser) {
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);

    auto *bubble = new QLabel(text, m_messagesWidget);
    bubble->setWordWrap(true);
    bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bubble->setMaximumWidth(520);
    bubble->setStyleSheet(
        fromUser
            ? "background-color: #EF5350; color: white; border-radius: 14px; padding: 10px 14px; font-size: 13px;"
            : "background-color: #141416; color: #FFFFFF; border: 1px solid #2A2A2E;"
              "border-radius: 14px; padding: 10px 14px; font-size: 13px;");

    if (fromUser) { row->addStretch(); row->addWidget(bubble); }
    else          { row->addWidget(bubble); row->addStretch(); }

    // Insert before the trailing stretch so messages stay top-aligned as the list grows.
    m_messagesLayout->insertLayout(m_messagesLayout->count() - 1, row);

    if (!fromUser) m_streamingBubble = bubble;
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

    addMessage(text, true);
    m_streamingBubble = nullptr;
    m_turnActive = true;
    m_input->clear();
    m_sendButton->setEnabled(false);
    m_input->setEnabled(false);
    scrollToBottom();

    m_chat->sendChatMessage(text);
}
