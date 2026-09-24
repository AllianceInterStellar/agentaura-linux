#pragma once
#include <QWidget>
#include "models/Subscription.h"

class SubscriptionBadge : public QWidget {
    Q_OBJECT
public:
    explicit SubscriptionBadge(bool isPro, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_isPro;
};
