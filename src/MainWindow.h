#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include <QPushButton>

class ClawsScreen;
class DeployScreen;
class AccountScreen;
class ConfigScreen;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    /// Called once a session exists — loads the data every screen needs.
    void onSignedIn();

    explicit MainWindow(QWidget *parent = nullptr);

private:
    /// Put the login dialog back in front of the user and reload once they are signed in — the
    /// one way back into a usable app, whether they signed out themselves or the session was
    /// revoked out from under them. Quits if they dismiss it, since nothing works signed out.
    /// `notice` explains an ending the user did not ask for; empty for a deliberate sign-out.
    void promptSignIn(const QString &notice = {});

    void switchTab(int index);
    void showDeployScreen();
    void showClawsScreen();
    QPushButton *makeNavButton(const QString &icon, const QString &label);

    QStackedWidget *m_stack = nullptr;
    ClawsScreen *m_clawsScreen = nullptr;
    DeployScreen *m_deployScreen = nullptr;
    AccountScreen *m_accountScreen = nullptr;
    ConfigScreen *m_configScreen = nullptr;
    QList<QPushButton *> m_navButtons;
    int m_currentTab = 0;
    bool m_signInPromptOpen = false;
};
