# EVBot Add-ons - Plan

**Status:** draft for discussion (2026-10-21). Nothing built yet.

**Goal:** students can plug extra parts into the car - bumpers, servos, distance sensors, displays, extra motors - and use them with the same simple style as the rest of EVBot:

```cpp
EVBotBumper bumper(PORT_A);
if (bumper.pressed()) robot.stop();
```

---

## 1. What the EVDrive board offers today

From the EVDrive pinout (rhcircuits site, `docs/pinout.md`) and the pins EVBot already uses.

### Expansion header J6 (10 pins: GND, +3V3, 8 GPIOs)

| J6 pin | GPIO | Usable for add-ons? | Why |
|---|---|---|---|
| 3 | 21 | ✅ yes | plain GPIO |
| 4 | 47 | ✅ yes | plain GPIO (check logic level on modules with 1.8 V octal flash) |
| 10 | 38 | ✅ yes | plain GPIO |
| 5 | 48 | ⚠️ shared | the DevKitC-1's RGB LED is on GPIO 48 - EVBot uses it as the status light |
| 6 | 45 | ⚠️ careful | strapping pin (flash voltage select): an external pull-up at power-on can stop the board booting. OK as an output, or an input without a pull-up |
| 7 | 35 | ❌ no | used by the octal PSRAM on N8R8 / N16R8 modules |
| 8 | 36 | ❌ no | same |
| 9 | 37 | ❌ no | same |

### Other limits

- **No analog inputs on J6.** The ESP32-S3 ADCs are only on GPIO 1-20; every J6 pin is outside that range. Analog sensors (IR line sensors, potentiometers) need an I2C ADC.
- **Only 3.3 V on J6.** Servos and HC-SR04 ultrasonic sensors need 5 V; the HC-SR04's 5 V echo output can damage a 3.3 V pin. The board's 5 V is only on the J5 screw terminal.
- **No free I2C port.** Both I2C controllers are in use (`Wire`: gyro + encoder A on J1; `Wire1`: encoder B on J2), and J1/J2 are taken by the encoders. Bus 2 carries only encoder B (`0x36`), so it has plenty of spare capacity - it just has no connector.

**Summary:** about 3 safe digital pins, no analog, no I2C port. Enough for bumpers and a servo, not for a sensor ecosystem.

> **Site fix needed:** `docs/modifications.md` says J6 has "8 extra digital/analog pins". On the recommended N8R8 module, GPIO 35-37 are not available, and none of the J6 pins can read analog.

---

## 2. Hardware plan

### 2a. Now (no PCB change): J2 Qwiic pass-through adapter

A small board that sits between J2 and encoder B:

- JST-XH 4-pin in (from J2) and JST-XH 4-pin out (to the encoder B breakout)
- one or two **Qwiic / STEMMA QT** sockets (JST-SH 4-pin, 3.3 V) on the same bus 2

This opens up the whole range of plug-in I2C parts with keyed cables and no wiring mistakes:

| Kind | Example parts | Address |
|---|---|---|
| Distance (non-laser) | I2C ultrasonic (e.g. RCWL-1601 / SparkFun Qwiic Ultrasonic) | 0x00-0x7F, check |
| Distance (laser, classroom only) | VL53L1X ToF | 0x29 |
| Colour | TCS34725 / APDS-9960 | 0x29 / 0x39 |
| Line sensing | I2C reflectance array, or ADS1115 + IR sensors | 0x48 |
| Display | SSD1306 OLED 128x64 | 0x3C |
| Servos | PCA9685 16-channel PWM driver | 0x40 |
| Extra motors | I2C DC motor driver (e.g. Qwiic Motor Driver) | 0x5D |

Address clashes to watch: VL53L1X and TCS34725 are both 0x29.

### 2b. Next PCB revision

- A Qwiic connector on bus 2 (makes the adapter unnecessary).
- 2-3 three-pin servo headers (Signal / 5 V / GND) with a 5 V rail rated ~2 A, fused, separate from the logic 5 V.
- Move J6 off GPIO 35-37 and 45 onto ADC-capable pins (candidates: 8, 10, 11, 12, 17, 18 - check against the DevKitC-1 pin list and the TMC2209 sockets).
- Label the add-on pins on the silkscreen as **A, B, C...** to match the software port names.

---

## 3. Software design

### 3a. Ports instead of pin numbers

Students use port names that match a label on the car:

| Port | GPIO (current board) | Notes |
|---|---|---|
| `PORT_A` | 21 | |
| `PORT_B` | 47 | |
| `PORT_C` | 38 | |
| `PORT_I2C` | bus 2 (`Wire1`) via the Qwiic adapter | |

EVBot keeps a table of reserved / unsafe pins and refuses them with an `Oops!` (for example, using 48 while the status light is on, or 35-37 on an R8 module).

### 3b. Add-on objects (student API)

```cpp
EVBotBumper   bumper(PORT_A);   bumper.pressed()            // true while pressed
EVBotServo    arm(PORT_B);      arm.angle(90)               // 0-180 degrees
EVBotDistance eyes;             eyes.cm()                   // on PORT_I2C
EVBotDisplay  screen;           screen.show("Hi!")          // on PORT_I2C
EVBotLights   strip(PORT_C, 8); strip.color(2, RED)         // addressable LED strip
EVBotColor    floorColor;       floorColor.isRed()          // on PORT_I2C
```

Rules every add-on follows:
- Created as a global next to `EVBot robot;` - nothing extra to set up.
- Starts itself when `robot.begin()` runs; if the part isn't found, it prints a friendly error and keeps the program running:
  `Oops! No distance sensor on the I2C port - is it plugged in?`
- Every value is in kid units (cm, degrees, true/false).

Optional safety link: `bumper.stopsRobot(true)` makes any running `forward()`, `drive()` or turn stop when the bumper is pressed.

### 3c. Under the hood

- **Base class** `evbot::Addon`: `begin()`, `poll()`, `name()`, `report()` (a short "name: value" line for the uploader page and CarCheck). Add-ons register themselves in a registry when constructed.
- **Polling:** add-ons on bus 2 must not collide with the sensor task, which reads encoder B ~400 times a second. Either the sensor task also polls add-ons (round-robin, ~50 Hz each), or all bus-2 access goes through a lock. Decide in phase B after measuring the I2C time budget at 100 kHz (maybe raise bus 2 to 400 kHz).
- **Servos:** ESP32 LEDC PWM (50 Hz), so no extra library; PCA9685 for more than two.
- **Escape hatch for teachers:** `robot.i2c()` returns the locked bus-2 `TwoWire&`, and `robot.pin(PORT_A)` returns the GPIO number, so any existing Arduino sensor library can be used.

### 3d. Packaging: a separate library

Add-ons go in a companion library, **EVBot-Addons** (`depends=EVBot` in `library.properties`), not in EVBot itself. Arduino compiles every `.cpp` in a library whether it is used or not, so drivers inside the core would slow down every student's compile (the first compile was just cut from 671 s to 217 s by dropping the Arduino BLE library). Options inside EVBot-Addons:
- one `.cpp` per add-on (compiled always, but small), or
- header-only add-ons (compiled only when included) - preferred if the drivers stay small.

### 3e. Tools that come along

- **Teacher_CarCheck:** scans the ports and the I2C bus, lists the add-ons it finds, and flags address clashes.
- **EVBotUploader page:** a live "Sensors" panel showing each add-on's `report()` line.
- **EVBotPy:** the same classes in Python (see `EVBotPy/PLAN.md`).
- **Lessons:** 14 "Stop at the bumper", 15 "How far is the wall?", 16 "Show it on the screen", 17 "Wave the arm" (servo).

### 3f. Competition note

Science Olympiad 2027 bans laser/ToF distance sensors and cameras on the vehicle. EVBot is a teaching platform, so they are fine in class, but the docs should mark each add-on as **competition-legal** or **classroom only**.

---

## 4. Phases

| Phase | What | Needs |
|---|---|---|
| **A** | Confirm hardware: module shipped in kits, which J6 pins really work, 5 V budget; fix the site's J6 text | a kit, a meter |
| **B** | Core framework (ports, reserved-pin table, `Addon` base, registry, polling/locking) + **bumper** + **servo**; lesson 14 | a microswitch, a servo, a 5 V supply |
| **C** | J2 Qwiic adapter (prototype on perfboard first) + **distance**, **display**, **colour**; lessons 15-16; CarCheck scan; uploader Sensors panel | adapter parts, 3 breakouts |
| **D** | Extra motors: PCA9685 servos, I2C DC motor driver; lesson 17 | driver boards |
| **E** | PCB revision with Qwiic + servo headers + ADC-capable J6 | board respin |

---

## 5. Open questions

1. Which ESP32-S3 module do the kits ship with - N8R8 (as the materials list recommends) or N16R8? Both lose GPIO 35-37.
2. Is the DevKit's RGB LED on GPIO 48 on every kit board, or should EVBot give up the status light so PORT D (48) becomes free?
3. Is a J2 pass-through adapter acceptable as a kit accessory, or wait for the PCB revision?
4. Which three add-ons matter most for the first lessons?
5. Is a PCB revision planned for the 2027 season, and is there room on the board for a 5 V servo regulator?
6. Should add-ons be visible in the competition firmware (PushReverseStepper) too, or only in EVBot?
