#include "services/ApiClient.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <QMap>

// Set at configure time (AGENTAURA_API_BASE_URL in CMakeLists.txt).
const QString ApiClient::BASE_URL = QStringLiteral(AGENTAURA_API_BASE_URL);

// Backend stores provider tokens keyed by env-var name (see TOKEN_ENV_MAP in ClawHostAPI).
static QString providerTokenKey(const QString &provider) {
    static const QMap<QString, QString> keys = {
        {"hetzner", "HETZNER_API_TOKEN"}, {"vultr", "VULTR_API_TOKEN"},
        {"digitalocean", "DIGITALOCEAN_API_TOKEN"}, {"linode", "LINODE_API_TOKEN"},
        {"railway", "RAILWAY_API_TOKEN"}, {"flyio", "FLY_API_TOKEN"},
        {"gcp", "GCP_PROJECT_ID"}, {"aws", "AWS_ACCESS_KEY_ID"},
        {"azure", "AZURE_SUBSCRIPTION_ID"}, {"ovhcloud", "OVH_APPLICATION_KEY"},
    };
    return keys.value(provider, provider.toUpper() + "_API_TOKEN");
}

ApiClient::ApiClient(QObject *parent) : QObject(parent) {}

QNetworkReply *ApiClient::request(const QString &method, const QString &path, const QByteArray &body) {
    QNetworkRequest req(QUrl(BASE_URL + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // Without this a connection that stalls never finishes, and the screen waiting on it sits on
    // "Loading…" for the rest of the session. Qt measures idle time, so a slow-but-live transfer
    // is not cut off.
    req.setTransferTimeout(30 * 1000);
    if (!m_authToken.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_authToken).toUtf8());

    if (method == "GET") return m_nam.get(req);
    if (method == "POST") return m_nam.post(req, body.isEmpty() ? QByteArray("{}") : body);
    if (method == "PUT") return m_nam.put(req, body.isEmpty() ? QByteArray("{}") : body);
    if (method == "DELETE") return m_nam.deleteResource(req);
    return m_nam.get(req);
}

/// An aborted reply (timeout) is already closed, and reading it anyway just logs a Qt warning.
static QByteArray readBody(QNetworkReply *reply) {
    return reply->isOpen() ? reply->readAll() : QByteArray();
}

/// Pull the backend's error message out of the {success,data,message} envelope.
static QString envelopeError(const QByteArray &raw, const QString &fallback) {
    const auto obj = QJsonDocument::fromJson(raw).object();
    const QString msg = obj.value("message").toString();
    return msg.isEmpty() ? fallback : msg;
}

void ApiClient::send(const QString &method, const QString &path, const QByteArray &body,
                     std::function<void(QByteArray)> onOk, std::function<void(QString)> onErr,
                     bool allowRetry) {
    auto *reply = request(method, path, body);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, method, path, body, onOk, onErr, allowRetry]() {
        reply->deleteLater();
        const QByteArray raw = readBody(reply);
        if (reply->error() == QNetworkReply::NoError) {
            onOk(raw);
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 && allowRetry && m_tokenRefresher) {
            // The Firebase idToken only lives about an hour. Re-mint it and replay once, so a
            // long-running window keeps working instead of failing every call from then on.
            m_tokenRefresher([this, method, path, body, onOk, onErr, raw](bool refreshed) {
                if (!refreshed) {
                    onErr(envelopeError(raw, "Session expired — please sign in again."));
                    return;
                }
                send(method, path, body, onOk, onErr, /*allowRetry=*/false);
            });
            return;
        }
        onErr(envelopeError(raw, reply->errorString()));
    });
}

void ApiClient::sendEmailOtp(const QString &email,
                             std::function<void()> onSuccess, std::function<void(QString)> onError) {
    QJsonObject body;
    body["email"] = email.trimmed().toLower();
    send("POST", "/auth/send-otp", QJsonDocument(body).toJson(QJsonDocument::Compact),
         [onSuccess](const QByteArray &) { onSuccess(); }, onError);
}

void ApiClient::verifyEmailOtp(const QString &email, const QString &code,
                               std::function<void(QString)> onSuccess,
                               std::function<void(QString)> onError) {
    QJsonObject body;
    body["email"] = email.trimmed().toLower();
    body["code"] = code.trimmed();
    send("POST", "/auth/verify-otp", QJsonDocument(body).toJson(QJsonDocument::Compact),
         [onSuccess, onError](const QByteArray &raw) {
             const auto obj = QJsonDocument::fromJson(raw).object();
             const QString customToken = obj.value("data").toObject().value("customToken").toString();
             if (customToken.isEmpty()) {
                 onError(envelopeError(raw, "No token returned"));
                 return;
             }
             onSuccess(customToken);
         },
         onError);
}

void ApiClient::fetchClaws(std::function<void(QList<Claw>)> onSuccess, std::function<void(QString)> onError) {
    send("GET", "/claws", {},
         [onSuccess](const QByteArray &raw) {
             // Every ClawHostAPI response is wrapped in {success,data,message,…}; the list lives
             // in `data`. Reading the root as an array returned nothing, so the list looked empty.
             QList<Claw> claws;
             for (const auto &v : QJsonDocument::fromJson(raw).object()["data"].toArray())
                 claws.append(Claw::fromJson(v.toObject()));
             onSuccess(claws);
         },
         onError);
}

void ApiClient::createClaw(const QString &name, const QString &provider, const QString &planId,
                           const QString &location, const QString &deployMethod,
                           const QString &appType, const AiModelConfig &aiModelConfig,
                           std::function<void(Claw)> onSuccess, std::function<void(QString)> onError) {
    QJsonObject body;
    body["name"] = name;
    body["provider"] = provider;
    body["planId"] = planId;
    if (!location.isEmpty()) body["location"] = location;
    if (!deployMethod.isEmpty()) body["deployMethod"] = deployMethod;
    if (!appType.isEmpty()) body["appTypes"] = QJsonArray{appType};
    if (!aiModelConfig.isEmpty()) {
        body["aiModelConfig"] = QJsonObject{
            {"provider", aiModelConfig.provider},
            {"modelId", aiModelConfig.modelId},
            {"apiKey", aiModelConfig.apiKey.trimmed()},
            {"envVarName", aiModelConfig.envVarName}
        };
    }
    send("POST", "/claws", QJsonDocument(body).toJson(QJsonDocument::Compact),
         [onSuccess](const QByteArray &raw) {
             onSuccess(Claw::fromJson(QJsonDocument::fromJson(raw).object()["data"].toObject()));
         },
         onError);
}

void ApiClient::deleteClaw(const QString &id, std::function<void()> onSuccess, std::function<void(QString)> onError) {
    send("DELETE", "/claws/" + id, {}, [onSuccess](const QByteArray &) { onSuccess(); }, onError);
}

void ApiClient::startClaw(const QString &id, std::function<void()> onSuccess, std::function<void(QString)> onError) {
    send("POST", "/claws/" + id + "/start", {}, [onSuccess](const QByteArray &) { onSuccess(); }, onError);
}

void ApiClient::stopClaw(const QString &id, std::function<void()> onSuccess, std::function<void(QString)> onError) {
    send("POST", "/claws/" + id + "/stop", {}, [onSuccess](const QByteArray &) { onSuccess(); }, onError);
}

void ApiClient::fetchSubscription(std::function<void(UserSubscription)> onSuccess,
                                  std::function<void(QString)> onError) {
    send("GET", "/subscriptions/status?app=agentaura", {},
         [onSuccess](const QByteArray &raw) {
             const QJsonObject data = QJsonDocument::fromJson(raw).object()["data"].toObject();
             UserSubscription sub;
             const QString tier = data["tier"].toString();
             const QString productId = data["productId"].toString();
             // The server usually collapses annual to the same "pro" tier (the product id is what
             // distinguishes the two for display), but legacy rows can still carry a literal
             // "pro_annual" — the tier column is free text and status echoes stored rows back.
             if (tier == "pro_annual")
                 sub.tier = SubscriptionTier::ProAnnual;
             else if (tier == "pro")
                 sub.tier = productId.contains("annual") ? SubscriptionTier::ProAnnual
                                                         : SubscriptionTier::Pro;
             sub.productId = productId;
             sub.expiresAt = data["updatedAt"].toString();
             sub.isActive = data["active"].toBool();
             onSuccess(sub);
         },
         onError);
}

void ApiClient::fetchPlans(const QString &provider, std::function<void(QList<PlanInfo>)> onSuccess,
                           std::function<void(QString)> onError) {
    send("GET", "/plans?provider=" + provider, {},
         [onSuccess](const QByteArray &raw) {
             // The envelope is {"success":…,"data":{"plans":[…],"atCapacity":…}} — the plans are
             // two levels down, not at the document root. Reading the root as an array silently
             // yielded an empty list, so the picker stayed blank while the request "succeeded".
             const QJsonObject data = QJsonDocument::fromJson(raw).object()["data"].toObject();
             QList<PlanInfo> plans;
             for (const auto &v : data["plans"].toArray())
                 plans.append(PlanInfo::fromJson(v.toObject()));
             onSuccess(plans);
         },
         onError);
}

void ApiClient::fetchRegions(const QString &provider, std::function<void(QList<RegionInfo>)> onSuccess,
                             std::function<void(QString)> onError) {
    send("GET", "/plans/locations?provider=" + provider, {},
         [onSuccess](const QByteArray &raw) {
             QList<RegionInfo> regions;
             for (const auto &v : QJsonDocument::fromJson(raw).object()["data"].toArray())
                 regions.append(RegionInfo::fromJson(v.toObject()));
             onSuccess(regions);
         },
         onError);
}

void ApiClient::syncProviderConfig(const QString &provider, const QString &token,
                                   std::function<void()> onSuccess, std::function<void(QString)> onError) {
    // PUT /provider-configs REPLACES the provider's whole tokens object (the server does
    // `.set({ tokens })`, not a merge). This client edits one key, but AWS/Azure/GCP configs
    // saved from the web dashboard hold several — so read what is stored first and merge our
    // key into it, or one Sync here silently wipes the rest of the credential.
    send("GET", "/provider-configs", {},
        [this, provider, token, onSuccess, onError](const QByteArray &raw) {
            QJsonObject tokens;
            for (const auto &v : QJsonDocument::fromJson(raw).object()["data"].toArray()) {
                const auto obj = v.toObject();
                if (obj["provider"].toString() == provider) {
                    tokens = obj["tokens"].toObject();
                    break;
                }
            }
            tokens[providerTokenKey(provider)] = token;
            QJsonObject body;
            body["provider"] = provider;
            body["tokens"] = tokens;
            send("PUT", "/provider-configs", QJsonDocument(body).toJson(QJsonDocument::Compact),
                 [onSuccess](const QByteArray &) { onSuccess(); }, onError);
        },
        onError);
}

void ApiClient::fetchProviderConfigs(std::function<void(QMap<QString, QString>)> onSuccess,
                                     std::function<void(QString)> onError) {
    send("GET", "/provider-configs", {},
         [onSuccess](const QByteArray &raw) {
             QMap<QString, QString> tokensByProvider;
             for (const auto &v : QJsonDocument::fromJson(raw).object()["data"].toArray()) {
                 const auto obj = v.toObject();
                 const QString provider = obj["provider"].toString();
                 if (provider.isEmpty()) continue;
                 const auto tokens = obj["tokens"].toObject();
                 QString token = tokens[providerTokenKey(provider)].toString();
                 // Providers whose credentials predate the env-var naming still store a single
                 // unnamed token; fall back to whatever is there.
                 if (token.isEmpty() && !tokens.isEmpty()) token = tokens.begin().value().toString();
                 if (!token.isEmpty()) tokensByProvider.insert(provider, token);
             }
             onSuccess(tokensByProvider);
         },
         onError);
}

void ApiClient::deleteProviderConfig(const QString &provider,
                                     std::function<void()> onSuccess, std::function<void(QString)> onError) {
    send("DELETE", "/provider-configs/" + provider, {},
         [onSuccess](const QByteArray &) { onSuccess(); }, onError);
}
