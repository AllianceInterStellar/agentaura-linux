#include "widgets/ClawCard.h"
#include "widgets/StatusBadge.h"
#include "theme/AppColors.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QPainter>

ClawCard::ClawCard(const Claw &claw, QWidget *parent)
    : QWidget(parent), m_claw(claw) {
    setupUi();
    refresh();
}

void ClawCard::updateClaw(const Claw &claw) {
    m_claw = claw;
    refresh();
}

void ClawCard::setupUi() {
    setStyleSheet(QString("ClawCard { %1 }").arg(AppColors::cardStyle()));
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    auto *headerRow = new QHBoxLayout();
    auto *headerLeft = new QVBoxLayout();

    m_nameLabel = new QLabel(this);
    m_nameLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: white; background: transparent; border: none;");
    headerLeft->addWidget(m_nameLabel);

    auto *providerRow = new QHBoxLayout();
    m_providerLabel = new QLabel(this);
    m_providerLabel->setStyleSheet("font-size: 12px; color: #8E8E93; background: transparent; border: none;");
    providerRow->addWidget(m_providerLabel);
    providerRow->addStretch();
    headerLeft->addLayout(providerRow);

    headerRow->addLayout(headerLeft, 1);
    m_statusBadge = new StatusBadge(m_claw.status(), m_claw.statusLabel(), this);
    headerRow->addWidget(m_statusBadge);
    mainLayout->addLayout(headerRow);

    auto *divider = new QWidget(this);
    divider->setFixedHeight(1);
    divider->setStyleSheet("background-color: #1E1E22; border: none;");
    mainLayout->addWidget(divider);

    m_specsLabel = new QLabel(this);
    m_specsLabel->setStyleSheet("font-size: 12px; color: #AEAEB2; background: transparent; border: none;");
    mainLayout->addWidget(m_specsLabel);

    auto *urlRow = new QHBoxLayout();
    m_urlLabel = new QLabel(this);
    m_urlLabel->setStyleSheet("font-size: 11px; color: #2196F3; background: transparent; border: none;");
    m_urlLabel->setCursor(Qt::PointingHandCursor);
    urlRow->addWidget(m_urlLabel, 1);

    auto *copyBtn = new QPushButton("Copy", this);
    copyBtn->setFixedSize(50, 26);
    copyBtn->setStyleSheet(
        "QPushButton { background: transparent; color: #8E8E93; border: 1px solid #2A2A2E;"
        " border-radius: 6px; font-size: 11px; }"
        "QPushButton:hover { border-color: #EF5350; color: #EF5350; }");
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        auto url = m_claw.gatewayUrl();
        if (!url.isEmpty()) QApplication::clipboard()->setText(url);
    });
    urlRow->addWidget(copyBtn);
    mainLayout->addLayout(urlRow);

    auto *divider2 = new QWidget(this);
    divider2->setFixedHeight(1);
    divider2->setStyleSheet("background-color: #1E1E22; border: none;");
    mainLayout->addWidget(divider2);

    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(8);

    m_startBtn = new QPushButton("▶ Start", this);
    m_startBtn->setFixedHeight(32);
    m_startBtn->setStyleSheet(
        "QPushButton { background-color: rgba(76,175,80,0.1); color: #4CAF50; border: none;"
        " border-radius: 8px; font-size: 12px; font-weight: bold; padding: 0 12px; }"
        "QPushButton:hover { background-color: rgba(76,175,80,0.2); }");
    connect(m_startBtn, &QPushButton::clicked, this, [this]() {
        if (m_actionInFlight) return;
        setActionBusy(true);
        emit startRequested(m_claw.id);
    });
    actionRow->addWidget(m_startBtn);

    m_stopBtn = new QPushButton("■ Stop", this);
    m_stopBtn->setFixedHeight(32);
    m_stopBtn->setStyleSheet(
        "QPushButton { background-color: rgba(255,152,0,0.1); color: #FF9800; border: none;"
        " border-radius: 8px; font-size: 12px; font-weight: bold; padding: 0 12px; }"
        "QPushButton:hover { background-color: rgba(255,152,0,0.2); }");
    connect(m_stopBtn, &QPushButton::clicked, this, [this]() {
        if (m_actionInFlight) return;
        setActionBusy(true);
        emit stopRequested(m_claw.id);
    });
    actionRow->addWidget(m_stopBtn);

    m_chatBtn = new QPushButton("💬 Chat", this);
    m_chatBtn->setFixedHeight(32);
    m_chatBtn->setStyleSheet(
        "QPushButton { background-color: rgba(33,150,243,0.1); color: #2196F3; border: none;"
        " border-radius: 8px; font-size: 12px; font-weight: bold; padding: 0 12px; }"
        "QPushButton:hover { background-color: rgba(33,150,243,0.2); }");
    connect(m_chatBtn, &QPushButton::clicked, this, [this]() {
        auto url = m_claw.gatewayUrl();
        if (!url.isEmpty()) emit chatRequested(m_claw);
    });
    actionRow->addWidget(m_chatBtn);

    actionRow->addStretch();

    m_deleteBtn = new QPushButton("🗑", this);
    m_deleteBtn->setFixedSize(32, 32);
    m_deleteBtn->setStyleSheet(
        "QPushButton { background-color: rgba(239,83,80,0.1); color: #EF5350; border: none;"
        " border-radius: 8px; font-size: 14px; }"
        "QPushButton:hover { background-color: rgba(239,83,80,0.2); }");
    connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
        auto reply = QMessageBox::question(this, "Delete Claw",
            QString("Delete \"%1\"? This action cannot be undone.").arg(m_claw.name),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
        setActionBusy(true);
        emit deleteRequested(m_claw.id);
    });
    actionRow->addWidget(m_deleteBtn);
    mainLayout->addLayout(actionRow);
}

void ClawCard::setActionBusy(bool busy) {
    m_actionInFlight = busy;
    m_startBtn->setEnabled(!busy);
    m_stopBtn->setEnabled(!busy);
    m_deleteBtn->setEnabled(!busy);
}

void ClawCard::refresh() {
    m_nameLabel->setText(m_claw.name);
    m_providerLabel->setText(QString("%1 • %2").arg(m_claw.provider.toUpper(), m_claw.region));
    m_statusBadge->setStatus(m_claw.status(), m_claw.statusLabel());
    m_specsLabel->setText(m_claw.specs());

    auto url = m_claw.gatewayUrl();
    m_urlLabel->setText(url.isEmpty() ? "No URL available" : url);

    bool running = m_claw.isActive();
    m_startBtn->setVisible(!running);
    m_stopBtn->setVisible(running);
    m_chatBtn->setEnabled(!url.isEmpty());
}
