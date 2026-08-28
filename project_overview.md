# Grogu — project overview & initialization

Grogu is a droid built from **three repositories**, one per piece of the
system:

| Repo | Role | Hardware |
|------|------|----------|
| **grogu_servo_controller** (this repo) | Bottango firmware for the servo/animatronic board — head, arms, authored animations | [Bottango Impulse](https://www.bottango.com/pages/bottango-controls-lineup) (ESP32, 10 servo headers) |
| [**grogu_mcu_receiver**](https://github.com/andcoderes/grogu_mcu_receiver) | BLE receiver + drive wheels; bridges phone commands to this board over ESP-NOW | Seeed XIAO ESP32-C6 |
| [**Droid_Phone_Controller**](https://github.com/andcoderes/Droid_Phone_Controller) | Android app — macro buttons that trigger animations and sounds | Phone |

## How the pieces talk

```
Droid_Phone_Controller  --BLE-->  grogu_mcu_receiver  --ESP-NOW-->  grogu_servo_controller
      (macro button)                 (drive + bridge)                (play animation index)
```

A macro-button press in the app sends an animation index over BLE to the
receiver; the receiver forwards it as an encrypted ESP-NOW event to this
board, which plays the matching Bottango animation. This board sends a
heartbeat back once a second so the receiver knows the link is alive. See
[README.md](README.md) ("How the trigger link works") for the full
protocol.

## Initialization order

Bring the system up in this order — each step produces a value the next
one needs:

1. **Clone all three repos** (they're developed together but built
   separately).

2. **Flash `grogu_mcu_receiver`.** Follow its own README. Open its serial
   monitor and note the printed WiFi MAC address.

3. **Flash this repo (`grogu_servo_controller`).** See [README.md](README.md)
   → "Setup":
   - `cp .env.example .env`, generate the ESP-NOW `PMK_KEY` / `LMK_KEY`
     (must be byte-for-byte identical to the receiver's `.env`).
   - Put the receiver's MAC (from step 2) into `.env` as
     `RECEIVER_BOARD_MAC`.
   - `pio run -e bottango_impulse -t upload`, then open the serial monitor
     and note *this* board's printed MAC.

4. **Exchange MACs.** Put this board's MAC into the receiver's `.env`,
   rebuild/reflash the receiver. Both boards now have each other's MAC and
   the ESP-NOW link comes up.

5. **Build & install `Droid_Phone_Controller`** onto the phone, pair with
   the receiver over BLE.

6. **Author animations in Bottango Studio.** Open `grogu.btngo` — see
   [bottango_setup.md](bottango_setup.md) for preparing the 3D body model
   (Project Gogurt STL parts → OBJ). Export the Code Command Stream
   (`GeneratedCodeAnimations.h`/`.cpp`) into `src/` and switch the board to
   Export mode so ESP-NOW triggers play; details in [README.md](README.md)
   → "Updating animations from Bottango Studio".

Once steps 2–4 are done the two boards talk on their own; the phone app
(step 5) and Bottango authoring (step 6) can be redone any time without
re-pairing the boards.
