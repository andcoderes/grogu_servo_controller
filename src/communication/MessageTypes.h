#pragma once
#include <Arduino.h>

// Must stay byte-for-byte identical to receiver's copy of this file.
#define EVENTNOW_MAGIC 0xE7

enum class EventMsgType : uint8_t {
    Heartbeat    = 0x10,   // keepalive, no arg
    Trigger      = 0x20,   // arg1 = animation index to play (see GeneratedCodeAnimations.h)
    Stop         = 0x30,   // no arg -- stop whatever animation is currently playing
    Nudge        = 0x40,   // arg1 = NudgeButton -- start moving that joint
    NudgeRelease = 0x41,   // arg1 = NudgeButton -- stop moving that joint
    Head         = 0x50,   // arg1 = horizontal rate, arg2 = tilt rate, both -100..100, flags bit0 = L1 held (see HeadControl.h)
};

// Head packet flag bits (EventPacket.flags).
#define HEAD_FLAG_L1_HELD 0x01

// Gamepad buttons that each drive a single joint continuously while held,
// instead of playing an animation. See ArmNudge.cpp for the button->joint
// mapping. Multiple buttons can be held/active at once -- each is tracked
// independently, so both arms can move at the same time.
enum class NudgeButton : int16_t {
    Y         = 0,  // right shoulder, up
    A         = 1,  // right shoulder, down
    X         = 2,  // right elbow, in
    B         = 3,  // right elbow, out
    DpadUp    = 4,  // left shoulder, up
    DpadDown  = 5,  // left shoulder, down
    DpadLeft  = 6,  // left elbow, in
    DpadRight = 7,  // left elbow, out
};

struct __attribute__((packed)) EventPacket {
    uint8_t      magic   = EVENTNOW_MAGIC;
    EventMsgType msgType = EventMsgType::Heartbeat;
    uint32_t     seq     = 0;   // monotonic per-sender counter, replay guard
    int16_t      arg1    = 0;   // Trigger: animation index. Head: horizontal rate. Heartbeat: unused.
    int16_t      arg2    = 0;   // Head: tilt rate. Unused by every other message type.
    uint8_t      flags   = 0;   // Head: HEAD_FLAG_* bits. Unused by every other message type.
};

// Documentation only -- real bounds check uses getAnimationCount() at
// runtime. Current index key (see GeneratedCodeAnimations.cpp): 0=idle,
// 1=No, 2=yes, 3=the force, 4=grab me, 5=Food.
#define MOTOR_CONTROLLER_EVENT_COUNT 6
