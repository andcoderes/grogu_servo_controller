#pragma once
#include "communication/MessageTypes.h"

// Moves one joint continuously toward its extreme while its button is
// held, freezing it on release. The 4 joints (shoulders/elbows) are
// tracked independently, so multiple buttons can be held at once (e.g.
// one per arm). Direct manual control, separate from NeutralPose/Trigger/Stop.
namespace ArmNudge {
    void apply(NudgeButton button, bool pressed);

    // True if any joint is currently being driven by a held button.
    bool isActive();

    // Freezes every currently-active joint in place. For safety stops
    // (e.g. ESP-NOW link loss) where there's no single button to release.
    void stopAll();
}
