#include <QApplication>
#include <QIcon>
#include <QSslSocket>
#include <QTextStream>
#include "MainWindow.h"
#include "theme/AppColors.h"
#include "services/AuthState.h"
#include "screens/LoginDialog.h"

#ifndef AGENTAURA_VERSION
#define AGENTAURA_VERSION "0.0.0-dev"
#endif

// `agentaura --self-test` answers the question that most often decides whether the app can
// work at all on a given machine: can Qt speak TLS here? Every call this client makes is
// HTTPS or WSS, and Qt loads its TLS backend at runtime — a missing OpenSSL or backend
// plugin does not stop the app from starting, it just makes every request fail. The
// release pipeline runs this on a freshly installed package; users can run it too.
static int selfTest() {
    QTextStream out(stdout);
    const bool tls = QSslSocket::supportsSsl();
    out << "AgentAura " << AGENTAURA_VERSION << "\n"
        << "Qt " << qVersion() << "\n"
        << "TLS supported: " << (tls ? "yes" : "no") << "\n"
        << "TLS library: " << QSslSocket::sslLibraryVersionString() << "\n";
    return tls ? 0 : 1;
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--version") {
            QTextStream(stdout) << "AgentAura " << AGENTAURA_VERSION << "\n";
            return 0;
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("AgentAura");
    app.setOrganizationName("AgentAura");
    app.setApplicationVersion(AGENTAURA_VERSION);
    app.setWindowIcon(QIcon(":/icons/agentaura-256.png"));
    // Wayland compositors pick the taskbar icon and grouping from the desktop file.
    QGuiApplication::setDesktopFileName("io.allianceinterstellar.AgentAura");
    app.setStyleSheet(AppColors::globalStyleSheet());

    if (app.arguments().contains("--self-test")) return selfTest();

    MainWindow window;

    // Restore a stored session (refresh token in QSettings) before showing anything; if
    // there is none, or it was revoked, ask the user to sign in. Every ClawHostAPI call
    // needs the Firebase idToken, so the app is unusable while signed out.
    // With no stored token restore() answers synchronously, i.e. before app.exec() — and
    // QApplication::quit() at that point is documented as a no-op, so cancelling the first-launch
    // login used to leave a windowless process running the event loop forever. Record the
    // cancellation and skip the loop; quit() still covers the case where the callback arrived
    // asynchronously (a token refresh) and the loop is already running.
    bool cancelled = false;
    AuthState::instance().restore([&window, &cancelled](bool restored) {
        if (!restored) {
            LoginDialog dlg(&window);
            if (dlg.exec() != QDialog::Accepted) {
                cancelled = true;
                QApplication::quit();
                return;
            }
        }
        window.onSignedIn();
        window.show();
    });

    if (cancelled) return 0;
    return app.exec();
}
