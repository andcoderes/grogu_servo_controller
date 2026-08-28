#pragma once
#include <stdint.h>

// Ramps servos to neutral together, then starts idle or plays a specific
// animation. Used at boot (ramps everything) and before playing a
// Trigger-ed animation (ramps the head pivots/horizontal always, plus the
// arms only if the animation actually moves them -- see shouldMoveArms()).
//
// start() while a ramp is already in progress just updates the target
// animation, not the ramp itself -- a fresh trigger always wins.
// Call update() every loop from Callbacks::onEarlyLoop().
namespace NeutralPose {
    // fromMidpoint: true only at boot, where no real position is known
    // yet -- uses each servo's own midpoint as a stand-in start.
    // targetAnimation: -1 starts idle once neutral is reached; otherwise
    // plays that animation index once.
    void start(bool fromMidpoint, int16_t targetAnimation);
    void update();
    bool isInProgress();

    // Aborts an in-progress ramp without playing its target -- caller
    // must still actually halt the servos (e.g. commandStreamProvider->stop()).
    void cancel();

    // Resyncs every servo's targetSignal to its current signal via a
    // zero-duration curve. Call after commandStreamProvider->stop() --
    // clearAllCurves() doesn't touch targetSignal, so without this it
    // drifts toward whatever the interrupted curve last computed.
    void freezeAll();
}
