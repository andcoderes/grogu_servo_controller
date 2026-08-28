#include "ManualMode.h"
#include "src/BottangoCore.h"
#include "src/ExportedAnimationPlaybackControl.h"
#include "NeutralPose.h"

namespace ManualMode {

static int activeCount_ = 0;

void enter() {
    if (activeCount_ == 0) {
        BottangoCore::commandStreamProvider->stop();  // clear whatever's playing/queued first (all servos, not just this one)
        NeutralPose::freezeAll();  // resync every servo's targetSignal before any manual curve is added
        ExportedAnimationPlaybackControl::idleAnimIndex = -1;  // manual control until a real Trigger/Stop resumes it (see NeutralPose)
    }
    activeCount_++;
}

void exit() {
    if (activeCount_ > 0) activeCount_--;
    ExportedAnimationPlaybackControl::idleAnimIndex = -1;  // stays disabled even once the count hits 0 -- see header
}

}  // namespace ManualMode
