#include "ArmNudge.h"
#include <Arduino.h>
#include "src/BottangoCore.h"
#include "src/EffectorPool.h"
#include "src/FixedBezierCurve.h"
#include "src/FloatBezierCurve.h"
#include "src/Time.h"
#include "ManualMode.h"

namespace ArmNudge {

struct Joint {
    const char* identifier;
    int minPWM;
    int maxPWM;
};

// Pin bounds must match GeneratedCodeAnimations.cpp's SETUP_DATA_0
// (rSVPin min/max) -- re-check both if servos are re-registered with
// different bounds in a future Studio export.
static const Joint kJoints[] = {
    {"4", 1000, 2400},  // 0: right shoulder
    {"6", 900,  1700},  // 1: right elbow
    {"3", 700,  2200},  // 2: left shoulder
    {"5", 1400, 2200},  // 3: left elbow
};
constexpr int kJointCount = sizeof(kJoints) / sizeof(kJoints[0]);

struct ButtonMap {
    int jointIndex;
    int direction;  // +1 moves toward maxPWM, -1 moves toward minPWM
};

// Index = NudgeButton value (Y=0 .. DpadRight=7).
static const ButtonMap kButtonMap[] = {
    /* Y         */ {0, +1},  // right shoulder, up
    /* A         */ {0, -1},  // right shoulder, down
    /* X         */ {1, -1},  // right elbow, in
    /* B         */ {1, +1},  // right elbow, out
    /* DpadUp    */ {2, -1},  // left shoulder, up
    /* DpadDown  */ {2, +1},  // left shoulder, down
    /* DpadLeft  */ {3, +1},  // left elbow, in
    /* DpadRight */ {3, -1},  // left elbow, out
};

// Matches the original click-nudge rate (100 PWM / 200ms).
constexpr float kSpeedPWMPerSec = 500.0f;

static bool jointActive_[kJointCount] = {};

static bool anyActive() {
    for (int i = 0; i < kJointCount; i++) {
        if (jointActive_[i]) return true;
    }
    return false;
}

static int toCompressed(int pwm, int minPWM, int maxPWM) {
    long c = (long)(pwm - minPWM) * COMPRESSED_SIGNAL_MAX_INT / (maxPWM - minPWM);
    if (c < 0) c = 0;
    if (c > COMPRESSED_SIGNAL_MAX_INT) c = COMPRESSED_SIGNAL_MAX_INT;
    return (int)c;
}

static void getId(const Joint& j, char* out) {
    strncpy(out, j.identifier, 8);
    out[8] = '\0';
}

// Resyncs one joint's targetSignal to its current signal (see NeutralPose.h
// for why -- clearing curves doesn't do this on its own).
static void freezeJoint(const Joint& j) {
    char id[9];
    getId(j, id);

    AbstractEffector* effector = BottangoCore::effectorPool.getEffector(id);
    if (!effector) return;

    BottangoCore::effectorPool.clearCurvesForEffector(id);
    int y = toCompressed(effector->getCurrentSignal(), j.minPWM, j.maxPWM);

    if (BottangoCore::effectorPool.effectorUsesFloatCurve(id)) {
        BottangoCore::effectorPool.addCurveToEffector(
            id, new FloatBezierCurve(Time::getCurrentTimeInMs(), 0, y, 0, 0, y, 0, 0));
    } else {
        BottangoCore::effectorPool.addCurveToEffector(
            id, new FixedBezierCurve(Time::getCurrentTimeInMs(), 0, y, 0, 0, y, 0, 0));
    }
}

// Queues a curve to the joint's extreme; cut short by a later
// freezeJoint() call (release, or a safety stop).
static void moveJointToward(const Joint& j, int direction) {
    char id[9];
    getId(j, id);

    AbstractEffector* effector = BottangoCore::effectorPool.getEffector(id);
    if (!effector) return;

    int currentPWM = effector->getCurrentSignal();
    int targetPWM = (direction > 0) ? j.maxPWM : j.minPWM;
    unsigned long moveMs = (unsigned long)(abs(targetPWM - currentPWM) / kSpeedPWMPerSec * 1000.0f);
    if (moveMs == 0) return;  // already at the extreme

    Serial.printf("ArmNudge: %s %d -> %d over %lums\n", id, currentPWM, targetPWM, moveMs);

    BottangoCore::effectorPool.clearCurvesForEffector(id);

    int startY = toCompressed(currentPWM, j.minPWM, j.maxPWM);
    int endY   = toCompressed(targetPWM, j.minPWM, j.maxPWM);

    // 1/3-2/3 control points -> linear motion; relative, not absolute
    // (see FixedBezierCurve's constructor).
    long third = (long)moveMs / 3;
    int startControlY = (endY - startY) / 3;
    int endControlY   = (startY - endY) / 3;

    if (BottangoCore::effectorPool.effectorUsesFloatCurve(id)) {
        BottangoCore::effectorPool.addCurveToEffector(
            id, new FloatBezierCurve(Time::getCurrentTimeInMs(), moveMs,
                                      startY, third, startControlY,
                                      endY, -third, endControlY));
    } else {
        BottangoCore::effectorPool.addCurveToEffector(
            id, new FixedBezierCurve(Time::getCurrentTimeInMs(), moveMs,
                                      startY, third, startControlY,
                                      endY, -third, endControlY));
    }
}

static void releaseJoint(int jointIndex) {
    jointActive_[jointIndex] = false;
    freezeJoint(kJoints[jointIndex]);
    ManualMode::exit();
}

void apply(NudgeButton button, bool pressed) {
    if (BottangoCore::commandStreamProvider == nullptr) return;  // live USB mode

    int index = (int)button;
    if (index < 0 || index >= (int)(sizeof(kButtonMap) / sizeof(kButtonMap[0]))) return;
    const ButtonMap& bm = kButtonMap[index];
    const Joint& j = kJoints[bm.jointIndex];

    if (!pressed) {
        if (!jointActive_[bm.jointIndex]) return;  // nothing to release
        releaseJoint(bm.jointIndex);
        return;
    }

    if (jointActive_[bm.jointIndex]) {
        // Another button aliasing the same joint (e.g. Y/A) -- retarget,
        // don't double-count against ManualMode.
        moveJointToward(j, bm.direction);
        return;
    }
    jointActive_[bm.jointIndex] = true;
    ManualMode::enter();  // no-ops the shared stop()/freezeAll() if another joint (this or HeadControl's) is already active
    moveJointToward(j, bm.direction);
}

bool isActive() {
    return anyActive();
}

void stopAll() {
    for (int i = 0; i < kJointCount; i++) {
        if (jointActive_[i]) releaseJoint(i);
    }
}

}  // namespace ArmNudge
