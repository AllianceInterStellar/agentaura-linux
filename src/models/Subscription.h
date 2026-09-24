#pragma once
#include <QString>
#include <QList>

enum class SubscriptionTier { Free, Pro, ProAnnual };

inline QString subscriptionTierName(SubscriptionTier t) {
    switch (t) {
        case SubscriptionTier::Free: return "Free";
        case SubscriptionTier::Pro: return "Pro";
        case SubscriptionTier::ProAnnual: return "Pro Annual";
    }
    return "Free";
}

struct SubscriptionPlan {
    SubscriptionTier tier;
    QString name;
    QString productId;
    double price;
    int maxClaws;
    bool autoBackup;
    bool prioritySupport;
    QStringList features;
};

struct UserSubscription {
    SubscriptionTier tier = SubscriptionTier::Free;
    QString productId;
    QString expiresAt;
    bool isActive = true;

    bool isFree() const { return tier == SubscriptionTier::Free; }
    bool isPro() const { return tier == SubscriptionTier::Pro || tier == SubscriptionTier::ProAnnual; }
};

inline QList<SubscriptionPlan> subscriptionPlans() {
    return {
        {SubscriptionTier::Free, "Free", "", 0.0, 1, false, false,
            {"Manage 1 Claw", "Basic monitoring", "Community support"}},
        {SubscriptionTier::Pro, "Pro", "agentaura_pro_monthly", 4.99, -1, true, true,
            {"Unlimited Claws", "Auto backup", "Priority support", "Advanced monitoring"}},
        {SubscriptionTier::ProAnnual, "Pro Annual", "agentaura_pro_annual", 49.99, -1, true, true,
            {"Everything in Pro", "Billed annually", "Save 17%"}},
    };
}
