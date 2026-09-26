#include "screens/ClawsScreen.h"
#include "widgets/ClawCard.h"
#include "services/ApiClient.h"
#include "theme/AppColors.h"
#include <QApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QDesktopServices>
#include <QUrl>

namespace {
/// How often to re-poll while an instance is still provisioning.
constexpr int kPollIntervalMs = 8000;
}

ClawsScreen::ClawsScreen(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(24, 20, 24, 16);
    auto *title = new QLabel("My Claws", this);
    title->setStyleSheet("font-size: 22px; font-weight: bold; color: white; background: transparent; border: none;");
    header->addWidget(title);
    header->addStretch();

    m_refreshBtn = new QPushButton("⟳ Refresh", this);
    m_refreshBtn->setFixedHeight(36);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn->setStyleSheet(
        "QPushButton { background: transparent; color: #AEAEB2; border: 1px solid #2A2A2E;"
        " border-radius: 8px; padding: 0 14px; font-size: 13px; }"
        "QPushButton:hover { border-color: #EF5350; color: #EF5350; }"
        "QPushButton:disabled { color: #48484A; border-color: #1E1E22; }");
    connect(m_refreshBtn, &QPushButton::clicked, this, [this]() { loadClaws(false); });
    header->addWidget(m_refreshBtn);
    header->addSpacing(8);

    auto *deployBtn = new QPushButton("+ Deploy", this);
    deployBtn->setFixedHeight(36);
    deployBtn->setStyleSheet(AppColors::buttonStyle());
    connect(deployBtn, &QPushButton::clicked, this, &ClawsScreen::deployRequested);
    header->addWidget(deployBtn);
    root->addLayout(header);

    m_refreshError = new QLabel(this);
    m_refreshError->setWordWrap(true);
    m_refreshError->setVisible(false);
    m_refreshError->setContentsMargins(24, 0, 24, 10);
    m_refreshError->setStyleSheet("font-size: 12px; color: #EF5350; background: transparent; border: none;");
    root->addWidget(m_refreshError);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_contentWidget = new QWidget();
    m_listLayout = new QVBoxLayout(m_contentWidget);
    m_listLayout->setContentsMargins(24, 0, 24, 24);
    m_listLayout->setSpacing(12);
    m_listLayout->addStretch();
    m_scrollArea->setWidget(m_contentWidget);
    root->addWidget(m_scrollArea);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, [this]() {
        // A modal (the delete confirmation) runs its own event loop; refreshing under it could
        // delete the very card the user is answering about.
        if (QApplication::activeModalWidget()) return;
        loadClaws(false);
    });
}

void ClawsScreen::loadClaws(bool showSpinner) {
    // An action (start/stop/delete) asks for a reload the moment it succeeds, which can land
    // while a poll is still in flight; remember it rather than dropping it, or the list keeps
    // showing the state the action just changed.
    if (m_loading) {
        m_reloadPending = true;
        return;
    }
    setBusy(true);
    if (showSpinner) showLoading();
    ApiClient::instance().fetchClaws(
        [this](QList<Claw> claws) {
            setBusy(false);
            populateClaws(claws);
            runPendingReload();
        },
        [this](QString err) {
            setBusy(false);
            // Keep the cards that are already on screen for a failed refresh — wiping a working
            // list because one poll failed is worse than showing a stale one with a warning.
            if (m_cards.isEmpty()) showError(err);
            else showRefreshError(err);
            runPendingReload();
        }
    );
}

void ClawsScreen::runPendingReload() {
    if (!m_reloadPending) return;
    m_reloadPending = false;
    loadClaws(false);
}

void ClawsScreen::setBusy(bool busy) {
    m_loading = busy;
    m_refreshBtn->setEnabled(!busy);
    m_refreshBtn->setText(busy ? "⟳ Refreshing…" : "⟳ Refresh");
}

void ClawsScreen::reportActionFailure(const QString &action, const QString &err) {
    QMessageBox::warning(this, action + " Failed", err);
}

void ClawsScreen::showRefreshError(const QString &msg) {
    m_refreshError->setText("Couldn't refresh — showing the last known state. " + msg);
    m_refreshError->setVisible(true);
}

ClawCard *ClawsScreen::makeCard(const Claw &claw) {
    auto *card = new ClawCard(claw, m_contentWidget);
    // The card disables its own actions when it emits and stays that way until the request comes
    // back, so a second click cannot fire the same action twice. QPointer because a refresh that
    // lands first can drop the card while its request is still out.
    const QPointer<ClawCard> guard(card);
    connect(card, &ClawCard::startRequested, this, [this, guard](const QString &id) {
        ApiClient::instance().startClaw(id,
            [this, guard]() { if (guard) guard->setActionBusy(false); loadClaws(false); },
            [this, guard](QString err) {
                if (guard) guard->setActionBusy(false);
                reportActionFailure("Start", err);
            });
    });
    connect(card, &ClawCard::stopRequested, this, [this, guard](const QString &id) {
        ApiClient::instance().stopClaw(id,
            [this, guard]() { if (guard) guard->setActionBusy(false); loadClaws(false); },
            [this, guard](QString err) {
                if (guard) guard->setActionBusy(false);
                reportActionFailure("Stop", err);
            });
    });
    connect(card, &ClawCard::deleteRequested, this, [this, guard](const QString &id) {
        ApiClient::instance().deleteClaw(id,
            [this, guard]() { if (guard) guard->setActionBusy(false); loadClaws(false); },
            [this, guard](QString err) {
                if (guard) guard->setActionBusy(false);
                reportActionFailure("Delete", err);
            });
    });
    connect(card, &ClawCard::chatRequested, this, [this](const Claw &c) {
        emit openChat(c);
    });
    return card;
}

void ClawsScreen::populateClaws(const QList<Claw> &claws) {
    m_refreshError->setVisible(false);
    if (claws.isEmpty()) {
        showEmpty();
        updatePolling(claws);
        return;
    }

    // Coming from a loading/empty/error state there is nothing to keep; otherwise reuse the
    // existing cards so a refresh does not tear down and rebuild the whole list under the user.
    // The trailing stretch has to go back straight away — every card is inserted in front of it.
    if (m_placeholder) {
        clearList();
        m_listLayout->addStretch();
    }

    QSet<QString> present;
    for (const auto &claw : claws) {
        present.insert(claw.id);
        if (auto *card = m_cards.value(claw.id)) {
            card->updateClaw(claw);
            continue;
        }
        auto *card = makeCard(claw);
        m_cards.insert(claw.id, card);
        m_listLayout->insertWidget(m_listLayout->count() - 1, card);  // before the trailing stretch
    }

    for (auto it = m_cards.begin(); it != m_cards.end();) {
        if (present.contains(it.key())) {
            ++it;
            continue;
        }
        m_listLayout->removeWidget(it.value());
        delete it.value();
        it = m_cards.erase(it);
    }

    updatePolling(claws);
}

void ClawsScreen::updatePolling(const QList<Claw> &claws) {
    // Every transitional status, not just provisioning: stopping/restarting/deleting/migrating and
    // a pending record all change server-side with nothing to tell us about it. Keyed off
    // isConfiguring() alone, a Stop left the card frozen until the user hit Refresh.
    bool settling = false;
    for (const auto &claw : claws)
        if (claw.isTransitioning()) { settling = true; break; }

    if (settling) {
        if (!m_pollTimer->isActive()) m_pollTimer->start();
    } else {
        m_pollTimer->stop();
    }
}

void ClawsScreen::clearList() {
    QLayoutItem *item;
    while ((item = m_listLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    m_cards.clear();
    m_placeholder = nullptr;
}

void ClawsScreen::showEmpty() {
    clearList();

    auto *empty = new QWidget(m_contentWidget);
    auto *vbox = new QVBoxLayout(empty);
    vbox->setAlignment(Qt::AlignCenter);
    vbox->setSpacing(12);

    auto *icon = new QLabel("🚀", empty);
    icon->setStyleSheet("font-size: 48px; background: transparent; border: none;");
    icon->setAlignment(Qt::AlignCenter);
    vbox->addWidget(icon);

    auto *msg = new QLabel("No Claws Yet", empty);
    msg->setStyleSheet("font-size: 18px; font-weight: bold; color: white; background: transparent; border: none;");
    msg->setAlignment(Qt::AlignCenter);
    vbox->addWidget(msg);

    auto *sub = new QLabel("Deploy your first OpenClaw instance to get started", empty);
    sub->setStyleSheet("font-size: 13px; color: #8E8E93; background: transparent; border: none;");
    sub->setAlignment(Qt::AlignCenter);
    vbox->addWidget(sub);

    auto *btn = new QPushButton("Deploy Now", empty);
    btn->setFixedSize(160, 40);
    btn->setStyleSheet(AppColors::buttonStyle());
    connect(btn, &QPushButton::clicked, this, &ClawsScreen::deployRequested);
    vbox->addWidget(btn, 0, Qt::AlignCenter);

    m_listLayout->addWidget(empty);
    m_listLayout->addStretch();
    m_placeholder = empty;
}

void ClawsScreen::showLoading() {
    clearList();
    auto *label = new QLabel("Loading...", m_contentWidget);
    label->setStyleSheet("font-size: 14px; color: #8E8E93; background: transparent; border: none;");
    label->setAlignment(Qt::AlignCenter);
    m_listLayout->addWidget(label);
    m_listLayout->addStretch();
    m_placeholder = label;
}

void ClawsScreen::showError(const QString &msg) {
    // Nothing left on screen to keep fresh, and the Retry button covers recovery.
    m_pollTimer->stop();
    m_refreshError->setVisible(false);
    clearList();

    auto *box = new QWidget(m_contentWidget);
    auto *vbox = new QVBoxLayout(box);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(12);

    auto *label = new QLabel("Error: " + msg, box);
    label->setWordWrap(true);
    label->setStyleSheet("font-size: 14px; color: #EF5350; background: transparent; border: none;");
    label->setAlignment(Qt::AlignCenter);
    vbox->addWidget(label);

    auto *retry = new QPushButton("Retry", box);
    retry->setFixedSize(100, 36);
    retry->setStyleSheet(AppColors::buttonStyle());
    connect(retry, &QPushButton::clicked, this, [this]() { loadClaws(); });
    vbox->addWidget(retry, 0, Qt::AlignCenter);

    m_listLayout->addWidget(box);
    m_listLayout->addStretch();
    m_placeholder = box;
}
