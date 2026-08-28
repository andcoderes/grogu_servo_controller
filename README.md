# Grogu motor_controller (Bottango Impulse)

Firmware for grogu's servo/animatronic board -- a [Bottango
Impulse](https://www.bottango.com/pages/bottango-controls-lineup) (ESP32,
10 servo headers). It vendors Bottango's own open-source Arduino driver
(BSD-3-Clause, see `LICENSE`) under `src/src/` -- named literally `src`
(not `src/bottango/`) because that's the include path Bottango Studio's
own code exports hardcode (`#include "src/CommandStream.h"` etc.); see
"Updating animations from Bottango Studio" below.

> New to the project? [project_overview.md](project_overview.md) explains
> how this repo, `grogu_mcu_receiver`, and `Droid_Phone_Controller` fit
> together and the order to bring them up.

**Two independent things, on two independent transports:**
- **ESP-NOW** (always on) carries grogu's own custom trigger link from
  `receiver` — a macro-button press in the DroidController phone app
  forwards an animation index for this board to play. This has nothing to
  do with Bottango's own protocol.
- **USB** (when a PC is plugged in) is regular Bottango Desktop live
  control — author animations, puppeteer servos in real time, exactly like
  a stock Impulse board. Toggle between "Live" and "Export" mode from
  Bottango Desktop's own UI once connected; it persists the choice to NVS
  and reboots to apply it (`ENABLE_DYNAMIC_ANIMATION_SOURCE_SWITCH` in
  `src/src/BoardDefs.h` — **without this, USB live control silently
  does nothing**: the stock Impulse-named-board config only enables
  `USE_CODE_COMMAND_STREAM`, which otherwise forces the board permanently
  offline and Bottango's own command filter rejects every live-control
  command except the handshake).

**ESP-NOW triggers only take effect while the board is in Export mode** —
`BottangoCore::commandStreamProvider` is only non-null then; while Desktop
has live control over USB, a trigger arriving over ESP-NOW is logged and
safely ignored rather than fighting the live connection (see
`Callbacks::onEarlyLoop()` in `BottangoArduinoCallbacks.cpp`). **A fresh
flash boots in Live mode by default** — the persisted preference defaults
to `false` (`getUseExportedCommandStream()`) until you switch it from
Bottango Desktop at least once.

This is *not* Bottango's own bridge/peer relay mesh (Nova <-> Impulse over
ESP-NOW) -- that's disabled here (see `src/src/BoardDefs.h`) because
grogu's `receiver` isn't a Bottango board and speaks a different, much
simpler protocol. It's also not a fork of Bottango's firmware in the sense
of changing its behavior -- it's the stock driver plus the one customization
point Bottango explicitly documents for this (`Callbacks::onEarlyLoop()` in
`src/BottangoArduinoCallbacks.cpp`), calling their public
`BottangoCore::commandStreamProvider->startCommandStream()` API the same way
their own commented-out example does.

## How the trigger link works

1. Phone app -> BLE -> `receiver`
2. `receiver`'s `CommandParser` reads `m[0]`. `id == 999` -> stop; anything
   else -> `EspNowController::sendEvent(id)`, an encrypted `EventPacket`
   (`Trigger`, `arg1 = id`) to `motor_controller`.
3. `motor_controller` (`EspNowController::onDataRecv`, WiFi task) hands it
   to the main loop; `Callbacks::onEarlyLoop()` calls
   `commandStreamProvider->startCommandStream(id, false)` (Trigger) or
   `->stop()` (Stop) -- only while the board is in Export mode (see above).
4. `motor_controller` sends a `Heartbeat` back every second, so `receiver`'s
   `EspNowController::isPeerConnected()` reflects real link status.

**The event id *is* the animation index** (999 reserved for Stop) --
whatever index Bottango Studio assigns on export is what the app must
send. Re-exporting in a different order changes what each button plays.

## Updating animations from Bottango Studio

`src/GeneratedCodeAnimations.h`/`.cpp` are a real **Code Command Stream**
export from Bottango Studio (Export > Code, in the desktop app), not
hand-written. Current export has 7 animations:

| Index | Name           |
|-------|----------------|
| 0     | idle annimation |
| 1     | No             |
| 2     | yes            |
| 3     | the force      |
| 4     | grab me        |
| 5     | eating         |
| 6     | `6 7` (placeholder — unnamed in Studio) |

**The event id the phone app sends *is* this index directly** — button "No"
= send `1`. Wired up in `Droid_Phone_Controller`'s
`grogu/src/main/res/raw/audio_grogu.json` (one entry per row above, plus a
reserved `"999"` = STOP). Re-check this
table (also printed as a comment at the top of the real
`GeneratedCodeAnimations.cpp`) any time you re-export, since
re-ordering/adding/removing animations in Studio changes what each index
means, and update `MOTOR_CONTROLLER_EVENT_COUNT` in both projects'
`MessageTypes.h` plus `audio_grogu.json`'s ids to match.

**To update: export from Studio, then drag both generated files
(`GeneratedCodeAnimations.h` and `.cpp`) into `src/`, overwriting what's
there.** Studio's own generated code always hardcodes `#include
"src/CommandStream.h"` etc. — that's *why* the vendored driver lives at
`src/src/` and not somewhere better-named: so a drag-and-drop export always
resolves correctly with zero hand-editing, every time. Don't rename that
folder.

Don't hand-edit the trigger-source fields (`playOnPin` etc.) in an exported
config to wire up GPIO buttons -- this project deliberately leaves them at 0
and drives playback from ESP-NOW instead (see `onEarlyLoop()`). Also don't
hand-edit `GeneratedCodeAnimations.h`/`.cpp` themselves -- they're marked
"GENERATED CODE, do not change" for a reason; the next Studio export
overwrites them anyway.

## Project structure

```
src/
  main.cpp                        Entry point: WiFi/ESP-NOW init, then hands off to BottangoCore
  config.h                        Timing constants (secrets come from secrets.h)
  BottangoArduinoConfig.h         Vendored -- Bottango's own tunables (timeouts, buffer sizes, etc.)
  BottangoArduinoModules.h        Vendored -- module toggle table (overridden by BoardDefs.h for named boards)
  BottangoArduinoCallbacks.h/.cpp Vendored + grogu's ESP-NOW trigger hook in onEarlyLoop()
  GeneratedCodeAnimations.h/.cpp  Real Bottango Studio export -- see "Updating animations" above; overwrite on re-export
  HowToUseThisCode.txt            Bottango Studio's own export instructions (informational, not used by the build)
  communication/
    MessageTypes.h                 EventPacket wire format (must match receiver's copy byte-for-byte)
    EspNowController.h/.cpp        ESP-NOW link to receiver: decrypts Trigger events, sends Heartbeats
  src/                              Vendored Bottango Arduino driver (BSD-3-Clause) -- see LICENSE. Named "src"
                                     (not "bottango") to match what Studio's own exports hardcode -- don't rename.
    BoardDefs.h                    Customized: BOTTANGO_IMPULSE + USE_CODE_COMMAND_STREAM +
                                     ENABLE_DYNAMIC_ANIMATION_SOURCE_SWITCH, relay mesh disabled
    [...]                          Everything else copied as-is from Bottango's official repo
scripts/
  load_secrets.py                  Pre-build script: .env -> include/secrets.h
grogu.btngo                        Bottango Studio project (servo rig + animations); models\*.obj paths are relative -- see bottango_setup.md
.env.example                       Template for ESP-NOW keys + receiver's MAC -- copy to .env
platformio.ini
LICENSE, ThirdPartyLicenses/       From the vendored Bottango driver (BSD-3-Clause)
```

## Prerequisites

- [Visual Studio Code](https://code.visualstudio.com/) with the [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension
- Python 3 (used by the `load_secrets.py` build script)

## Setup

1. Set up `.env` -- see [Generating ESP-NOW keys](#generating-esp-now-keys) below. `PMK_KEY`/`LMK_KEY` must be byte-for-byte identical to `receiver/.env`'s copies (already pre-filled matching in both, if you're using the values this project shipped with).

2. Build the firmware:
   ```bash
   pio run -e bottango_impulse
   ```

3. Upload to the board:
   ```bash
   pio run -e bottango_impulse -t upload
   ```

4. Open the serial monitor and note the printed MAC address:
   ```bash
   pio device monitor -b 115200
   ```
   Look for: `ESP-NOW: ready  MAC=XX:XX:XX:XX:XX:XX`

5. Put that MAC into `receiver/.env`'s `EVENT_BOARD_MAC`, rebuild/reflash
   `receiver`. Then do the reverse: read `receiver`'s printed MAC and put it
   into this project's `.env`'s `RECEIVER_BOARD_MAC`, rebuild/reflash this
   board. (Both boards need the other's MAC to talk — see receiver's own
   README for its side of this.)

6. To connect Bottango Desktop over USB for live control/authoring
   instead, just plug in and open the desktop app -- a fresh flash boots
   in Live mode by default (see above). To play triggered animations with
   no PC connected, switch the board to Export mode from Desktop's UI
   once, and re-export/drop in `GeneratedCodeAnimations.h`/`.cpp` whenever
   animations change (see "Updating animations" above). To author against
   the 3D body model in Bottango Studio, open `grogu.btngo` -- see
   [bottango_setup.md](bottango_setup.md) for converting the Project Gogurt
   STL parts to OBJ and where to put them.

## Generating ESP-NOW keys

Copy the template and fill in real values:

```bash
cp .env.example .env
```

**`PMK_KEY` / `LMK_KEY`** — two 16-byte (128-bit) keys, each as 32 hex
characters, generated together with `receiver/.env`'s copies:

```bash
openssl rand -hex 16   # run twice — once for PMK_KEY, once for LMK_KEY
```

These must end up byte-for-byte identical in both `.env` files.

**`RECEIVER_BOARD_MAC`** — the `receiver` board's WiFi MAC address (see
step 5 above).

`scripts/load_secrets.py` runs automatically before every build (see
`platformio.ini`) and turns `.env` into `include/secrets.h`, which
`config.h` includes. Neither `.env` nor the generated `secrets.h` are
committed to git.


## License

Vendored Bottango driver code under `src/src/`, `LICENSE`, and
`ThirdPartyLicenses/` is BSD-3-Clause, Copyright (c) 2025 Bottango LLC.
`GeneratedCodeAnimations.h`/`.cpp` are generated by Bottango Studio from
your own project. All other code (grogu's ESP-NOW link, `main.cpp`,
`BoardDefs.h`/`BottangoArduinoCallbacks.cpp` customizations): all rights
reserved.
