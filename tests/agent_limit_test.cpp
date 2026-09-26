// SPDX-License-Identifier: MIT
//
// The app opens allianceinterstellar.com only for the server's one-agent refusal. Every other
// failure of POST /claws has to stay on the ordinary error path, where the user reads what the
// server said. The messages below are the ones the service actually sends
// (ClawHostAPI apps/api/src/services/clawEntitlement.ts, controllers/claws/createClaw.ts).
#include "services/AgentLimit.h"

#include <cstdio>

// The refusal, word for word as the service sends it.
#define REFUSAL "ClawHost Pro is required to create a second server."

namespace {

struct Case {
    int status;
    const char *message;
    bool expected;
    const char *what;
};

const Case kCases[] = {
    // The refusal itself.
    {402, REFUSAL, true, "402 one-agent refusal"},
    {402, "REQUIRED TO CREATE A SECOND SERVER", true, "402 one-agent refusal, different case"},

    // A 402 that is not the one-agent refusal.
    {402, "cloud memory is a paid feature", false, "402 with another message"},
    {402, "", false, "402 with no message"},

    // The same words under any other status.
    {500, "Failed to create claw: " REFUSAL, false, "500 quoting the refusal"},
    {403, REFUSAL, false, "403 with the refusal's message"},

    // Every other refusal the create endpoint answers with.
    {503, "Could not verify the ClawHost Pro subscription. Please retry.", false,
     "503 verification unavailable"},
    {429, "Server limit reached for this account.", false, "429 account ceiling"},
    {409, "Another server deployment is already in progress for this account.", false,
     "409 deployment in progress"},
    {403, "Verify an email address or phone number before creating a server.", false,
     "403 identity required"},
    {400, "Cloud provider account limit reached. Please contact support or try a different provider.",
     false, "400 provider limit"},
    {401, "Session expired — please sign in again.", false, "401 session expired"},
    {0, "Host awsapi.example.invalid not found", false, "no response at all"},
};

}  // namespace

int main() {
    int failures = 0;
    for (const Case &c : kCases) {
        const bool got =
            AgentLimit::isOneAgentLimitRefusal(c.status, QString::fromUtf8(c.message));
        if (got != c.expected) {
            ++failures;
            std::printf("FAIL  %s: expected %s, got %s\n", c.what,
                        c.expected ? "true" : "false", got ? "true" : "false");
        } else {
            std::printf("ok    %s\n", c.what);
        }
    }

    // The page the app opens must be the one on allianceinterstellar.com.
    const QString url = AgentLimit::continueUrl();
    if (url != QLatin1String("https://allianceinterstellar.com/pricing#agentaura-plans")) {
        ++failures;
        std::printf("FAIL  continue URL is %s\n", url.toUtf8().constData());
    } else {
        std::printf("ok    continue URL\n");
    }

    const int total = int(sizeof(kCases) / sizeof(kCases[0])) + 1;
    std::printf("%d/%d passed\n", total - failures, total);
    return failures == 0 ? 0 : 1;
}
