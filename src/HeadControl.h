#pragma once
#include <stdint.h>

// Drives the head (horizontal_movement + both head-pivot servos) from the
// right stick's continuous rate, not a fixed target: how far the stick is
// pushed sets how FAST the head moves. Position is integrated in software
// and pushed to each servo as a fresh instant curve.
//
// Call update() every loop from Callbacks::onEarlyLoop() -- it polls the
// latest rates itself, advances the integration, and enforces the
// no-update safety timeout.
namespace HeadControl {
    void update();
    bool isActive();

    // Freezes both axes in place. For safety stops (e.g. ESP-NOW link loss).
    void stopAll();
}
