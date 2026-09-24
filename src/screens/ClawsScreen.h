#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QMap>
#include <QScrollArea>
#include <QList>
#include "models/Claw.h"

class ClawCard;
class QLabel;
class QPushButton;
class QTimer;

class ClawsScreen : public QWidget {
    Q_OBJECT
public:
    explicit ClawsScreen(QWidget *parent = nullptr);

    /// Fetch the list. `showSpinner` is off for background refreshes (polling, post-action
    /// reloads) so the cards update in place instead of blinking through "Loading…".
    void loadClaws(bool showSpinner = true);

signals:
    void deployRequested();
    void openChat(const Claw &claw);

private:
    void populateClaws(const QList<Claw> &claws);
    void showEmpty();
    void showLoading();
    void showError(const QString &msg);
    void clearList();
    void setBusy(bool busy);
    void runPendingReload();
    /// A refresh that failed while cards are still on screen: say so without a modal, because
    /// polling would otherwise put a dialog in the user's face every few seconds.
    void showRefreshError(const QString &msg);
    /// A claw in a transitional state (provisioning, stopping, restarting, deleting, migrating,
    /// awaiting payment) changes server-side with nothing to tell us about it, so poll while any
    /// of them is mid-flight and stop once they all settle.
    void updatePolling(const QList<Claw> &claws);
    ClawCard *makeCard(const Claw &claw);
    /// Report a failed Start/Stop/Delete. These used to be swallowed, so a rejected action was
    /// indistinguishable from one that worked.
    void reportActionFailure(const QString &action, const QString &err);

    QVBoxLayout *m_listLayout = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_contentWidget = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QLabel *m_refreshError = nullptr;
    QTimer *m_pollTimer = nullptr;
    /// Live cards by claw id, so a refresh can update them in place.
    QMap<QString, ClawCard *> m_cards;
    /// The loading/empty/error widget currently standing in for the list, if any.
    QWidget *m_placeholder = nullptr;
    bool m_loading = false;
    bool m_reloadPending = false;
};
