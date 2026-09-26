#pragma once
#include <QString>
#include <QList>
#include <QJsonObject>

/// One server size the provider offers (GET /plans). The server is created in the user's own
/// cloud account, so this is the machine's shape, nothing more.
struct PlanInfo {
    QString id;
    QString name;
    int cpu = 0;
    int memory = 0;
    int storage = 0;

    /// GET /plans names the disk size `disk`. Reading `storage` returned 0, so every size in the
    /// picker read "0GB".
    static PlanInfo fromJson(const QJsonObject &obj) {
        return {obj["id"].toString(), obj["name"].toString(),
                obj["cpu"].toInt(), obj["memory"].toInt(), obj["disk"].toInt()};
    }
};

struct RegionInfo {
    QString id;
    QString name;
    QString city;
    QString country;

    static RegionInfo fromJson(const QJsonObject &obj) {
        return {obj["id"].toString(), obj["name"].toString(),
                obj["city"].toString(), obj["country"].toString()};
    }
};

struct CloudProvider {
    QString id;
    QString name;

    static QList<CloudProvider> all() {
        return {
            {"hetzner", "Hetzner"}, {"vultr", "Vultr"},
            {"digitalocean", "DigitalOcean"}, {"linode", "Linode"},
            {"railway", "Railway"}, {"flyio", "Fly.io"},
            {"gcp", "Google Cloud"}, {"aws", "AWS"},
            {"azure", "Azure"}, {"ovhcloud", "OVHcloud"},
        };
    }
};

enum class DeployMethod { Npm, Docker };

inline QString deployMethodName(DeployMethod m) {
    return m == DeployMethod::Npm ? "NPM" : "Docker";
}

inline QString deployMethodValue(DeployMethod m) {
    return m == DeployMethod::Npm ? "npm" : "docker";
}
