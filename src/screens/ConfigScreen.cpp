#include "screens/ConfigScreen.h"
#include "models/ApiModels.h"
#include "services/ApiClient.h"
#include "theme/AppColors.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QMessageBox>

namespace {
const char *kDotConnected = "color: #4CAF50; font-size: 10px; background: transparent; border: none;";
const char *kDotIdle = "color: #48484A; font-size: 10px; background: transparent; border: none;";
}

ConfigScreen::ConfigScreen(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(24, 20, 24, 16);
    auto *title = new QLabel("Provider Settings", this);
    title->setStyleSheet("font-size: 22px; font-weight: bold; color: white; background: transparent; border: none;");
    header->addWidget(title);
    header->addStretch();
    root->addLayout(header);

    auto *desc = new QLabel("Configure API tokens for your cloud providers", this);
    desc->setContentsMargins(24, 0, 24, 12);
    desc->setStyleSheet("font-size: 13px; color: #8E8E93; background: transparent; border: none;");
    root->addWidget(desc);

    // Only shown when the saved tokens could not be fetched — otherwise every card would look
    // like an unconfigured provider and the user would have no idea why.
    m_loadStatus = new QLabel(this);
    m_loadStatus->setWordWrap(true);
    m_loadStatus->setVisible(false);
    m_loadStatus->setContentsMargins(24, 0, 24, 12);
    m_loadStatus->setStyleSheet("font-size: 12px; color: #EF5350; background: transparent; border: none;");
    root->addWidget(m_loadStatus);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *content = new QWidget();
    auto *vbox = new QVBoxLayout(content);
    vbox->setContentsMargins(24, 0, 24, 24);
    vbox->setSpacing(12);

    for (const auto &p : CloudProvider::all())
        addProviderCard(vbox, p.id, p.name);

    vbox->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll);
}

void ConfigScreen::reload() {
    // Empty the fields before asking: after an account switch a failed fetch only shows a banner,
    // and the previous user's provider tokens would otherwise stay in the inputs — readable with
    // the eye toggle — as if they belonged to the account now signed in.
    for (auto it = m_tokenEdits.cbegin(); it != m_tokenEdits.cend(); ++it) {
        it.value()->clear();
        setConnected(it.key(), false);
    }

    ApiClient::instance().fetchProviderConfigs(
        [this](QMap<QString, QString> tokens) {
            m_loadStatus->setVisible(false);
            for (auto it = m_tokenEdits.cbegin(); it != m_tokenEdits.cend(); ++it) {
                const QString token = tokens.value(it.key());
                it.value()->setText(token);
                setConnected(it.key(), !token.isEmpty());
            }
        },
        [this](const QString &err) { showLoadError(err); });
}

void ConfigScreen::setConnected(const QString &id, bool connected) {
    if (auto *dot = m_statusDots.value(id))
        dot->setStyleSheet(connected ? kDotConnected : kDotIdle);
}

void ConfigScreen::showLoadError(const QString &message) {
    m_loadStatus->setText("Couldn't load your saved tokens: " + message);
    m_loadStatus->setVisible(true);
}

void ConfigScreen::addProviderCard(QLayout *layout, const QString &id, const QString &name) {
    auto *card = new QWidget(this);
    card->setStyleSheet(AppColors::cardStyle());
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(14, 14, 14, 14);
    cardLayout->setSpacing(10);

    auto *headerRow = new QHBoxLayout();
    auto *provName = new QLabel(name, card);
    provName->setStyleSheet("font-size: 14px; font-weight: bold; color: white; background: transparent; border: none;");
    headerRow->addWidget(provName);
    headerRow->addStretch();

    auto *statusDot = new QLabel("●", card);
    statusDot->setStyleSheet(kDotIdle);
    statusDot->setObjectName("status_" + id);
    headerRow->addWidget(statusDot);
    cardLayout->addLayout(headerRow);

    auto *inputRow = new QHBoxLayout();
    auto *tokenEdit = new QLineEdit(card);
    tokenEdit->setPlaceholderText("Enter API token...");
    tokenEdit->setEchoMode(QLineEdit::Password);
    tokenEdit->setStyleSheet(AppColors::inputStyle());
    tokenEdit->setFixedHeight(36);
    inputRow->addWidget(tokenEdit, 1);

    auto *toggleBtn = new QPushButton("👁", card);
    toggleBtn->setFixedSize(36, 36);
    toggleBtn->setStyleSheet(
        "QPushButton { background: transparent; border: 1px solid #2A2A2E; border-radius: 8px; font-size: 14px; }"
        "QPushButton:hover { border-color: #EF5350; }");
    connect(toggleBtn, &QPushButton::clicked, this, [tokenEdit]() {
        tokenEdit->setEchoMode(tokenEdit->echoMode() == QLineEdit::Password
                               ? QLineEdit::Normal : QLineEdit::Password);
    });
    inputRow->addWidget(toggleBtn);
    cardLayout->addLayout(inputRow);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    auto *syncBtn = new QPushButton("Sync", card);
    syncBtn->setFixedHeight(32);
    syncBtn->setStyleSheet(AppColors::buttonStyle());
    connect(syncBtn, &QPushButton::clicked, this, [this, id, tokenEdit]() {
        auto token = tokenEdit->text().trimmed();
        if (token.isEmpty()) return;
        ApiClient::instance().syncProviderConfig(id, token,
            [this, id]() { setConnected(id, true); },
            [this](QString err) { QMessageBox::warning(this, "Sync Failed", err); }
        );
    });
    btnRow->addWidget(syncBtn);

    auto *delBtn = new QPushButton("Remove", card);
    delBtn->setFixedHeight(32);
    delBtn->setStyleSheet(
        "QPushButton { background-color: rgba(239,83,80,0.1); color: #EF5350; border: none;"
        " border-radius: 8px; padding: 0 12px; font-size: 12px; }"
        "QPushButton:hover { background-color: rgba(239,83,80,0.2); }");
    connect(delBtn, &QPushButton::clicked, this, [this, id, tokenEdit]() {
        ApiClient::instance().deleteProviderConfig(id,
            [this, id, tokenEdit]() {
                setConnected(id, false);
                tokenEdit->clear();
            },
            [this](QString err) { QMessageBox::warning(this, "Error", err); }
        );
    });
    btnRow->addWidget(delBtn);
    btnRow->addStretch();
    cardLayout->addLayout(btnRow);

    m_tokenEdits.insert(id, tokenEdit);
    m_statusDots.insert(id, statusDot);

    layout->addWidget(card);
}
