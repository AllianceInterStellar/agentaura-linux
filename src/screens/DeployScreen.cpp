#include "screens/DeployScreen.h"
#include "services/ApiClient.h"
#include "theme/AppColors.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QMessageBox>

/// Where each provider's agent reads its key from. Mirrors AIProvider.envVarName on the other
/// clients — the server writes the key into this variable on the instance.
static QString aiEnvVarName(const QString &provider);

DeployScreen::DeployScreen(QWidget *parent) : QWidget(parent) {
    setupUi();
}

void DeployScreen::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(24, 20, 24, 16);
    auto *backBtn = new QPushButton("← Back", this);
    backBtn->setStyleSheet("QPushButton { background: transparent; color: #EF5350; border: none; font-size: 13px; }"
                           "QPushButton:hover { color: #E53935; }");
    connect(backBtn, &QPushButton::clicked, this, &DeployScreen::goBack);
    headerRow->addWidget(backBtn);
    auto *title = new QLabel("Deploy OpenClaw", this);
    title->setStyleSheet("font-size: 22px; font-weight: bold; color: white; background: transparent; border: none;");
    headerRow->addWidget(title, 1, Qt::AlignCenter);
    headerRow->addSpacing(60);
    root->addLayout(headerRow);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget();
    auto *form = new QVBoxLayout(content);
    form->setContentsMargins(24, 0, 24, 24);
    form->setSpacing(16);

    form->addWidget(sectionLabel("Instance Name"));
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("my-openclaw");
    m_nameEdit->setStyleSheet(AppColors::inputStyle());
    m_nameEdit->setFixedHeight(40);
    form->addWidget(m_nameEdit);

    form->addWidget(sectionLabel("Cloud Provider"));
    m_providerCombo = new QComboBox(this);
    m_providerCombo->setStyleSheet(comboStyle());
    m_providerCombo->setFixedHeight(40);
    for (const auto &p : CloudProvider::all())
        m_providerCombo->addItem(p.name, p.id);
    connect(m_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DeployScreen::onProviderChanged);
    form->addWidget(m_providerCombo);

    form->addWidget(sectionLabel("Plan"));
    m_planCombo = new QComboBox(this);
    m_planCombo->setStyleSheet(comboStyle());
    m_planCombo->setFixedHeight(40);
    form->addWidget(m_planCombo);

    form->addWidget(sectionLabel("Region"));
    m_regionCombo = new QComboBox(this);
    m_regionCombo->setStyleSheet(comboStyle());
    m_regionCombo->setFixedHeight(40);
    form->addWidget(m_regionCombo);

    form->addWidget(sectionLabel("Agent"));
    m_agentCombo = new QComboBox(this);
    m_agentCombo->setStyleSheet(comboStyle());
    m_agentCombo->setFixedHeight(40);
    // The agents that can authenticate with their own account need no key; OpenClaw does. The
    // server enforces exactly this rule, so an agent missing here would be refused at deploy time.
    m_agentCombo->addItem("Claude Code", "claudecode");
    m_agentCombo->addItem("Codex", "codex");
    m_agentCombo->addItem("GitHub Copilot", "copilot");
    m_agentCombo->addItem("Gemini CLI", "gemini");
    m_agentCombo->addItem("Kiro", "kiro");
    m_agentCombo->addItem("OpenClaw (bring your own API key)", "openclaw");
    connect(m_agentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DeployScreen::onAgentChanged);
    form->addWidget(m_agentCombo);

    m_aiProviderLabel = sectionLabel("AI Provider");
    form->addWidget(m_aiProviderLabel);
    m_aiProviderCombo = new QComboBox(this);
    m_aiProviderCombo->setStyleSheet(comboStyle());
    m_aiProviderCombo->setFixedHeight(40);
    // id, display, env var — same four the iOS/Android/WinUI clients offer.
    m_aiProviderCombo->addItem("Claude (Anthropic)", "claude");
    m_aiProviderCombo->addItem("GPT (OpenAI)", "openai");
    m_aiProviderCombo->addItem("Gemini (Google)", "gemini");
    m_aiProviderCombo->addItem("Other (xAI & more)", "other");
    form->addWidget(m_aiProviderCombo);

    m_apiKeyLabel = sectionLabel("AI API Key");
    form->addWidget(m_apiKeyLabel);
    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setPlaceholderText("sk-...");
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setStyleSheet(AppColors::inputStyle());
    m_apiKeyEdit->setFixedHeight(40);
    form->addWidget(m_apiKeyEdit);
    onAgentChanged();

    form->addWidget(sectionLabel("Deploy Method"));
    auto *methodRow = new QHBoxLayout();
    methodRow->setSpacing(8);
    m_npmBtn = new QPushButton("NPM", this);
    m_npmBtn->setFixedHeight(36);
    m_npmBtn->setCheckable(true);
    m_npmBtn->setChecked(true);
    m_dockerBtn = new QPushButton("Docker", this);
    m_dockerBtn->setFixedHeight(36);
    m_dockerBtn->setCheckable(true);
    styleMethodButtons();

    connect(m_npmBtn, &QPushButton::clicked, this, [this]() {
        m_deployMethod = DeployMethod::Npm;
        m_npmBtn->setChecked(true);
        m_dockerBtn->setChecked(false);
        styleMethodButtons();
    });
    connect(m_dockerBtn, &QPushButton::clicked, this, [this]() {
        m_deployMethod = DeployMethod::Docker;
        m_npmBtn->setChecked(false);
        m_dockerBtn->setChecked(true);
        styleMethodButtons();
    });
    methodRow->addWidget(m_npmBtn);
    methodRow->addWidget(m_dockerBtn);
    methodRow->addStretch();
    form->addLayout(methodRow);

    // Without this the plan and region pickers just sit there empty when their fetch fails.
    m_loadError = new QLabel(this);
    m_loadError->setWordWrap(true);
    m_loadError->setVisible(false);
    m_loadError->setStyleSheet("font-size: 12px; color: #EF5350; background: transparent; border: none;");
    form->addWidget(m_loadError);

    form->addSpacing(12);
    m_deployBtn = new QPushButton("🚀 Deploy Now", this);
    m_deployBtn->setFixedHeight(44);
    m_deployBtn->setStyleSheet(AppColors::buttonStyle());
    connect(m_deployBtn, &QPushButton::clicked, this, &DeployScreen::onDeploy);
    form->addWidget(m_deployBtn);
    form->addStretch();

    scroll->setWidget(content);
    root->addWidget(scroll);

    // Pick the first provider but do not fetch yet: nobody is signed in while this is built.
    if (m_providerCombo->count() > 0)
        m_selectedProvider = m_providerCombo->itemData(0).toString();
}

void DeployScreen::reload() {
    if (m_providerCombo->count() > 0) onProviderChanged(m_providerCombo->currentIndex());
}

void DeployScreen::showLoadError(const QString &what, const QString &err) {
    m_loadError->setText(QString("Couldn't load %1: %2").arg(what, err));
    m_loadError->setVisible(true);
}

void DeployScreen::onProviderChanged(int index) {
    m_selectedProvider = m_providerCombo->itemData(index).toString();
    m_planCombo->clear();
    m_regionCombo->clear();
    m_loadError->setVisible(false);

    // Switching provider while a fetch is still out must not let the old provider's late reply
    // land in the pickers that were just cleared for the new one — the user would otherwise be
    // able to deploy provider B with provider A's planId/region.
    const quint64 generation = ++m_fetchGeneration;

    ApiClient::instance().fetchPlans(m_selectedProvider,
        [this, generation](QList<PlanInfo> plans) {
            if (generation != m_fetchGeneration) return;
            m_plans = plans;
            for (const auto &p : plans)
                m_planCombo->addItem(QString("%1 — %2 vCPU / %3GB RAM ($%4/mo)")
                    .arg(p.name).arg(p.cpu).arg(p.memory).arg(p.price, 0, 'f', 2), p.id);
        },
        [this, generation](QString err) {
            if (generation != m_fetchGeneration) return;
            showLoadError("plans", err);
        }
    );

    ApiClient::instance().fetchRegions(m_selectedProvider,
        [this, generation](QList<RegionInfo> regions) {
            if (generation != m_fetchGeneration) return;
            m_regions = regions;
            for (const auto &r : regions)
                m_regionCombo->addItem(QString("%1, %2").arg(r.city, r.country), r.id);
        },
        [this, generation](QString err) {
            if (generation != m_fetchGeneration) return;
            showLoadError("regions", err);
        }
    );
}

void DeployScreen::onDeploy() {
    auto name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) { QMessageBox::warning(this, "Error", "Please enter an instance name."); return; }
    auto planId = m_planCombo->currentData().toString();
    auto regionId = m_regionCombo->currentData().toString();
    if (planId.isEmpty()) { QMessageBox::warning(this, "Error", "Please select a plan."); return; }

    const auto appType = m_agentCombo->currentData().toString();
    ApiClient::AiModelConfig aiConfig;
    if (agentNeedsApiKey()) {
        if (m_apiKeyEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "Error",
                "OpenClaw needs an AI API key. Enter one, or pick an agent that signs in with its "
                "own account.");
            return;
        }
        aiConfig.provider = m_aiProviderCombo->currentData().toString();
        aiConfig.apiKey = m_apiKeyEdit->text();
        aiConfig.envVarName = aiEnvVarName(aiConfig.provider);
    }

    m_deployBtn->setEnabled(false);
    m_deployBtn->setText("Deploying...");

    ApiClient::instance().createClaw(name, m_selectedProvider, planId, regionId,
        deployMethodValue(m_deployMethod), appType, aiConfig,
        [this](Claw) {
            m_deployBtn->setEnabled(true);
            m_deployBtn->setText("🚀 Deploy Now");
            emit deployStarted();
            emit goBack();
        },
        [this](QString err) {
            m_deployBtn->setEnabled(true);
            m_deployBtn->setText("🚀 Deploy Now");
            QMessageBox::critical(this, "Deploy Failed", err);
        }
    );
}

bool DeployScreen::agentNeedsApiKey() const {
    // Only OpenClaw has no account login of its own, so only it must carry an explicit key.
    return m_agentCombo && m_agentCombo->currentData().toString() == "openclaw";
}

void DeployScreen::onAgentChanged() {
    const bool needsKey = agentNeedsApiKey();
    for (QWidget *w : {static_cast<QWidget *>(m_aiProviderLabel),
                       static_cast<QWidget *>(m_aiProviderCombo),
                       static_cast<QWidget *>(m_apiKeyLabel),
                       static_cast<QWidget *>(m_apiKeyEdit)}) {
        if (w) w->setVisible(needsKey);
    }
}

static QString aiEnvVarName(const QString &provider) {
    if (provider == "claude") return "ANTHROPIC_API_KEY";
    if (provider == "openai") return "OPENAI_API_KEY";
    if (provider == "gemini") return "GEMINI_API_KEY";
    return "XAI_API_KEY";
}

QLabel *DeployScreen::sectionLabel(const QString &text) {
    auto *label = new QLabel(text, this);
    label->setStyleSheet("font-size: 12px; font-weight: bold; color: #AEAEB2; background: transparent; border: none;");
    return label;
}

QString DeployScreen::comboStyle() {
    return QStringLiteral(
        "QComboBox {"
        "  background-color: #0A0A0B; color: #FFFFFF; border: 1px solid #2A2A2E;"
        "  border-radius: 10px; padding: 8px 14px; font-size: 13px;"
        "}"
        "QComboBox:focus { border-color: #EF5350; }"
        "QComboBox::drop-down { border: none; width: 30px; }"
        "QComboBox::down-arrow { image: none; border-left: 5px solid transparent;"
        "  border-right: 5px solid transparent; border-top: 6px solid #8E8E93;"
        "  margin-right: 10px; }"
        "QComboBox QAbstractItemView {"
        "  background-color: #141416; color: white; border: 1px solid #2A2A2E;"
        "  selection-background-color: #EF5350; selection-color: white;"
        "}"
    );
}

void DeployScreen::styleMethodButtons() {
    auto active = QStringLiteral(
        "QPushButton { background-color: #EF5350; color: white; border: none;"
        " border-radius: 8px; padding: 0 16px; font-weight: bold; font-size: 12px; }");
    auto inactive = QStringLiteral(
        "QPushButton { background-color: transparent; color: #8E8E93; border: 1px solid #2A2A2E;"
        " border-radius: 8px; padding: 0 16px; font-size: 12px; }"
        "QPushButton:hover { border-color: #EF5350; color: #EF5350; }");
    m_npmBtn->setStyleSheet(m_npmBtn->isChecked() ? active : inactive);
    m_dockerBtn->setStyleSheet(m_dockerBtn->isChecked() ? active : inactive);
}
