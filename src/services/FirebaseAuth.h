#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>
#include <functional>

/// One signed-in Firebase session (what the backend's custom token is exchanged for).
struct FirebaseSession {
    QString idToken;       ///< Bearer token for ClawHostAPI calls.
    QString refreshToken;  ///< Used to mint a new idToken when it expires (~1h).
    QString uid;
    qint64 expiresAtMs = 0;

    bool isValid() const { return !idToken.isEmpty() && !uid.isEmpty(); }
};

/// Why a token operation failed — the two cases call for opposite responses.
///
/// `Rejected` means Firebase answered and refused the credential (refresh token revoked by a
/// password change, expired, user disabled). It will never work again, so the session has to end.
/// `Transport` means no verdict ever came back (offline, timeout, a 5xx from Google). That says
/// nothing about the credential, so the session is kept and the next attempt retries it — losing
/// a good session to a flaky network would sign the user out for no reason.
enum class AuthFailure { Transport, Rejected };

/// Firebase Auth over its public REST API — no Firebase SDK, mirroring the WinUI
/// FirebaseRestAuth port. The backend hands out a *custom* token (POST /auth/verify-otp);
/// Firebase exchanges that for the idToken every other API call is authenticated with.
class FirebaseAuth : public QObject {
    Q_OBJECT
public:
    static FirebaseAuth &instance();

    /// Exchange a backend custom token for a real session.
    void signInWithCustomToken(const QString &customToken,
                               std::function<void(FirebaseSession)> onSuccess,
                               std::function<void(QString)> onError);

    /// Mint a fresh idToken from a stored refresh token (called on launch, on a 401, and by the
    /// keep-alive). `onError` reports whether the refresh token was *rejected* or merely
    /// unreachable, because only the former means the session is over.
    void refresh(const QString &refreshToken,
                 std::function<void(FirebaseSession)> onSuccess,
                 std::function<void(QString, AuthFailure)> onError);

private:
    explicit FirebaseAuth(QObject *parent = nullptr);

    QNetworkAccessManager m_nam;

    /// Public Firebase Web API key (same project as the iOS/Android/WinUI clients).
    static const QString API_KEY;
};
