#pragma once
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QList>
#include "models/ApiModels.h"

class DeployScreen : public QWidget {
    Q_OBJECT
public:
    explicit DeployScreen(QWidget *parent = nullptr);

    /// Load the server sizes and regions for the selected provider. Called once a session
    /// exists — the screen is built before sign-in, when those fetches would only 401.
    void reload();

signals:
    void deployStarted();
    void goBack();

private:
    void setupUi();
    void onProviderChanged(int index);
    void onDeploy();
    void showLoadError(const QString &what, const QString &err);
    /// The server refused a second agent: open allianceinterstellar.com and say why.
    void showAgentLimit();

    QComboBox *m_providerCombo = nullptr;
    QComboBox *m_planCombo = nullptr;
    QComboBox *m_regionCombo = nullptr;
    QComboBox *m_agentCombo = nullptr;
    QComboBox *m_aiProviderCombo = nullptr;
    QLabel *m_aiProviderLabel = nullptr;
    QLabel *m_apiKeyLabel = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    /// Show the AI-credential fields only for agents that cannot sign in with an account of their
    /// own — for the others the server accepts the deployment without a key.
    void onAgentChanged();
    bool agentNeedsApiKey() const;
    QLineEdit *m_nameEdit = nullptr;
    QPushButton *m_npmBtn = nullptr;
    QPushButton *m_dockerBtn = nullptr;
    QPushButton *m_deployBtn = nullptr;
    QWidget *m_planContainer = nullptr;
    QLabel *m_loadError = nullptr;

    QLabel *sectionLabel(const QString &text);
    QString comboStyle();
    void styleMethodButtons();

    QString m_selectedProvider;
    /// Bumped on every provider switch; a plans/regions reply from an older generation is dropped.
    quint64 m_fetchGeneration = 0;
    DeployMethod m_deployMethod = DeployMethod::Npm;
    QList<PlanInfo> m_plans;
    QList<RegionInfo> m_regions;
};
