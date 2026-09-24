#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

/// Email one-time-passcode sign-in, matching the mobile clients: enter an address,
/// receive a 6-digit code, exchange it for a Firebase session. There is no password
/// anywhere in this flow.
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    /// `notice` is shown above the form to say why sign-in is being asked for when the user did
    /// not ask for it (a revoked session). Empty for an ordinary sign-in or sign-out, where the
    /// dialog appearing needs no explanation.
    explicit LoginDialog(QWidget *parent = nullptr, const QString &notice = {});

private:
    void sendCode();
    void verifyCode();
    void setBusy(bool busy);
    void showError(const QString &message);

    QStackedWidget *m_stack = nullptr;

    // Step 1 — email
    QLineEdit *m_emailEdit = nullptr;
    QPushButton *m_sendBtn = nullptr;

    // Step 2 — code
    QLabel *m_sentToLabel = nullptr;
    QLineEdit *m_codeEdit = nullptr;
    QPushButton *m_verifyBtn = nullptr;

    QLabel *m_errorLabel = nullptr;
    QString m_email;
    bool m_busy = false;
};
