#pragma once

// Shared entry/exit coordinator for direct-effector-curve manual control
// (ArmNudge, HeadControl). The global stop+resync must happen only on the
// true first-active transition across ALL of them combined -- otherwise
// one system entering manual mode would freeze joints another already has
// mid-move (e.g. moving the head while an arm nudge is in progress).
namespace ManualMode {
    // Call per joint going inactive->active. Runs the shared
    // stop()/freezeAll() only on the first such call since the last exit().
    void enter();

    // Call per joint going active->inactive. Keeps idle disabled regardless
    // of the remaining count -- manual control persists until a real
    // Trigger/Stop resumes it.
    void exit();
}
