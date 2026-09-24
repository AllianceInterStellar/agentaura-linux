#pragma once
#include <QObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include "models/Claw.h"
#include "models/Subscription.h"
#include "models/ApiModels.h"

class ApiClient : public QObject {
    Q_OBJECT
public:
    static ApiClient &instance() { static ApiClient c; return c; }

    void setAuthToken(const QString &token) { m_authToken = token; }

    /// Mint a fresh idToken and report whether it worked. AuthState installs this so a 401
    /// mid-session can be recovered from instead of failing the user's action.
    using TokenRefresher = std::function<void(std::function<void(bool)>)>;
    void setTokenRefresher(TokenRefresher refresher) { m_tokenRefresher = std::move(refresher); }

    // ── Auth (email one-time passcode) ──
    /// POST /auth/send-otp — emails a 6-digit code.
    void sendEmailOtp(const QString &email,
                      std::function<void()> onSuccess, std::function<void(QString)> onError);
    /// POST /auth/verify-otp — returns a Firebase *custom* token to exchange via FirebaseAuth.
    void verifyEmailOtp(const QString &email, const QString &code,
                        std::function<void(QString)> onSuccess, std::function<void(QString)> onError);

    void fetchClaws(std::function<void(QList<Claw>)> onSuccess, std::function<void(QString)> onError);
    /// The AI credential a new instance is provisioned with. The server rejects a deployment
    /// (400) that carries neither an explicit key nor an agent that can authenticate with an
    /// account of its own, so this is not optional for the default `openclaw` agent.
    struct AiModelConfig {
        QString provider;      // "claude" | "openai" | "gemini" | "other"
        QString modelId;
        QString apiKey;
        QString envVarName;    // e.g. ANTHROPIC_API_KEY — where the agent reads the key from
        bool isEmpty() const { return apiKey.trimmed().isEmpty(); }
    };

    /// `location` and `appTypes` are the server's field names; sending `region` (as this client
    /// used to) means the request is missing a required field and is refused before anything is
    /// provisioned.
    void createClaw(const QString &name, const QString &provider, const QString &planId,
                    const QString &location, const QString &deployMethod,
                    const QString &appType, const AiModelConfig &aiModelConfig,
                    std::function<void(Claw)> onSuccess, std::function<void(QString)> onError);
    void deleteClaw(const QString &id, std::function<void()> onSuccess, std::function<void(QString)> onError);
    void startClaw(const QString &id, std::function<void()> onSuccess, std::function<void(QString)> onError);
    void stopClaw(const QString &id, std::function<void()> onSuccess, std::function<void(QString)> onError);

    /// GET /subscriptions/status — the server is the only source of truth for entitlement
    /// (store purchases on mobile and Stripe on the web both land there).
    void fetchSubscription(std::function<void(UserSubscription)> onSuccess,
                           std::function<void(QString)> onError);

    void fetchPlans(const QString &provider, std::function<void(QList<PlanInfo>)> onSuccess, std::function<void(QString)> onError);
    void fetchRegions(const QString &provider, std::function<void(QList<RegionInfo>)> onSuccess, std::function<void(QString)> onError);

    void syncProviderConfig(const QString &provider, const QString &token,
                            std::function<void()> onSuccess, std::function<void(QString)> onError);
    /// GET /provider-configs — every saved provider token in one call, keyed by provider id.
    /// One request covers the whole settings screen; asking per provider meant ten identical ones.
    void fetchProviderConfigs(std::function<void(QMap<QString, QString>)> onSuccess,
                              std::function<void(QString)> onError);
    void deleteProviderConfig(const QString &provider,
                              std::function<void()> onSuccess, std::function<void(QString)> onError);

private:
    explicit ApiClient(QObject *parent = nullptr);
    QNetworkReply *request(const QString &method, const QString &path, const QByteArray &body = {});

    /// Send a request and hand the raw response body to `onOk`. Failures reach `onErr` carrying
    /// the backend's own message where there is one. A 401 means the idToken lapsed mid-session,
    /// so the refresher gets one chance to re-mint it and the call is replayed.
    void send(const QString &method, const QString &path, const QByteArray &body,
              std::function<void(QByteArray)> onOk, std::function<void(QString)> onErr,
              bool allowRetry = true);

    QNetworkAccessManager m_nam;
    QString m_authToken;
    TokenRefresher m_tokenRefresher;
    static const QString BASE_URL;
};
