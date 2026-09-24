#pragma once
#include <QString>
#include <QList>
#include <cmath>

struct AppUser {
    QString id;
    QString email;
    QString phoneNumber;
    QString displayName;
    QString photoUrl;
    bool isAnonymous = false;
    QString createdAt;

    QString displayIdentifier() const {
        if (!displayName.isEmpty()) return displayName;
        if (!email.isEmpty()) return email;
        if (!phoneNumber.isEmpty()) return phoneNumber;
        if (isAnonymous) return generateGuestName(id);
        return "User";
    }

    static QString generateGuestName(const QString &uid) {
        static const QStringList adjectives = {
            "Happy", "Brave", "Swift", "Clever", "Bright",
            "Calm", "Cool", "Lucky", "Noble", "Wise",
            "Cosmic", "Cyber", "Neon", "Pixel", "Turbo",
            "Super", "Mega", "Ultra", "Epic", "Astro"
        };
        static const QStringList nouns = {
            "Claw", "Fox", "Wolf", "Eagle", "Tiger",
            "Panda", "Dragon", "Phoenix", "Falcon", "Lion",
            "Shark", "Bear", "Hawk", "Cobra", "Raven",
            "Panther", "Ninja", "Pilot", "Knight", "Wizard"
        };
        int hash = 0;
        for (auto ch : uid) hash = ((hash << 5) - hash) + ch.unicode();
        int adjIdx = std::abs(hash) % adjectives.size();
        int nounIdx = std::abs(hash / adjectives.size()) % nouns.size();
        return adjectives[adjIdx] + " " + nouns[nounIdx];
    }
};
