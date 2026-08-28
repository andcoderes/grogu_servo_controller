#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include "MessageTypes.h"
#include "../config.h"

// ESP-NOW link to grogu's BLE receiver board. Custom protocol, separate
// from Bottango's own relay mesh (disabled, see BoardDefs.h).
class EspNowController {
public:
    void setup();
    void loop();

    bool isReceiverConnected() const;

    // Consumed from Callbacks::onEarlyLoop().
    bool consumePendingTrigger(int16_t &outAnimationIndex);
    bool consumePendingStop();
    bool consumePendingNudge(NudgeButton &outButton, bool &outPressed);

    // Level-based, not a one-shot flag -- HeadControl polls this every
    // tick. outLastRecvMs feeds its no-update safety timeout.
    void getHeadRates(int8_t &outRx, int8_t &outRy, bool &outL1Held, unsigned long &outLastRecvMs) const;

private:
    static void onDataRecv(const esp_now_recv_info_t* info,
                           const uint8_t* data, int len);
    static void onDataSent(const esp_now_send_info_t* tx_info, esp_now_send_status_t status);

    void sendHeartbeat();

    // Cross-core lock for every field below (WiFi task writes, main loop
    // reads) -- noInterrupts() alone only suspends the local core.
    static portMUX_TYPE mux_;

    static volatile bool     triggerPending_;
    static volatile int16_t  pendingAnimationIndex_;
    static volatile bool     stopPending_;

    // Small ring buffer so two Nudge/NudgeRelease events arriving before
    // the next onEarlyLoop() tick both survive instead of the second
    // silently overwriting the first.
    struct NudgeEvent { int16_t button; bool pressed; };
    static const int kNudgeQueueSize = 8;
    static NudgeEvent nudgeQueue_[kNudgeQueueSize];
    static volatile int nudgeQueueHead_;
    static volatile int nudgeQueueTail_;
    static volatile int nudgeQueueCount_;

    static volatile int16_t  headRx_;
    static volatile int16_t  headRy_;
    static volatile uint8_t  headFlags_;
    static volatile unsigned long headLastRecvMs_;

    static volatile bool peerEverRecv_;
    static volatile unsigned long lastPeerRecv_;
    static uint32_t      txSeq_;

    unsigned long lastHeartbeatSent_ = 0;
};

extern EspNowController espNow;
