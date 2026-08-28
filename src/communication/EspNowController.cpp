#include "EspNowController.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

EspNowController espNow;

portMUX_TYPE     EspNowController::mux_                    = portMUX_INITIALIZER_UNLOCKED;

volatile bool    EspNowController::triggerPending_        = false;
volatile int16_t EspNowController::pendingAnimationIndex_  = 0;
volatile bool    EspNowController::stopPending_            = false;

EspNowController::NudgeEvent EspNowController::nudgeQueue_[EspNowController::kNudgeQueueSize] = {};
volatile int EspNowController::nudgeQueueHead_  = 0;
volatile int EspNowController::nudgeQueueTail_  = 0;
volatile int EspNowController::nudgeQueueCount_ = 0;

volatile int16_t EspNowController::headRx_                 = 0;
volatile int16_t EspNowController::headRy_                 = 0;
volatile uint8_t EspNowController::headFlags_               = 0;
volatile unsigned long EspNowController::headLastRecvMs_   = 0;

volatile bool    EspNowController::peerEverRecv_           = false;
volatile unsigned long EspNowController::lastPeerRecv_     = 0;
uint32_t         EspNowController::txSeq_                  = 0;

// ---- ESP-NOW callbacks (run in the WiFi task) ----

void EspNowController::onDataRecv(const esp_now_recv_info_t* info,
                                   const uint8_t* data, int len) {
    if (!info || !info->src_addr) return;
    if (len != sizeof(EventPacket)) {
        Serial.printf("ESP-NOW: recv %d bytes from unexpected source, ignored\n", len);
        return;
    }
    if (memcmp(info->src_addr, RECEIVER_BOARD_MAC, 6) != 0) {
        Serial.println("ESP-NOW: recv from unknown MAC, ignored");
        return;
    }

    peerEverRecv_ = true;
    lastPeerRecv_ = millis();

    EventPacket packet;
    memcpy(&packet, data, sizeof(packet));
    if (packet.magic != EVENTNOW_MAGIC) {
        Serial.println("ESP-NOW: recv bad magic, ignored");
        return;
    }

    if (packet.msgType == EventMsgType::Trigger) {
        Serial.printf("[%lu] ESP-NOW: recv Trigger(%d) seq=%lu (WiFi task callback)\n", millis(), packet.arg1, (unsigned long)packet.seq);
        portENTER_CRITICAL(&mux_);
        pendingAnimationIndex_ = packet.arg1;
        triggerPending_ = true;
        portEXIT_CRITICAL(&mux_);
    } else if (packet.msgType == EventMsgType::Stop) {
        Serial.printf("ESP-NOW: recv Stop seq=%lu\n", (unsigned long)packet.seq);
        stopPending_ = true;
    } else if (packet.msgType == EventMsgType::Nudge || packet.msgType == EventMsgType::NudgeRelease) {
        bool pressed = packet.msgType == EventMsgType::Nudge;
        Serial.printf("[%lu] ESP-NOW: recv Nudge(%d) %s seq=%lu\n", millis(), packet.arg1, pressed ? "press" : "release", (unsigned long)packet.seq);
        portENTER_CRITICAL(&mux_);
        if (nudgeQueueCount_ < kNudgeQueueSize) {
            nudgeQueue_[nudgeQueueTail_] = {packet.arg1, pressed};
            nudgeQueueTail_ = (nudgeQueueTail_ + 1) % kNudgeQueueSize;
            nudgeQueueCount_++;
        } else {
            Serial.println("ESP-NOW: nudge queue full, dropping");
        }
        portEXIT_CRITICAL(&mux_);
    } else if (packet.msgType == EventMsgType::Head) {
        portENTER_CRITICAL(&mux_);
        headRx_ = packet.arg1;
        headRy_ = packet.arg2;
        headFlags_ = packet.flags;
        headLastRecvMs_ = millis();
        portEXIT_CRITICAL(&mux_);
    } else {
        Serial.printf("ESP-NOW: recv Heartbeat seq=%lu\n", (unsigned long)packet.seq);
    }
}

void EspNowController::onDataSent(const esp_now_send_info_t* tx_info,
                                   esp_now_send_status_t status) {
    Serial.printf("ESP-NOW: send callback status=%s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// ---- Public API ----

void EspNowController::setup() {
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW: init FAILED");
        return;
    }

    esp_now_set_pmk(PMK_KEY);

    esp_now_peer_info_t peer = {};
    memcpy(peer.lmk, LMK_KEY, 16);
    memcpy(peer.peer_addr, RECEIVER_BOARD_MAC, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = true;
    peer.ifidx   = WIFI_IF_STA;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("ESP-NOW: failed to add receiver peer");
    }

    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);

    txSeq_ = esp_random();

    Serial.printf("ESP-NOW: ready  MAC=%s  channel=%d  peer=%02X:%02X:%02X:%02X:%02X:%02X\n",
                  WiFi.macAddress().c_str(), ESPNOW_CHANNEL,
                  RECEIVER_BOARD_MAC[0], RECEIVER_BOARD_MAC[1], RECEIVER_BOARD_MAC[2],
                  RECEIVER_BOARD_MAC[3], RECEIVER_BOARD_MAC[4], RECEIVER_BOARD_MAC[5]);
}

void EspNowController::loop() {
    if (millis() - lastHeartbeatSent_ >= HEARTBEAT_INTERVAL_MS) {
        sendHeartbeat();
        lastHeartbeatSent_ = millis();
    }
}

void EspNowController::sendHeartbeat() {
    EventPacket packet;
    packet.msgType = EventMsgType::Heartbeat;
    packet.seq = ++txSeq_;
    esp_now_send(RECEIVER_BOARD_MAC, (const uint8_t*)&packet, sizeof(packet));
}

bool EspNowController::isReceiverConnected() const {
    return peerEverRecv_ && (millis() - lastPeerRecv_ < RECEIVER_LINK_TIMEOUT_MS);
}

bool EspNowController::consumePendingTrigger(int16_t &outAnimationIndex) {
    if (!triggerPending_) return false;
    portENTER_CRITICAL(&mux_);
    outAnimationIndex = pendingAnimationIndex_;
    triggerPending_ = false;
    portEXIT_CRITICAL(&mux_);
    return true;
}

bool EspNowController::consumePendingStop() {
    if (!stopPending_) return false;
    stopPending_ = false;
    return true;
}

bool EspNowController::consumePendingNudge(NudgeButton &outButton, bool &outPressed) {
    bool got = false;
    portENTER_CRITICAL(&mux_);
    if (nudgeQueueCount_ > 0) {
        NudgeEvent ev = nudgeQueue_[nudgeQueueHead_];
        nudgeQueueHead_ = (nudgeQueueHead_ + 1) % kNudgeQueueSize;
        nudgeQueueCount_--;
        outButton = (NudgeButton)ev.button;
        outPressed = ev.pressed;
        got = true;
    }
    portEXIT_CRITICAL(&mux_);
    return got;
}

void EspNowController::getHeadRates(int8_t &outRx, int8_t &outRy, bool &outL1Held, unsigned long &outLastRecvMs) const {
    int16_t rx, ry;
    uint8_t flags;
    portENTER_CRITICAL(&mux_);
    rx = headRx_;
    ry = headRy_;
    flags = headFlags_;
    outLastRecvMs = headLastRecvMs_;
    portEXIT_CRITICAL(&mux_);

    // Clamp before narrowing -- an out-of-spec value would otherwise wrap
    // sign on the int16_t->int8_t cast instead of saturating.
    outRx = (int8_t)constrain(rx, -100, 100);
    outRy = (int8_t)constrain(ry, -100, 100);
    outL1Held = (flags & HEAD_FLAG_L1_HELD) != 0;
}
