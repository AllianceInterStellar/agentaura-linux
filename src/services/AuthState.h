#pragma once

#include "models/AppUser.h"
#include "services/ApiClient.h"
#include "services/FirebaseAuth.h"

#include <QDateTime>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <functional>
#include <utility>
#include <vector>

/// Signed-in state for the desktop client.
///
/// Sessions come from the backend's email one-time passcode: POST /auth/send-otp then
/// /auth/verify-otp yields a Firebase *custom* token, which FirebaseAuth exchanges for the
/// idToken every ClawHostAPI call is bearer-authenticated with. The refresh token is kept in
/// QSettings so the app comes back signed in, exactly like the mobile clients.
class AuthState {
public:
    static AuthState &instance() { static AuthState s; return s; }

    bool isLoggedIn() const { return m_loggedIn; }
    const AppUser &user() const { return m_user; }

    /// Installed by MainWindow: the session ended for a reason the app cannot undo (the refresh
    /// token was revoked or expired), so the user has to be asked to sign in again. AuthState
    /// owns no UI, so it reports the fact and lets the window decide how to gate on it.
    void setOnSessionExpired(std::function<void()> handler) {
        m_onSessionExpired = std::move(handler);
    }

    /// Adopt a freshly exchanged Firebase session and start authenticating API calls with it.
    void applySession(const FirebaseSession &session, const QString &email) {
        m_user.id = session.uid;
        if (!email.isEmpty()) {
            m_user.email = email;
            if (m_user.displayName.isEmpty()) m_user.displayName = email.section('@', 0, 0);
        }
        m_refreshToken = session.refreshToken;
        m_expiresAtMs = session.expiresAtMs;
        m_loggedIn = true;

        ApiClient::instance().setAuthToken(session.idToken);
        persist();
    }

    void logout() {
        m_user = {};
        m_loggedIn = false;
        m_refreshToken.clear();
        m_expiresAtMs = 0;
        // Any refresh still in flight belongs to the session being ended; bumping the epoch
        // makes its result land nowhere instead of quietly signing the user back in.
        ++m_sessionEpoch;
        ApiClient::instance().setAuthToken({});
        QSettings st;
        st.remove("auth");
    }

    /// Restore a stored session at startup. Calls back with whether a usable session was
    /// re-established (the refresh token is long-lived, the idToken is not).
    void restore(std::function<void(bool)> done) {
        QSettings st;
        const QString refresh = st.value("auth/refreshToken").toString();
        const QString email = st.value("auth/email").toString();
        if (refresh.isEmpty()) {
            if (done) done(false);
            return;
        }
        FirebaseAuth::instance().refresh(
            refresh,
            [this, email, done](FirebaseSession session) {
                applySession(session, email);
                if (done) done(true);
            },
            [this, done](const QString &, AuthFailure failure) {
                // Rejected means revoked/expired: start clean rather than leaving the UI
                // claiming to be signed in. A transport failure (launched with no network) is
                // no verdict on the token, so keep it stored — the sign-in prompt still comes
                // up now, but the next launch with a connection restores the session instead
                // of having thrown it away.
                if (failure == AuthFailure::Rejected) logout();
                if (done) done(false);
            });
    }

    /// How long before the cached expiry the idToken is treated as due for renewal. It has to
    /// cover a whole keep-alive poll interval *plus* the round trip of the refresh itself:
    /// renewing only once the deadline is behind us leaves calls going out on a token with
    /// seconds left on it, which land as 401s and lean on ApiClient's replay — the pre-emptive
    /// refresh exists precisely so that does not happen. (FirebaseAuth already parks
    /// expiresAtMs a minute ahead of Firebase's own expiry; this margin stacks on top, well
    /// inside the token's ~1h life.)
    static constexpr qint64 kRefreshMarginMs = 2 * 60 * 1000;

    /// True when the cached idToken is at/near expiry and should be refreshed before use.
    bool needsRefresh() const {
        return m_loggedIn && m_expiresAtMs > 0 &&
               QDateTime::currentMSecsSinceEpoch() >= m_expiresAtMs - kRefreshMarginMs;
    }

    /// Refresh the idToken in place (no-op when there is nothing stored).
    void refreshIfNeeded(std::function<void(bool)> done = {}) {
        if (!needsRefresh() || m_refreshToken.isEmpty()) {
            if (done) done(m_loggedIn);
            return;
        }
        forceRefresh(std::move(done));
    }

    /// Re-mint the idToken now, whatever the cached expiry says — what a 401 from the backend
    /// calls for, since the server's verdict beats our clock. Callers arriving while one refresh
    /// is already in flight wait for that one instead of starting another.
    void forceRefresh(std::function<void(bool)> done = {}) {
        if (!m_loggedIn || m_refreshToken.isEmpty()) {
            if (done) done(false);
            return;
        }
        if (done) m_refreshWaiters.push_back(std::move(done));
        if (m_refreshInFlight) return;
        m_refreshInFlight = true;

        const QString email = m_user.email;
        const int epoch = m_sessionEpoch;
        FirebaseAuth::instance().refresh(
            m_refreshToken,
            [this, email, epoch](FirebaseSession session) {
                if (epoch != m_sessionEpoch) {  // signed out while this was in flight
                    settleRefresh(false);
                    return;
                }
                applySession(session, email);
                settleRefresh(true);
            },
            [this, epoch](const QString &, AuthFailure failure) {
                // A refresh token Firebase *refused* is the end of the session: a password
                // change or a revocation killed it, and no amount of retrying brings it back.
                // Left alone, every screen would sit on "Session expired — please sign in
                // again" behind a Retry that can never succeed, with no way back to a login.
                // So drop the session and let MainWindow re-gate on the sign-in dialog.
                // A transport failure is not a verdict — keep the session and let the
                // keep-alive poll (or the next 401) try again.
                const bool revoked = (epoch == m_sessionEpoch) &&  // still the same session
                                     failure == AuthFailure::Rejected;
                if (revoked) logout();
                // Waiters first: they carry the failure into whatever the user was doing, so
                // the screen shows its error before the sign-in prompt goes up over it.
                settleRefresh(false);
                if (revoked) notifySessionExpired();
            });
    }

private:
    AuthState() {
        // The idToken every API call is bearer-authenticated with expires about an hour in, and
        // nothing else renews it — the session used to go quietly dead until the app restarted.
        // Poll so it is re-minted just before it lapses (a poll rather than a one-shot timer at
        // expiry so a suspended machine catches up on wake), and let ApiClient re-mint on a 401.
        ApiClient::instance().setTokenRefresher(
            [this](std::function<void(bool)> done) { forceRefresh(std::move(done)); });

        m_keepAlive.setInterval(30 * 1000);
        QObject::connect(&m_keepAlive, &QTimer::timeout, &m_keepAlive, [this]() { refreshIfNeeded(); });
        m_keepAlive.start();
    }

    /// Announce that the session is gone. Deferred onto the event loop because this is reached
    /// from inside a network reply handler and the handler opens a modal dialog — let the
    /// refresh (and the API call that triggered it) finish unwinding first. m_keepAlive is just
    /// a long-lived QObject on this thread to hang the timer off.
    void notifySessionExpired() {
        if (!m_onSessionExpired) return;
        QTimer::singleShot(0, &m_keepAlive, [handler = m_onSessionExpired]() { handler(); });
    }

    /// Hand the outcome of the one in-flight refresh to everyone who was waiting on it.
    void settleRefresh(bool ok) {
        m_refreshInFlight = false;
        auto waiters = std::move(m_refreshWaiters);
        m_refreshWaiters.clear();
        for (auto &waiter : waiters) waiter(ok);
    }

    void persist() const {
        QSettings st;
        st.setValue("auth/refreshToken", m_refreshToken);
        st.setValue("auth/email", m_user.email);
        st.setValue("auth/uid", m_user.id);
    }

    bool m_loggedIn = false;
    AppUser m_user;
    QString m_refreshToken;
    qint64 m_expiresAtMs = 0;

    QTimer m_keepAlive;
    bool m_refreshInFlight = false;
    int m_sessionEpoch = 0;
    std::vector<std::function<void(bool)>> m_refreshWaiters;
    std::function<void()> m_onSessionExpired;
};
