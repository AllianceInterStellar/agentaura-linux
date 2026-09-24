#include "screens/LoginDialog.h"

#include "services/ApiClient.h"
#include "services/AuthState.h"
#include "services/FirebaseAuth.h"
#include "theme/AppColors.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

LoginDialog::LoginDialog(QWidget *parent, const QString &notice) : QDialog(parent) {
    setWindowTitle("Sign in to AgentAura");
    setModal(true);
    setMinimumWidth(380);
    setStyleSheet("QDialog { background-color: #0A0A0B; }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(14);

    auto *title = new QLabel("Sign in", this);
    title->setStyleSheet("font-size: 20px; font-weight: bold; color: #FFFFFF; background: transparent;");
    root->addWidget(title);

    // A dialog that appears mid-session is otherwise unexplained — say what happened.
    if (!notice.isEmpty()) {
        auto *noticeLabel = new QLabel(notice, this);
        noticeLabel->setWordWrap(true);
        noticeLabel->setStyleSheet(
            "font-size: 12px; color: #EF5350; background-color: rgba(239,83,80,0.1);"
            " border: 1px solid rgba(239,83,80,0.3); border-radius: 8px; padding: 10px;");
        root->addWidget(noticeLabel);
    }

    auto *subtitle = new QLabel("We'll email you a one-time code — no password needed.", this);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet("font-size: 12px; color: #8E8E93; background: transparent;");
    root->addWidget(subtitle);

    // Reuse the app-wide input/button styling so the dialog matches every other screen.
    const QString fieldStyle = AppColors::inputStyle();
    const QString primaryStyle = AppColors::buttonStyle();

    m_stack = new QStackedWidget(this);

    // ── Step 1: email ──
    auto *emailPage = new QWidget(m_stack);
    auto *emailLayout = new QVBoxLayout(emailPage);
    emailLayout->setContentsMargins(0, 0, 0, 0);
    emailLayout->setSpacing(10);
    m_emailEdit = new QLineEdit(emailPage);
    m_emailEdit->setPlaceholderText("you@example.com");
    m_emailEdit->setStyleSheet(fieldStyle);
    emailLayout->addWidget(m_emailEdit);
    m_sendBtn = new QPushButton("Send code", emailPage);
    m_sendBtn->setStyleSheet(primaryStyle);
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    emailLayout->addWidget(m_sendBtn);
    m_stack->addWidget(emailPage);

    // ── Step 2: code ──
    auto *codePage = new QWidget(m_stack);
    auto *codeLayout = new QVBoxLayout(codePage);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    codeLayout->setSpacing(10);
    m_sentToLabel = new QLabel(codePage);
    m_sentToLabel->setWordWrap(true);
    m_sentToLabel->setStyleSheet("font-size: 12px; color: #8E8E93; background: transparent;");
    codeLayout->addWidget(m_sentToLabel);
    m_codeEdit = new QLineEdit(codePage);
    m_codeEdit->setPlaceholderText("6-digit code");
    m_codeEdit->setMaxLength(6);
    m_codeEdit->setStyleSheet(fieldStyle);
    codeLayout->addWidget(m_codeEdit);
    m_verifyBtn = new QPushButton("Verify", codePage);
    m_verifyBtn->setStyleSheet(primaryStyle);
    m_verifyBtn->setCursor(Qt::PointingHandCursor);
    codeLayout->addWidget(m_verifyBtn);
    m_stack->addWidget(codePage);

    root->addWidget(m_stack);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    m_errorLabel->setStyleSheet("font-size: 12px; color: #EF5350; background: transparent;");
    root->addWidget(m_errorLabel);

    connect(m_sendBtn, &QPushButton::clicked, this, &LoginDialog::sendCode);
    connect(m_emailEdit, &QLineEdit::returnPressed, this, &LoginDialog::sendCode);
    connect(m_verifyBtn, &QPushButton::clicked, this, &LoginDialog::verifyCode);
    connect(m_codeEdit, &QLineEdit::returnPressed, this, &LoginDialog::verifyCode);
}

void LoginDialog::setBusy(bool busy) {
    m_busy = busy;
    m_sendBtn->setEnabled(!busy);
    m_verifyBtn->setEnabled(!busy);
    m_emailEdit->setEnabled(!busy);
    m_codeEdit->setEnabled(!busy);
    if (busy) {
        m_sendBtn->setText("Sending…");
        m_verifyBtn->setText("Verifying…");
    } else {
        m_sendBtn->setText("Send code");
        m_verifyBtn->setText("Verify");
    }
}

void LoginDialog::showError(const QString &message) {
    m_errorLabel->setText(message);
    m_errorLabel->setVisible(!message.isEmpty());
}

void LoginDialog::sendCode() {
    if (m_busy) return;
    const QString email = m_emailEdit->text().trimmed();
    if (!email.contains('@')) {
        showError("Enter a valid email address.");
        return;
    }
    showError({});
    setBusy(true);
    m_email = email;

    ApiClient::instance().sendEmailOtp(
        email,
        [this]() {
            setBusy(false);
            m_sentToLabel->setText(QString("We sent a code to %1.").arg(m_email));
            m_stack->setCurrentIndex(1);
            m_codeEdit->setFocus();
        },
        [this](const QString &error) {
            setBusy(false);
            showError(error);
        });
}

void LoginDialog::verifyCode() {
    if (m_busy) return;
    const QString code = m_codeEdit->text().trimmed();
    if (code.isEmpty()) {
        showError("Enter the code from your email.");
        return;
    }
    showError({});
    setBusy(true);

    ApiClient::instance().verifyEmailOtp(
        m_email, code,
        [this](const QString &customToken) {
            // The backend's custom token still has to be exchanged with Firebase before
            // it can authenticate API calls.
            FirebaseAuth::instance().signInWithCustomToken(
                customToken,
                [this](FirebaseSession session) {
                    AuthState::instance().applySession(session, m_email);
                    setBusy(false);
                    accept();
                },
                [this](const QString &error) {
                    setBusy(false);
                    showError(error);
                });
        },
        [this](const QString &error) {
            setBusy(false);
            showError(error);
        });
}
