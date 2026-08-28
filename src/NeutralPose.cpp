#include "NeutralPose.h"
#include <Arduino.h>
#include "src/BottangoCore.h"
#include "src/EffectorPool.h"
#include "src/FixedBezierCurve.h"
#include "src/FloatBezierCurve.h"
#include "src/Time.h"
#include "src/ExportedAnimationPlaybackControl.h"

namespace NeutralPose {

struct ServoNeutral {
    const char* identifier;
    int minPWM;
    int maxPWM;
    int neutralPWM;  // used directly as this servo's target PWM, no scaling
    bool isArm;       // only ramped to neutral when shouldMoveArms() is true
};

// Pin bounds must match GeneratedCodeAnimations.cpp's SETUP_DATA_0
// (rSVPin min/max) -- re-check both if servos are re-registered with
// different bounds in a future Studio export.
static const ServoNeutral kAllServos[] = {
    {"5", 1400, 2200, 2055, true},   // left elbow
    {"6", 900,  1700, 900,  true},   // right elbow
    {"1", 850,  2000, 1310, false},  // right head pivot
    {"2", 1000, 2000, 1600, false},  // left head pivot
    {"7", 1000, 2000, 1432, false},  // horizontal movement
    {"3", 700,  2200, 2200, true},   // left shoulder
    {"4", 1000, 2400, 1000, true},   // right shoulder
};
constexpr int kServoCount = sizeof(kAllServos) / sizeof(kAllServos[0]);
constexpr unsigned long kRampMs = 3000;

// Animation 0 is GeneratedCodeAnimations.cpp's configured idle animation
// (idleAnim=1). Must match if a future Studio re-export changes which
// index is idle.
constexpr int kRealIdleAnimIndex = 0;

// idle/No/yes don't move the arms, so only these targets ramp them.
constexpr int kAnimTheForce = 3;
constexpr int kAnimGrabMe   = 4;
constexpr int kAnimFood     = 5;

static bool shouldMoveArms(int16_t targetAnimation) {
    return targetAnimation == kAnimTheForce || targetAnimation == kAnimGrabMe || targetAnimation == kAnimFood;
}

enum class State { Idle, Moving };
static State state_ = State::Idle;
static int16_t targetAnimation_ = -1;
static unsigned long phaseStartMs_ = 0;
static bool includeArms_ = false;  // whether the in-flight ramp includes the arms

static int toCompressed(int pwm, int minPWM, int maxPWM) {
    long c = (long)(pwm - minPWM) * COMPRESSED_SIGNAL_MAX_INT / (maxPWM - minPWM);
    if (c < 0) c = 0;
    if (c > COMPRESSED_SIGNAL_MAX_INT) c = COMPRESSED_SIGNAL_MAX_INT;
    return (int)c;
}

void freezeAll() {
    for (int i = 0; i < kServoCount; i++) {
        const ServoNeutral& n = kAllServos[i];
        char id[9];
        strncpy(id, n.identifier, sizeof(id) - 1);
        id[sizeof(id) - 1] = '\0';

        AbstractEffector* effector = BottangoCore::effectorPool.getEffector(id);
        if (!effector) continue;
        int y = toCompressed(effector->getCurrentSignal(), n.minPWM, n.maxPWM);

        if (BottangoCore::effectorPool.effectorUsesFloatCurve(id)) {
            BottangoCore::effectorPool.addCurveToEffector(
                id, new FloatBezierCurve(Time::getCurrentTimeInMs(), 0, y, 0, 0, y, 0, 0));
        } else {
            BottangoCore::effectorPool.addCurveToEffector(
                id, new FixedBezierCurve(Time::getCurrentTimeInMs(), 0, y, 0, 0, y, 0, 0));
        }
    }
}

// Ramps to neutral over kRampMs. Skips arm servos if includeArms is
// false; armsOnly ramps only the arms, leaving other servos untouched
// (mid-ramp catch-up).
static void rampAll(bool fromMidpoint, bool includeArms, bool armsOnly = false) {
    for (int i = 0; i < kServoCount; i++) {
        const ServoNeutral& n = kAllServos[i];
        if (armsOnly ? !n.isArm : (n.isArm && !includeArms)) continue;

        char id[9];
        strncpy(id, n.identifier, sizeof(id) - 1);
        id[sizeof(id) - 1] = '\0';

        AbstractEffector* effector = BottangoCore::effectorPool.getEffector(id);
        if (!effector) continue;  // matches freezeAll()/ArmNudge's guard -- avoids leaking a Curve* below

        int targetPWM = n.neutralPWM;
        int startPWM = fromMidpoint ? (n.minPWM + n.maxPWM) / 2 : effector->getCurrentSignal();

        int startY = toCompressed(startPWM, n.minPWM, n.maxPWM);
        int endY   = toCompressed(targetPWM, n.minPWM, n.maxPWM);

        // 1/3-2/3 control points -> linear interpolation. Curve args are
        // relative offsets, not absolute Ys (see FixedBezierCurve ctor).
        long third = (long)kRampMs / 3;
        int startControlY = (endY - startY) / 3;
        int endControlY   = (startY - endY) / 3;

        if (BottangoCore::effectorPool.effectorUsesFloatCurve(id)) {
            BottangoCore::effectorPool.addCurveToEffector(
                id, new FloatBezierCurve(Time::getCurrentTimeInMs(), kRampMs,
                                          startY, third, startControlY,
                                          endY, -third, endControlY));
        } else {
            BottangoCore::effectorPool.addCurveToEffector(
                id, new FixedBezierCurve(Time::getCurrentTimeInMs(), kRampMs,
                                          startY, third, startControlY,
                                          endY, -third, endControlY));
        }
    }
}

void start(bool fromMidpoint, int16_t targetAnimation) {
    Serial.printf("[%lu] NeutralPose::start() called, state=%d\n", millis(), (int)state_);
    targetAnimation_ = targetAnimation;  // always update -- a fresh trigger wins even mid-ramp
    if (state_ != State::Idle) {
        // A fresh target needing arms arriving mid-ramp still needs them ramped.
        if (!includeArms_ && shouldMoveArms(targetAnimation)) {
            includeArms_ = true;
            rampAll(/*fromMidpoint=*/false, /*includeArms=*/true, /*armsOnly=*/true);
            phaseStartMs_ = millis();
        }
        Serial.printf("[%lu] NeutralPose: already mid-transition, only updated target to %d\n", millis(), targetAnimation);
        return;
    }

    BottangoCore::commandStreamProvider->stop();  // clear whatever's playing/queued first
    if (!fromMidpoint) freezeAll();  // resync targetSignal to reality before ramping from it

    // Direct effector curves don't count as streamIsInProgress(), so
    // Bottango's scheduler would auto-restart idle on top of the ramp
    // otherwise. Restored once startCommandStream() hands control back.
    ExportedAnimationPlaybackControl::idleAnimIndex = -1;

    // Boot always moves everything; a Trigger only moves the arms if the
    // target animation actually uses them.
    includeArms_ = fromMidpoint || shouldMoveArms(targetAnimation);
    Serial.printf("[%lu] NeutralPose: stopped + resynced, moving %s to neutral\n", millis(),
                  includeArms_ ? "all servos" : "head/horizontal only");
    rampAll(fromMidpoint, includeArms_);
    phaseStartMs_ = millis();
    state_ = State::Moving;
}

void update() {
    if (BottangoCore::commandStreamProvider == nullptr) return;  // live USB mode

    if (state_ == State::Moving && millis() - phaseStartMs_ >= kRampMs) {
        if (targetAnimation_ <= 0) {
            // <=0: boot (-1) or an explicit idle trigger (0) -- both loop idle.
            ExportedAnimationPlaybackControl::idleAnimIndex = kRealIdleAnimIndex;
            Serial.printf("[%lu] NeutralPose: neutral reached, starting idle\n", millis());
            BottangoCore::commandStreamProvider->startCommandStream(0, true);
        } else {
            // idleAnimIndex stays disabled so idle doesn't auto-resume
            // once this animation finishes -- only a new Trigger/boot does.
            Serial.printf("[%lu] NeutralPose: neutral reached, playing animation %d\n", millis(), targetAnimation_);
            BottangoCore::commandStreamProvider->startCommandStream((byte)targetAnimation_, false);
        }
        state_ = State::Idle;
    }
}

bool isInProgress() {
    return state_ != State::Idle;
}

void cancel() {
    // Only undo start()'s disable if a ramp was actually in progress --
    // cancel() runs on every Stop/Nudge regardless.
    if (state_ == State::Moving) {
        ExportedAnimationPlaybackControl::idleAnimIndex = kRealIdleAnimIndex;  // don't leave it disabled if interrupted mid-ramp
    }
    state_ = State::Idle;
}

}  // namespace NeutralPose
