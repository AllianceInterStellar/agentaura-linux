#pragma once
#include <QString>
#include <QList>
#include <QJsonObject>

struct PlanInfo {
    QString id;
    QString name;
    int cpu = 0;
    int memory = 0;
    int storage = 0;
    double price = 0;

    /// GET /plans names these `disk` and `priceMonthly`. Reading `storage`/`price` returned 0 for
    /// both, so every plan in the picker read "0GB • $0".
    static PlanInfo fromJson(const QJsonObject &obj) {
        return {obj["id"].toString(), obj["name"].toString(),
                obj["cpu"].toInt(), obj["memory"].toInt(), obj["disk"].toInt(),
                obj["priceMonthly"].toDouble()};
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
