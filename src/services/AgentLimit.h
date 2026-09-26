#pragma once
#include <QString>

/// This client runs one agent per account. The server is what enforces that — the client never
/// counts agents or decides anything itself. Asked for a second one, POST /claws answers 402
/// with the message "… required to create a second server." (ClawHostAPI,
/// services/clawEntitlement.ts, reserveClawDeployment). When that happens the app opens
/// allianceinterstellar.com, where the user can continue, instead of showing the raw refusal.
namespace AgentLimit {

/// The page the app opens when the server refuses a second agent.
inline QString continueUrl() {
    return QStringLiteral("https://allianceinterstellar.com/pricing#agentaura-plans");
}

/// What the user is told in place of the server's refusal.
inline QString message() {
    return QStringLiteral(
        "This version runs one agent. To run more, continue on allianceinterstellar.com.");
}

/// True only for the server's one-agent refusal. The status alone is not enough: the message
/// check keeps any other 402 the service may answer with on the ordinary error path, where the
/// user sees what the server said.
inline bool isOneAgentLimitRefusal(int httpStatus, const QString &serverMessage) {
    return httpStatus == 402 &&
           serverMessage.contains(QLatin1String("second server"), Qt::CaseInsensitive);
}

}  // namespace AgentLimit
