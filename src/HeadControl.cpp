#include "HeadControl.h"
#include <Arduino.h>
#include "src/BottangoCore.h"
#include "src/EffectorPool.h"
#include "src/FixedBezierCurve.h"
#include "src/FloatBezierCurve.h"
#include "src/Time.h"
#include "ManualMode.h"
#include "communication/EspNowController.h"

namespace HeadControl {

struct Servo {
    const char* identifier;
    int minPWM;
    int maxPWM;
};

// Pin bounds must match GeneratedCodeAnimations.cpp's SETUP_DATA_0
// (rSVPin min/max) -- re-check both if servos are re-registered with
// different bounds in a future Studio export.
static const Servo kHorizontal = {"7", 1000, 2000};
static const Servo kRightPivot = {"1", 850,  2000};
static const Servo kLeftPivot  = {"2", 1000, 2000};

constexpr float kHorizontalSpeedPWMPerSec = 500.0f;  // matches ArmNudge's kSpeedPWMPerSec
constexpr float kTiltSpeedPWMPerSec       = 500.0f;

// rx within this range doesn't drive horizontal_movement. Only applies
// when L1 isn't held (L1 repurposes rx for the pivots instead).
constexpr int8_t kHorizontalDeadZone = 20;

constexpr unsigned long kRateTimeoutMs    = 500;  // matches receiver's MOTOR_TIMEOUT_MS convention
constexpr unsigned long kUpdateIntervalMs = 50;   // ~20Hz -- smooth enough for a servo, avoids curve churn every loop tick
constexpr unsigned long kMaxDtMs          = 200;  // caps the integration step if the main loop ever stalls while active

static bool active_ = false;
static unsigned long lastIntegrateMs_ = 0;

static int toCompressed(int pwm, int minPWM, int maxPWM) {
    long c = (long)(pwm - minPWM) * COMPRESSED_SIGNAL_MAX_INT / (maxPWM - minPWM);
    if (c < 0) c = 0;
    if (c > COMPRESSED_SIGNAL_MAX_INT) c = COMPRESSED_SIGNAL_MAX_INT;
    return (int)c;
}

static void getId(const Servo& s, char* out) {
    strncpy(out, s.identifier, 8);
    out[8] = '\0';
}

static int getCurrentPWM(const Servo& s) {
    char id[9];
    getId(s, id);
    AbstractEffector* effector = BottangoCore::effectorPool.getEffector(id);
    return effector ? effector->getCurrentSignal() : (s.minPWM + s.maxPWM) / 2;
}

// Same zero-duration-curve trick as ArmNudge/NeutralPose's freeze helpers,
// but to an arbitrary PWM -- how position updates get delivered every tick.
static void setTargetPWM(const Servo& s, int targetPWM) {
    char id[9];
    getId(s, id);

    AbstractEffector* effector = BottangoCore::effectorPool.getEffector(id);
    if (!effector) return;

    BottangoCore::effectorPool.clearCurvesForEffector(id);
    int y = toCompressed(targetPWM, s.minPWM, s.maxPWM);

    if (BottangoCore::effectorPool.effectorUsesFloatCurve(id)) {
        BottangoCore::effectorPool.addCurveToEffector(
            id, new FloatBezierCurve(Time::getCurrentTimeInMs(), 0, y, 0, 0, y, 0, 0));
    } else {
        BottangoCore::effectorPool.addCurveToEffector(
            id, new FixedBezierCurve(Time::getCurrentTimeInMs(), 0, y, 0, 0, y, 0, 0));
    }
}

static void freezeAxes() {
    setTargetPWM(kHorizontal, getCurrentPWM(kHorizontal));
    setTargetPWM(kRightPivot, getCurrentPWM(kRightPivot));
    setTargetPWM(kLeftPivot,  getCurrentPWM(kLeftPivot));
}

void update() {
    if (BottangoCore::commandStreamProvider == nullptr) return;  // live USB mode

    int8_t rx, ry;
    bool l1Held;
    unsigned long lastRecvMs;
    espNow.getHeadRates(rx, ry, l1Held, lastRecvMs);

    unsigned long now = millis();
    bool timedOut = lastRecvMs == 0 || (now - lastRecvMs) >= kRateTimeoutMs;
    if (timedOut) { rx = 0; ry = 0; }

    bool shouldBeActive = (rx != 0 || ry != 0);

    if (!shouldBeActive) {
        if (active_) {
            freezeAxes();
            ManualMode::exit();
            active_ = false;
        }
        return;
    }

    if (!active_) {
        ManualMode::enter();
        active_ = true;
        lastIntegrateMs_ = now;  // avoid one big dt jump on the first tick after activation
        return;
    }

    if (now - lastIntegrateMs_ < kUpdateIntervalMs) return;
    unsigned long dtMs = now - lastIntegrateMs_;
    if (dtMs > kMaxDtMs) dtMs = kMaxDtMs;  // cap a stalled-loop gap so the resumed tick doesn't jump the servo
    float dtSec = dtMs / 1000.0f;
    lastIntegrateMs_ = now;

    // Top/bottom tilt (opposite pivots, from ry) always works, L1 or not.
    // L1 additionally drives both pivots TOGETHER from rx (same sign, not
    // opposite) and is the only thing that blocks horizontal_movement.
    int rightDeltaPWM = (int)((-ry / 100.0f) * kTiltSpeedPWMPerSec * dtSec);
    int leftDeltaPWM  = -rightDeltaPWM;  // opposite of the right pivot

    if (l1Held) {
        int rxDeltaPWM = (int)((rx / 100.0f) * kTiltSpeedPWMPerSec * dtSec);
        rightDeltaPWM += rxDeltaPWM;
        leftDeltaPWM  += rxDeltaPWM;  // same direction as the right pivot
    } else if (rx > kHorizontalDeadZone || rx < -kHorizontalDeadZone) {
        int horizontalPWM = getCurrentPWM(kHorizontal);
        horizontalPWM += (int)((-rx / 100.0f) * kHorizontalSpeedPWMPerSec * dtSec);
        horizontalPWM = constrain(horizontalPWM, kHorizontal.minPWM, kHorizontal.maxPWM);
        setTargetPWM(kHorizontal, horizontalPWM);
    }

    if (rightDeltaPWM != 0) {
        int rightPWM = getCurrentPWM(kRightPivot);
        int newRightPWM = constrain(rightPWM + rightDeltaPWM, kRightPivot.minPWM, kRightPivot.maxPWM);
        setTargetPWM(kRightPivot, newRightPWM);
    }

    if (leftDeltaPWM != 0) {
        int leftPWM = getCurrentPWM(kLeftPivot);
        int newLeftPWM = constrain(leftPWM + leftDeltaPWM, kLeftPivot.minPWM, kLeftPivot.maxPWM);
        setTargetPWM(kLeftPivot, newLeftPWM);
    }
}

bool isActive() {
    return active_;
}

void stopAll() {
    if (active_) {
        freezeAxes();
        ManualMode::exit();
        active_ = false;
    }
}

}  // namespace HeadControl
