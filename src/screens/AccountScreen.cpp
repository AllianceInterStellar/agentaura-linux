#include "screens/AccountScreen.h"
#include "services/AuthState.h"
#include "widgets/SubscriptionBadge.h"
#include "theme/AppColors.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QDesktopServices>
#include <QUrl>
#include "services/ApiClient.h"

AccountScreen::AccountScreen(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(24, 20, 24, 16);
    auto *title = new QLabel("Account", this);
    title->setStyleSheet("font-size: 22px; font-weight: bold; color: white; background: transparent; border: none;");
    header->addWidget(title);
    header->addStretch();
    root->addLayout(header);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *content = new QWidget();
    m_contentLayout = new QVBoxLayout(content);
    m_contentLayout->setContentsMargins(24, 0, 24, 24);
    m_contentLayout->setSpacing(16);

    scroll->setWidget(content);
    root->addWidget(scroll);

    buildContent();
}

void AccountScreen::reload() {
    buildContent();
    refreshSubscription();
}

/// Ask the server what this user actually pays for, then rebuild so the card and the Upgrade
/// button reflect it. Failures leave the last known state alone rather than downgrading the user,
/// but say so on the card — a Pro customer must not be told they are on Free because a request
/// failed.
void AccountScreen::refreshSubscription() {
    if (!AuthState::instance().isLoggedIn()) return;
    ApiClient::instance().fetchSubscription(
        [this](UserSubscription sub) {
            AuthState::instance().setSubscription(sub);
            m_subscriptionError.clear();
            buildContent();
        },
        [this](const QString &err) {
            m_subscriptionError = err;
            buildContent();
        });
}

void AccountScreen::buildContent() {
    auto *vbox = m_contentLayout;
    // Rebuild in place: clear whatever the previous pass added.
    while (QLayoutItem *item = vbox->takeAt(0)) {
        if (QWidget *w = item->widget()) w->deleteLater();
        delete item;
    }
    QWidget *content = vbox->parentWidget();

    auto *profileCard = new QWidget(content);
    profileCard->setStyleSheet(AppColors::cardStyle());
    auto *profileLayout = new QVBoxLayout(profileCard);
    profileLayout->setContentsMargins(16, 16, 16, 16);
    profileLayout->setSpacing(8);

    auto &auth = AuthState::instance();
    auto *avatarRow = new QHBoxLayout();
    auto *avatar = new QLabel("👤", profileCard);
    avatar->setStyleSheet("font-size: 40px; background: transparent; border: none;");
    avatarRow->addWidget(avatar);

    auto *nameCol = new QVBoxLayout();
    auto *nameLabel = new QLabel(auth.user().displayIdentifier(), profileCard);
    nameLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: white; background: transparent; border: none;");
    nameCol->addWidget(nameLabel);

    if (!auth.user().email.isEmpty()) {
        auto *emailLabel = new QLabel(auth.user().email, profileCard);
        emailLabel->setStyleSheet("font-size: 12px; color: #8E8E93; background: transparent; border: none;");
        nameCol->addWidget(emailLabel);
    }
    avatarRow->addLayout(nameCol, 1);

    auto *subBadge = new SubscriptionBadge(auth.subscription().isPro(), profileCard);
    avatarRow->addWidget(subBadge);
    profileLayout->addLayout(avatarRow);
    vbox->addWidget(profileCard);

    auto *subCard = new QWidget(content);
    subCard->setStyleSheet(AppColors::cardStyle());
    auto *subLayout = new QVBoxLayout(subCard);
    subLayout->setContentsMargins(16, 16, 16, 16);
    subLayout->setSpacing(8);

    auto *subTitle = new QLabel("Subscription", subCard);
    subTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: white; background: transparent; border: none;");
    subLayout->addWidget(subTitle);

    auto *tierLabel = new QLabel(subscriptionTierName(auth.subscription().tier) + " Plan", subCard);
    tierLabel->setStyleSheet("font-size: 13px; color: #AEAEB2; background: transparent; border: none;");
    subLayout->addWidget(tierLabel);

    if (!m_subscriptionError.isEmpty()) {
        auto *errLabel = new QLabel("Couldn't check your plan: " + m_subscriptionError, subCard);
        errLabel->setWordWrap(true);
        errLabel->setStyleSheet("font-size: 12px; color: #EF5350; background: transparent; border: none;");
        subLayout->addWidget(errLabel);

        auto *retryBtn = new QPushButton("Retry", subCard);
        retryBtn->setFixedHeight(30);
        retryBtn->setCursor(Qt::PointingHandCursor);
        retryBtn->setStyleSheet(
            "QPushButton { background: transparent; color: #EF5350; border: 1px solid rgba(239,83,80,0.3);"
            " border-radius: 8px; padding: 0 12px; font-size: 12px; }"
            "QPushButton:hover { background-color: rgba(239,83,80,0.15); }");
        connect(retryBtn, &QPushButton::clicked, this, &AccountScreen::refreshSubscription);
        subLayout->addWidget(retryBtn, 0, Qt::AlignLeft);
    }

    if (auth.subscription().isFree()) {
        auto *upgradeBtn = new QPushButton("Upgrade to Pro", subCard);
        upgradeBtn->setFixedHeight(36);
        upgradeBtn->setStyleSheet(AppColors::buttonStyle());
        upgradeBtn->setCursor(Qt::PointingHandCursor);
        // There is no desktop store to buy through, so send the user to the web billing page —
        // the same Stripe checkout the dashboard uses. Previously this button did nothing at all.
        connect(upgradeBtn, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl("https://clawhost.cloud/dashboard/billing"));
        });
        subLayout->addWidget(upgradeBtn);
    }
    vbox->addWidget(subCard);

    struct MenuItem { QString icon; QString text; QString url; };
    QList<MenuItem> items = {
        {"❓", "Help & Support", "https://allianceinterstellar.com"},
        {"🔒", "Privacy Policy", "https://allianceinterstellar.com/legal/privacy"},
        {"📄", "Terms of Service", "https://allianceinterstellar.com/legal/terms"},
        {"💬", "Discord", "https://discord.gg/PkqfnYSmZB"},
    };

    for (const auto &item : items) {
        auto *row = new QPushButton(item.icon + "  " + item.text, content);
        row->setFixedHeight(44);
        row->setCursor(Qt::PointingHandCursor);
        row->setStyleSheet(
            "QPushButton { background-color: #141416; color: white; border: 1px solid #2A2A2E;"
            " border-radius: 10px; text-align: left; padding: 0 16px; font-size: 13px; }"
            "QPushButton:hover { border-color: #EF5350; }");
        connect(row, &QPushButton::clicked, this, [url = item.url]() {
            QDesktopServices::openUrl(QUrl(url));
        });
        vbox->addWidget(row);
    }

    auto *signOutBtn = new QPushButton("🚪  Sign Out", content);
    signOutBtn->setFixedHeight(44);
    signOutBtn->setStyleSheet(
        "QPushButton { background-color: rgba(239,83,80,0.1); color: #EF5350; border: 1px solid rgba(239,83,80,0.3);"
        " border-radius: 10px; text-align: left; padding: 0 16px; font-size: 13px; font-weight: bold; }"
        "QPushButton:hover { background-color: rgba(239,83,80,0.2); }");
    connect(signOutBtn, &QPushButton::clicked, this, [this]() {
        AuthState::instance().logout();
        emit signedOut();
    });
    vbox->addWidget(signOutBtn);

    auto *version = new QLabel("AgentAura v1.0.0 (Qt)", content);
    version->setStyleSheet("font-size: 11px; color: #48484A; background: transparent; border: none;");
    version->setAlignment(Qt::AlignCenter);
    vbox->addWidget(version);
    vbox->addStretch();
}
