#pragma once
#include <QString>
#include <QStringList>

enum class ProvisionStep {
    Creating, Booting, WaitingSSH, InstallingDeps,
    CloningRepo, ConfiguringNginx, SettingUpSSL, Complete
};

inline QString provisionStepName(ProvisionStep s) {
    switch (s) {
        case ProvisionStep::Creating: return "Creating server";
        case ProvisionStep::Booting: return "Booting up";
        case ProvisionStep::WaitingSSH: return "Waiting for SSH";
        case ProvisionStep::InstallingDeps: return "Installing dependencies";
        case ProvisionStep::CloningRepo: return "Cloning repository";
        case ProvisionStep::ConfiguringNginx: return "Configuring Nginx";
        case ProvisionStep::SettingUpSSL: return "Setting up SSL";
        case ProvisionStep::Complete: return "Complete";
    }
    return "Unknown";
}

struct LogEntry {
    QString timestamp;
    QString message;
    bool isError = false;
};

    void fetchRegions(const QString &provider, std::function<void(QList<RegionInfo>)> onSuccess, std::function<void(QString)> onError);
    void syncProviderConfig(const QString &provider, const QString &token,
                            std::function<void()> onSuccess, std::function<void(QString)> onError);
    void loadProviderConfig(const QString &provider,
                            std::function<void(QString)> onSuccess, std::function<void(QString)> onError);
    void deleteProviderConfig(const QString &provider,
                              std::function<void()> onSuccess, std::function<void(QString)> onError);

private:
    explicit ApiClient(QObject *parent = nullptr);
    QNetworkReply *request(const QString &method, const QString &path, const QByteArray &body = {});
    static const QString BASE_URL;

    QNetworkAccessManager m_nam;
    QString m_authToken;
};
