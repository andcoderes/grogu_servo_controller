// Based on Bottango's official BottangoArduinoDriver.ino. Driver vendored
// under src/src/ (see README) with a custom BoardDefs.h; this file stands
// in for the stock .ino.

#include <Arduino.h>
#include <WiFi.h>

#include "src/BottangoCore.h"
#include "src/BasicCommands.h"
#include "src/PersistentConfigUtil.h"
#include "src/BoardDefs.h"
#include "communication/EspNowController.h"

void setup()
{
    // Bottango's own Serial.begin() happens partway through
    // bottangoSetup() below -- start it here first so nothing logged
    // before that point (WiFi/ESP-NOW setup) is silently lost.
    Serial.begin(115200);
    delay(100);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    espNow.setup();

    // Defensive: a soft reboot (e.g. Bottango Desktop's Live/Export
    // toggle) doesn't always clear LEDC's pin-attachment bookkeeping,
    // which makes every servo's rSVPin registration fail with "already
    // attached to LEDC". Detach all of Impulse's servo pins first so
    // Bottango's setup stream can (re)attach them cleanly.
    for (int i = 0; i < PIN_REMAP_LENGTH; i++) {
        ledcDetach(onboardPins[i]);
    }

    BottangoCore::bottangoSetup();

    Serial.printf("Bottango mode: %s\n",
                  PersistentConfigUtil::getUseExportedCommandStream() ? "Export (offline playback)" : "Live (USB/Desktop control)");

    Serial.println("Grogu motor_controller (Bottango Impulse) ready");
}

void loop()
{
    espNow.loop();
    BottangoCore::bottangoLoop();
}
