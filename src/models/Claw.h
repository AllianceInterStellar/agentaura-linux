#pragma once
#include <QString>
#include <QColor>
#include <QJsonObject>
#include <QJsonArray>
#include "theme/AppColors.h"

/// Mirrors the server's claw status vocabulary (ClawHostAPI packages/shared/src/clawStatus.ts):
/// initializing, starting, running, stopping, off, stopped, deleting, migrating, rebuilding,
/// unreachable, unknown, creating, configuring, restarting, error, and a pending-record status
/// (see Pending). Statuses the server really emits used to land in Unknown, which also kept the
/// poll below from ever starting.
enum class ClawStatus {
    Running,
    Configuring,      // creating / configuring / initializing / starting — the provisioning path
    Restarting,
    Stopping,
    Updating,         // migrating / rebuilding
    Deleting,
    Pending,          // a record the server lists before it has started creating the server
    Stopped,          // stopped / off
    Unreachable,
    Error,
    Unknown
};

inline QString clawStatusName(ClawStatus s) {
    switch (s) {
        case ClawStatus::Running: return "Running";
        case ClawStatus::Configuring: return "Configuring";
        case ClawStatus::Restarting: return "Restarting";
        case ClawStatus::Stopping: return "Stopping";
        case ClawStatus::Updating: return "Updating";
        case ClawStatus::Deleting: return "Deleting";
        case ClawStatus::Pending: return "Pending";
        case ClawStatus::Stopped: return "Stopped";
        case ClawStatus::Unreachable: return "Unreachable";
        case ClawStatus::Error: return "Error";
        default: return "Unknown";
    }
}

inline QColor clawStatusColor(ClawStatus s) {
    switch (s) {
        case ClawStatus::Running: return AppColors::success;
        case ClawStatus::Configuring:
        case ClawStatus::Restarting:
        case ClawStatus::Stopping:
        case ClawStatus::Deleting:
        case ClawStatus::Pending: return AppColors::warning;
        case ClawStatus::Updating: return AppColors::info;
        case ClawStatus::Stopped: return AppColors::textMuted;
        case ClawStatus::Unreachable:
        case ClawStatus::Error: return AppColors::error;
        default: return AppColors::textMuted;
    }
}

/// True while the server is still doing something to the instance. Pending is left out: nothing
/// has been started on it yet, so an animated dot would be a lie.
inline bool clawStatusShouldPulse(ClawStatus s) {
    switch (s) {
        case ClawStatus::Running:
        case ClawStatus::Configuring:
        case ClawStatus::Restarting:
        case ClawStatus::Stopping:
        case ClawStatus::Updating:
        case ClawStatus::Deleting: return true;
        default: return false;
    }
}

/// "Is this claw in flight?" — every status that changes on its own, so the list keeps polling
/// until it settles. Pending counts: the server turns the record into `creating`, or drops it,
/// with nothing to tell us about it.
inline bool clawStatusIsTransitional(ClawStatus s) {
    switch (s) {
        case ClawStatus::Configuring:
        case ClawStatus::Restarting:
        case ClawStatus::Stopping:
        case ClawStatus::Updating:
        case ClawStatus::Deleting:
        case ClawStatus::Pending: return true;
        default: return false;
    }
}

inline ClawStatus clawStatusFromString(const QString &s) {
    auto lower = s.toLower();
    if (lower == "running") return ClawStatus::Running;
    if (lower == "configuring" || lower == "creating" || lower == "starting" || lower == "initializing")
        return ClawStatus::Configuring;
    if (lower == "restarting") return ClawStatus::Restarting;
    if (lower == "stopping") return ClawStatus::Stopping;
    if (lower == "migrating" || lower == "rebuilding") return ClawStatus::Updating;
    if (lower == "deleting") return ClawStatus::Deleting;
    // The server's name for a record it has not started creating yet.
    if (lower == "awaiting_payment") return ClawStatus::Pending;
    if (lower == "stopped" || lower == "off") return ClawStatus::Stopped;
    if (lower == "unreachable") return ClawStatus::Unreachable;
    if (lower == "error" || lower == "failed") return ClawStatus::Error;
    return ClawStatus::Unknown;
}

/// Providers whose own status strings are passed through unmapped can still produce something we
/// have never seen. Show that string rather than hiding it behind "Unknown".
inline QString clawStatusLabel(ClawStatus s, const QString &raw) {
    if (s != ClawStatus::Unknown || raw.trimmed().isEmpty()) return clawStatusName(s);
    QString pretty = raw.trimmed();
    pretty.replace('_', ' ');
    pretty.replace('-', ' ');
    pretty[0] = pretty[0].toUpper();
    return pretty;
}

struct Claw {
    QString id;
    QString name = "OpenClaw Instance";
    QString provider = "hetzner";
    QString statusRaw;
    QString planId;
    QString ipAddress;
    QString createdAt;
    QString expiresAt;
    int cpu = 0;
    int memory = 0;
    int storage = 0;
    QString region;
    QString subdomain;
    QString gatewayToken;
    QString providerServerId;
    bool gatewayReady = false;
    bool sslReady = false;
    bool storageMountEnabled = false;
    QString storageConfigId;

    ClawStatus status() const { return clawStatusFromString(statusRaw); }
    QString statusLabel() const { return clawStatusLabel(status(), statusRaw); }
    bool isActive() const { return status() == ClawStatus::Running; }
    bool isConfiguring() const { return status() == ClawStatus::Configuring; }
    /// Still settling server-side — start/stop/restart/delete and pending records all land here.
    bool isTransitioning() const { return clawStatusIsTransitional(status()); }
    QString specs() const { return QString("%1 vCPUs • %2GB RAM • %3GB SSD").arg(cpu).arg(memory).arg(storage); }
    QString displayPlan() const { return planId.toUpper(); }

    // providerServerId is "{appName}:{machineId}" for Fly.io — only the app name is the host.
    QString flyAppName() const { return providerServerId.split(':', Qt::SkipEmptyParts).value(0, providerServerId); }

    QString gatewayUrl() const {
        // Local/custom instances: ipAddress stores the full gateway URL directly
        if (provider == "local" && !ipAddress.isEmpty()) return ipAddress;

        // fly.dev is in the list because Fly.io apps are only reachable on their native
        // domain — the digitalenginecore.com subdomain is never provisioned for them.
        bool needsNative = ipAddress.contains("code.run") || ipAddress.contains("onrender.com") ||
                           ipAddress.contains("vercel.run") || ipAddress.contains("workers.dev") ||
                           ipAddress.contains("fly.dev");
        if (needsNative) return providerUrl();
        // Fly.io: use native fly.dev domain directly
        if (provider == "flyio" && !providerServerId.isEmpty())
            return QString("https://%1.fly.dev").arg(flyAppName());
        if (!subdomain.isEmpty()) return QString("https://%1.digitalenginecore.com").arg(subdomain);
        return providerUrl();
    }

    QString providerUrl() const {
        if (ipAddress.isEmpty()) return {};
        QStringList containerDomains = {"railway.app", "fly.dev", "exe.xyz",
            "onrender.com", "code.run", "vercel.run", "workers.dev"};
        for (const auto &d : containerDomains)
            if (ipAddress.contains(d)) return "https://" + ipAddress;
        QRegularExpression ipPattern(R"(^\d{1,3}(\.\d{1,3}){3}$)");
        if (ipPattern.match(ipAddress).hasMatch()) {
            if (provider == "flyio" && !providerServerId.isEmpty())
                return QString("https://%1.fly.dev").arg(flyAppName());
            return "https://" + ipAddress + ":18789";
        }
        return {};
    }

    bool hasGateway() const { return !subdomain.isEmpty() || !ipAddress.isEmpty(); }

    static Claw fromJson(const QJsonObject &obj) {
        Claw c;
        c.id = obj["id"].toString();
        c.name = obj["name"].toString("OpenClaw Instance");
        c.provider = obj["provider"].toString("hetzner");
        c.statusRaw = obj["status"].toString();
        c.planId = obj["planId"].toString();
        c.ipAddress = obj["ip"].toString();
        c.createdAt = obj["createdAt"].toString();
        c.expiresAt = obj["expiresAt"].toString();
        c.cpu = obj["cpu"].toInt();
        c.memory = obj["memory"].toInt();
        c.storage = obj["storage"].toInt();
        c.region = obj["region"].toString();
        c.subdomain = obj["subdomain"].toString();
        c.gatewayToken = obj["gatewayToken"].toString();
        c.providerServerId = obj["providerServerId"].toString();
        c.gatewayReady = obj["gatewayReady"].toBool();
        c.sslReady = obj["sslReady"].toBool();
        c.storageMountEnabled = obj["storageMountEnabled"].toBool();
        c.storageConfigId = obj["storageConfigId"].toString();
        return c;
    }
};

Q_DECLARE_METATYPE(Claw)
