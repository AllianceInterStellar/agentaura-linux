#pragma once
#include <QWidget>
#include <QTimer>
#include "models/Claw.h"

class StatusBadge : public QWidget {
    Q_OBJECT
public:
    /// `label` overrides the enum's name — the caller passes the raw server status when we did
    /// not recognise it, so an unhandled status is readable instead of a bare "Unknown".
    explicit StatusBadge(ClawStatus status, const QString &label = {}, QWidget *parent = nullptr);
    void setStatus(ClawStatus status, const QString &label = {});

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString text() const;

    ClawStatus m_status = ClawStatus::Unknown;
    QString m_label;
    QTimer m_pulseTimer;
    qreal m_pulseAlpha = 1.0;
    bool m_pulseGrowing = false;
};
