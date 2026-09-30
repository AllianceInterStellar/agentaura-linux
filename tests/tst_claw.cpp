// SPDX-License-Identifier: MIT
//
// The claw record: status vocabulary and where its gateway is reached.
#include "models/Claw.h"

#include <QtTest>

class TstClaw : public QObject {
    Q_OBJECT
private slots:
    void mapsServerStatuses_data() {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("transitional");
        const auto row = [](const char *raw, ClawStatus s, bool t) {
            QTest::addRow("%s", raw) << QString(raw) << int(s) << t;
        };
        row("running", ClawStatus::Running, false);
        row("RUNNING", ClawStatus::Running, false);
        row("creating", ClawStatus::Configuring, true);
        row("initializing", ClawStatus::Configuring, true);
        row("starting", ClawStatus::Configuring, true);
        row("restarting", ClawStatus::Restarting, true);
        row("stopping", ClawStatus::Stopping, true);
        row("migrating", ClawStatus::Updating, true);
        row("rebuilding", ClawStatus::Updating, true);
        row("deleting", ClawStatus::Deleting, true);
        row("awaiting_payment", ClawStatus::Pending, true);
        row("off", ClawStatus::Stopped, false);
        row("stopped", ClawStatus::Stopped, false);
        row("unreachable", ClawStatus::Unreachable, false);
        row("failed", ClawStatus::Error, false);
        row("something_new", ClawStatus::Unknown, false);
    }
    void mapsServerStatuses() {
        QFETCH(QString, raw);
        QFETCH(int, status);
        QFETCH(bool, transitional);
        QCOMPARE(int(clawStatusFromString(raw)), status);
        QCOMPARE(clawStatusIsTransitional(clawStatusFromString(raw)), transitional);
    }

    void showsAnUnknownStatusAsSent() {
        QCOMPARE(clawStatusLabel(ClawStatus::Unknown, "being-provisioned_now"),
                 QString("Being provisioned now"));
        QCOMPARE(clawStatusLabel(ClawStatus::Unknown, "  "), QString("Unknown"));
        QCOMPARE(clawStatusLabel(ClawStatus::Running, "running"), QString("Running"));
    }

    void findsTheGateway_data() {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<QString>("ip");
        QTest::addColumn<QString>("subdomain");
        QTest::addColumn<QString>("serverId");
        QTest::addColumn<QString>("url");
        QTest::addRow("subdomain wins for a VM")
            << "hetzner" << "203.0.113.7" << "abc" << "" << "https://abc.digitalenginecore.com";
        QTest::addRow("bare IP without subdomain")
            << "hetzner" << "203.0.113.7" << "" << "" << "https://203.0.113.7:18789";
        QTest::addRow("fly uses its native domain")
            << "flyio" << "" << "abc" << "my-app:machine-1" << "https://my-app.fly.dev";
        QTest::addRow("fly host in ip")
            << "flyio" << "my-app.fly.dev" << "abc" << "" << "https://my-app.fly.dev";
        QTest::addRow("railway host")
            << "railway" << "svc.up.railway.app" << "" << "" << "https://svc.up.railway.app";
        QTest::addRow("local stores the URL itself")
            << "local" << "http://10.0.0.5:18789" << "" << "" << "http://10.0.0.5:18789";
        QTest::addRow("nothing to reach") << "hetzner" << "" << "" << "" << "";
    }
    void findsTheGateway() {
        QFETCH(QString, provider);
        QFETCH(QString, ip);
        QFETCH(QString, subdomain);
        QFETCH(QString, serverId);
        QFETCH(QString, url);
        Claw c;
        c.provider = provider;
        c.ipAddress = ip;
        c.subdomain = subdomain;
        c.providerServerId = serverId;
        QCOMPARE(c.gatewayUrl(), url);
    }

    void readsTheServerRecord() {
        const QJsonObject obj{{"id", "c1"}, {"status", "stopping"}, {"ip", "203.0.113.9"},
                              {"gatewayToken", "gt"}, {"cpu", 2}, {"memory", 4}};
        const Claw c = Claw::fromJson(obj);
        QCOMPARE(c.id, QString("c1"));
        QCOMPARE(c.name, QString("Agent"));          // default when the record has none
        QCOMPARE(c.provider, QString("hetzner"));
        QVERIFY(c.isTransitioning());
        QCOMPARE(c.gatewayToken, QString("gt"));
        QCOMPARE(c.cpu, 2);
        QVERIFY(c.hasGateway());
    }
};

QTEST_GUILESS_MAIN(TstClaw)
#include "tst_claw.moc"
