#include "MainWindow.h"
#include <QApplication>
#include "screens/ChatScreen.h"
#include "screens/ClawsScreen.h"
#include "screens/LoginDialog.h"
#include "services/AuthState.h"
#include "screens/DeployScreen.h"
#include "screens/AccountScreen.h"
#include "screens/ConfigScreen.h"
#include "theme/AppColors.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QDesktopServices>
#include <QPointer>
#include <QUrl>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("AgentAura");
    setMinimumSize(900, 640);
    resize(1060, 720);

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *sidebar = new QWidget(central);
    sidebar->setFixedWidth(200);
    sidebar->setStyleSheet("background-color: #0E0E10; border-right: 1px solid #1E1E22;");
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(12, 20, 12, 20);
    sideLayout->setSpacing(4);

    auto *logo = new QLabel("🔥 AgentAura", sidebar);
    logo->setStyleSheet("font-size: 16px; font-weight: bold; color: #EF5350; background: transparent;"
                        " border: none; padding: 0 8px 16px 8px;");
    sideLayout->addWidget(logo);

    auto *clawsBtn = makeNavButton("🖥", "Claws");
    auto *configBtn = makeNavButton("⚙", "Settings");
    auto *accountBtn = makeNavButton("👤", "Account");

    m_navButtons = {clawsBtn, configBtn, accountBtn};

    connect(clawsBtn, &QPushButton::clicked, this, [this]() { switchTab(0); });
    connect(configBtn, &QPushButton::clicked, this, [this]() { switchTab(1); });
    connect(accountBtn, &QPushButton::clicked, this, [this]() { switchTab(2); });

    sideLayout->addWidget(clawsBtn);
    sideLayout->addWidget(configBtn);
    sideLayout->addWidget(accountBtn);
    sideLayout->addStretch();

    auto *version = new QLabel("v1.0.0", sidebar);
    version->setStyleSheet("font-size: 10px; color: #48484A; background: transparent; border: none; padding: 0 8px;");
    sideLayout->addWidget(version);

    mainLayout->addWidget(sidebar);

    m_stack = new QStackedWidget(central);

    m_clawsScreen = new ClawsScreen(m_stack);
    m_deployScreen = new DeployScreen(m_stack);
    m_configScreen = new ConfigScreen(m_stack);
    m_accountScreen = new AccountScreen(m_stack);

    m_stack->addWidget(m_clawsScreen);
    m_stack->addWidget(m_configScreen);
    m_stack->addWidget(m_accountScreen);
    m_stack->addWidget(m_deployScreen);

    connect(m_clawsScreen, &ClawsScreen::deployRequested, this, &MainWindow::showDeployScreen);
    connect(m_clawsScreen, &ClawsScreen::openChat, this, [this](const Claw &claw) {
        auto *chat = new ChatScreen(claw, this);
        chat->setAttribute(Qt::WA_DeleteOnClose);
        chat->show();
    });
    connect(m_accountScreen, &AccountScreen::signedOut, this, [this]() {
        // Signing out invalidates every API call — re-gate behind the login dialog.
        promptSignIn();
    });
    // The same gate for a session that ended without being asked to: once Firebase refuses the
    // refresh token (password changed, access revoked) nothing the app holds is usable again,
    // so the user is sent back to the login dialog instead of being left staring at "Session
    // expired" on every screen. QPointer because AuthState is a singleton that outlives the
    // window and the callback arrives from a network reply.
    AuthState::instance().setOnSessionExpired([self = QPointer<MainWindow>(this)]() {
        if (self) self->promptSignIn("Your session ended — please sign in again.");
    });
    connect(m_deployScreen, &DeployScreen::goBack, this, &MainWindow::showClawsScreen);
    connect(m_deployScreen, &DeployScreen::deployStarted, this, [this]() {
        m_clawsScreen->loadClaws();
    });

    mainLayout->addWidget(m_stack, 1);

    // No data is loaded here: the screens exist before anyone is signed in, and an unauthenticated
    // ClawHostAPI call is just a 401. onSignedIn() is what starts the fetching.
    switchTab(0);
}

void MainWindow::promptSignIn(const QString &notice) {
    // exec() spins a nested event loop, and an expired session can be discovered by several
    // in-flight calls at once — without this guard the user would get a stack of login dialogs,
    // each waiting on the one in front of it.
    if (m_signInPromptOpen) return;
    m_signInPromptOpen = true;
    LoginDialog dlg(this, notice);
    const bool signedIn = (dlg.exec() == QDialog::Accepted);
    m_signInPromptOpen = false;
    if (!signedIn) {
        // Every screen is backed by an authenticated call, so there is nothing to stay open for.
        QApplication::quit();
        return;
    }
    onSignedIn();
}

void MainWindow::switchTab(int index) {
    m_currentTab = index;
    m_stack->setCurrentIndex(index);
    for (int i = 0; i < m_navButtons.size(); ++i) {
        bool active = (i == index);
        m_navButtons[i]->setStyleSheet(
            QString("QPushButton { background-color: %1; color: %2; border: none; border-radius: 8px;"
                    " text-align: left; padding: 10px 12px; font-size: 13px; font-weight: %3; }"
                    "QPushButton:hover { background-color: %4; }")
            .arg(active ? "rgba(239,83,80,0.12)" : "transparent",
                 active ? "#EF5350" : "#AEAEB2",
                 active ? "bold" : "normal",
                 active ? "rgba(239,83,80,0.18)" : "#141416"));
    }
}

void MainWindow::showDeployScreen() {
    m_stack->setCurrentWidget(m_deployScreen);
}

void MainWindow::showClawsScreen() {
    switchTab(0);
}

QPushButton *MainWindow::makeNavButton(const QString &icon, const QString &label) {
    auto *btn = new QPushButton(icon + "  " + label, this);
    btn->setFixedHeight(40);
    btn->setCursor(Qt::PointingHandCursor);
    return btn;
}

void MainWindow::onSignedIn() {
    // Every screen is constructed before a session exists, so this is the first point at which
    // any of them may talk to the backend — and the point at which the account screen finally
    // knows who is signed in. Refresh a stale restored idToken first, then load them all.
    AuthState::instance().refreshIfNeeded([this](bool) {
        m_clawsScreen->loadClaws();
        m_configScreen->reload();
        m_accountScreen->reload();
        m_deployScreen->reload();
    });
    switchTab(0);
}
