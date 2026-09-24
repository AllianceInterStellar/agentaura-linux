#pragma once
#include <QWidget>

class QVBoxLayout;

class AccountScreen : public QWidget {
    Q_OBJECT
public:
    explicit AccountScreen(QWidget *parent = nullptr);

    /// Redraw for the current session and re-check entitlement. The screen is constructed before
    /// anyone is signed in, so without this it keeps showing the empty user it was built with.
    void reload();

    /// Pull the real entitlement from the server and rebuild. The UI is built once in the
    /// constructor, so without this a Pro customer keeps seeing "Free Plan" for the whole session.
    void refreshSubscription();

signals:
    void signedOut();

private:
    void buildContent();

    QVBoxLayout *m_contentLayout = nullptr;
    /// Why the last entitlement check failed, shown on the subscription card — a failed check
    /// must not read as a confirmed "Free Plan".
    QString m_subscriptionError;
};
