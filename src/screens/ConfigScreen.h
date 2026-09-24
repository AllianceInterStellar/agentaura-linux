#pragma once
#include <QMap>
#include <QWidget>

class QLabel;
class QLineEdit;

class ConfigScreen : public QWidget {
    Q_OBJECT
public:
    explicit ConfigScreen(QWidget *parent = nullptr);

    /// Pull the saved provider tokens from the backend and fill the cards in. Called once a
    /// session exists — the screen is built before sign-in, when the fetch would only 401.
    void reload();

private:
    void addProviderCard(QLayout *layout, const QString &id, const QString &name);
    void setConnected(const QString &id, bool connected);
    void showLoadError(const QString &message);

    QMap<QString, QLineEdit *> m_tokenEdits;
    QMap<QString, QLabel *> m_statusDots;
    QLabel *m_loadStatus = nullptr;
};
