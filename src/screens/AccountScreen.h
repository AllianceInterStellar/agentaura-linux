#pragma once
#include <QWidget>

class QVBoxLayout;

class AccountScreen : public QWidget {
    Q_OBJECT
public:
    explicit AccountScreen(QWidget *parent = nullptr);

    /// Redraw for the current session. The screen is constructed before anyone is signed in, so
    /// without this it keeps showing the empty user it was built with.
    void reload();

signals:
    void signedOut();

private:
    void buildContent();

    QVBoxLayout *m_contentLayout = nullptr;
};
