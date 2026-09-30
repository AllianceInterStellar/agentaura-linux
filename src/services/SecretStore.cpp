// libsecret pulls in GLib/GIO, whose headers use `signals` as an identifier — include it before
// anything that could bring in Qt's `signals` macro.
#ifdef AGENTAURA_HAVE_LIBSECRET
#include <libsecret/secret.h>
#endif

#include "services/SecretStore.h"

#include <QDebug>

namespace SecretStore {

#ifdef AGENTAURA_HAVE_LIBSECRET

namespace {

const SecretSchema *schema() {
    static const SecretSchema s = {
        "io.allianceinterstellar.AgentAura",
        SECRET_SCHEMA_NONE,
        {
            {"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
        },
        // Reserved fields, zero-initialised as libsecret expects.
        0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    };
    return &s;
}

/// Log once per kind of failure: without a Secret Service every call fails the same way.
void report(GError *error, const char *what) {
    if (!error) return;
    static bool logged = false;
    if (!logged) {
        qWarning().noquote() << "Keyring unavailable (" << what << "):" << error->message
                             << "— falling back to the settings file";
        logged = true;
    }
    g_error_free(error);
}

}  // namespace

bool compiledIn() { return true; }

bool store(const QString &key, const QString &value) {
    GError *error = nullptr;
    const QByteArray k = key.toUtf8(), v = value.toUtf8();
    const QByteArray label = "AgentAura " + k;
    const gboolean ok = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT,
                                                   label.constData(), v.constData(),
                                                   nullptr, &error, "account", k.constData(),
                                                   nullptr);
    report(error, "store");
    return ok;
}

std::optional<QString> lookup(const QString &key) {
    GError *error = nullptr;
    const QByteArray k = key.toUtf8();
    gchar *value = secret_password_lookup_sync(schema(), nullptr, &error,
                                               "account", k.constData(), nullptr);
    if (error) {
        report(error, "lookup");
        return std::nullopt;
    }
    if (!value) return QString();
    const QString out = QString::fromUtf8(value);
    secret_password_free(value);
    return out;
}

bool remove(const QString &key) {
    GError *error = nullptr;
    const QByteArray k = key.toUtf8();
    secret_password_clear_sync(schema(), nullptr, &error, "account", k.constData(), nullptr);
    if (error) {
        report(error, "remove");
        return false;
    }
    return true;
}

#else  // no libsecret in this build

bool compiledIn() { return false; }
bool store(const QString &, const QString &) { return false; }
std::optional<QString> lookup(const QString &) { return std::nullopt; }
bool remove(const QString &) { return false; }

#endif

}  // namespace SecretStore
