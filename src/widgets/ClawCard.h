#pragma once
#include <QWidget>
#include "models/Claw.h"

class ClawCard : public QWidget {
    Q_OBJECT
public:
    explicit ClawCard(const Claw &claw, QWidget *parent = nullptr);
    void updateClaw(const Claw &claw);

    /// Grey out the start/stop/delete actions while one of them is still in flight, so a second
    /// click cannot submit the same action twice.
    void setActionBusy(bool busy);

signals:
    void startRequested(const QString &id);
    void stopRequested(const QString &id);
    void deleteRequested(const QString &id);
    void chatRequested(const Claw &claw);

private:
    void setupUi();
    void refresh();

    Claw m_claw;
    class QLabel *m_nameLabel;
    class QLabel *m_providerLabel;
    class QLabel *m_specsLabel;
    class QLabel *m_urlLabel;
    class QPushButton *m_startBtn;
    class QPushButton *m_stopBtn;
    class QPushButton *m_chatBtn;
    class QPushButton *m_deleteBtn;
    class StatusBadge *m_statusBadge;
    bool m_actionInFlight = false;
};
