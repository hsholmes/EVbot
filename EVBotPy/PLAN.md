# EVBotPy - plan: MicroPython version of EVBot

> **Moved 2026-10-20** from the EV202627 repo into the EVbot repo
> (github.com/hsholmes/EVbot). In this plan, `EVBot/` means the root of this
> repository - for example `EVBot/src` is now just `src/`.

Goal: students in grades 4-7 program the same Scioly EV ESP32-S3 car in Python,
with the same commands, lessons and driving quality as the C++ EVBot library.

## Ground rules (decided 2026-10-20)

- **MicroPython** runs on the car. Students' `main.py` runs on the robot itself, so it is
  not remote control.
- **The C++ library stays as it is.** Nothing under `EVBot/` is edited. The Python firmware
  compiles the C++ engine source files *from* `EVBot/src` without changing them, so a fix
  to the C++ engine reaches both versions automatically.
- **The two versions live side by side.** A car runs either the C++ firmware or the Python
  firmware. The teacher switches by flashing over USB.
- **There is a separate upload page.** `EVBotPy/extras/EVBotPyUploader.html` is its own
  page. The C++ `EVBotUploader.html` stays as it is.
- The user commits; nothing is committed automatically.
- **Module: ESP32-S3 N16R8** (16 MB flash, 8 MB octal PSRAM). Build target is the
  ESP32_GENERIC_S3 `SPIRAM_OCT` variant. Octal PSRAM uses GPIO 35-37, and none of the
  car's pins clash with them.
- **The page has a Live tab.** You type one command and the car does it. This is the
  MicroPython REPL, the interactive prompt.
- **USB is a second way to program the car.** The page itself can connect over USB as
  well as Bluetooth. Thonny and mpremote also work, because the car runs standard
  MicroPython.

## Architecture

```
 student main.py   (robot.forward(50) ...)
        |
 evbot  (MicroPython C module: modevbot.cpp, a thin binding)
        |
 EVBot.cpp + internal/{Engine,Motion,Gyro,Encoders,Steppers,Board}.cpp   <- compiled from EVBot/src, not copied
        |
 arduino_shim/  (Arduino.h, Wire.h, Preferences.h, WString.h, Serial, hw timer
                 written on plain ESP-IDF drivers)
        |
 ESP-IDF (the one MicroPython is built on)

 frozen Python: boot.py + evbot_ble.py   (BLE upload service, Python version)
 wireless_mp.cpp: stub for internal/Wireless.h (replaces Wireless.cpp)
```

### Why a shim, rather than porting or copying the engine

The engine uses only a small set of Arduino functions. Counted in `EVBot/src/internal`:
`millis/micros/delay`, `pinMode/digitalWrite/digitalRead`, `Wire` (begin, setClock,
beginTransmission, write, endTransmission, requestFrom, read, available),
`Preferences` (getFloat, getString, putString), `timerBegin/timerAttachInterrupt/timerAlarm`,
`rgbLedWrite`, `analogReadMilliVolts`, `String`, and `Serial.print/println/printf`.
Re-creating those on ESP-IDF is roughly 400 lines and leaves the library untouched.

The alternatives are worse:
- **Copying the engine** means every fix has to be made twice, and the copies drift apart.
- **Using arduino-esp32 as an IDF component inside MicroPython** is fragile: arduino-esp32
  3.3.12 needs IDF 5.5, while MicroPython pins its own IDF version.

`Wireless.cpp` is **not** compiled. It owns NimBLE, and MicroPython's `bluetooth` module
needs NimBLE for itself. `wireless_mp.cpp` provides the same `Wireless.h` functions as
no-ops: `begin`, `print`, `waitForPageAfterRestart`, and so on. Messages reach the page
another way (see "Output path" below).

### Key design points

1. **`delay()` is the interrupt point.** Every blocking loop in the engine (Motion.cpp
   lines 244/332/423/451, the turn settle, gyro calibration) waits with `delay()`. The
   shim's `delay()`:
   - waits with `vTaskDelay`, releasing the MicroPython GIL while it waits;
   - runs MicroPython's pending scheduler, so BLE events (STOP) and Ctrl-C are noticed;
   - if a KeyboardInterrupt is pending, calls `steppers::lockout(true)` so the wheels
     stop and the C++ move loop exits on its own.

   Only after the C++ call returns does the binding raise the exception
   (`mp_handle_pending(true)`). The exception never jumps through C++ stack frames.

   The steps above apply only when `delay()` is called on the MicroPython task. Calls
   from the sensor task just sleep.

2. **Two waits are reimplemented in the binding.** `wait()` and `wait_for_button()` are
   rewritten with `mp_hal_delay_ms`. Their C++ loops would never end under a lockout
   (`while (!buttonPressed()) delay(10)`).

3. **The engine starts once per power-on.** Its timers, sensor task and gyro calibration
   keep running across MicroPython soft resets. Running a new program therefore takes
   under a second, with no 7 s gyro wait. There is one static C++ `EVBot` object.
   - On the first `robot.begin()` after power-on, the binding runs the full `EVBot::begin()`.
   - Each later `robot.begin()` is quick: `stop()`, unlock the motors, `set_speed(MEDIUM)`,
     `reset_heading()`, `reset_distance()`, and show the blue "ready" light.

4. **The car stops when a program ends.** The board's `MICROPY_BOARD_END_SOFT_RESET` hook
   calls `stop()`. This covers a normal finish, an error, Ctrl-C or the STOP button, and
   matters most after `drive()` / `spin_left()`, which keep going on their own. In C++
   the car keeps driving after `setup()` ends; in Python it stops. The teacher guide
   documents this difference.

5. **Output and input path (one terminal for USB and Bluetooth).** The shim's `Serial`
   writes to MicroPython stdout, which already goes to USB. `boot.py` also attaches a
   BLE stream with `os.dupterm` once an authorised page subscribes:
   - **Out:** stdout goes to the LOG notify characteristic. `print()`, `robot.say()`,
     "Oops!" messages, tracebacks and the REPL prompt all travel the same way.
   - **In:** bytes written to the TERM characteristic are given to MicroPython as
     keyboard input. This gives a full REPL over Bluetooth, and Ctrl-C (0x03) works
     there exactly as it does on USB.

   Lines starting with `[EVBot]` are engine details. The page shows them only when "show
   details" is ticked. With no page connected, BLE output is simply dropped and never
   blocks.

6. **NVS and the partition table (16 MB flash).** NVS stays at 0x9000 with the same size
   as the Arduino layout. This keeps two things across firmware switches:
   - the `scioly_ev` calibration the engine reads;
   - the car name and PIN in the `evbot` namespace, readable from Python with
     `esp32.NVS`.

   Layout: `nvs` (0x9000, 24 KB), `otadata`, `phy_init`, `ota_0` 3 MB, `ota_1` 3 MB, then
   `vfs` for the remaining ~9.9 MB. Firmware OTA is not in version 1. The two app slots
   are reserved now anyway: changing the partition table later would wipe every
   student's files.

7. **PSRAM.** The MicroPython heap lives in the 8 MB PSRAM. The step ISR, its variables
   and the shim's timer code must stay in internal RAM/IRAM. While flash is being
   written, the cache is off, and with octal PSRAM that also blocks PSRAM. That's why
   file saves are refused while the wheels turn.

8. **Live tab = REPL with a ready robot.** When the Live tab opens, the page quietly
   sends `from evbot import *; robot = EVBot(); robot.begin()`. The student then types
   `robot.forward(20)` or `robot.light(RED)`. The engine is already running, so
   `begin()` is instant. The REPL never soft-resets between lines, so the end-of-program
   stop hook doesn't fire. Instead, Stop and Esc send Ctrl-C followed by
   `robot.stop()`.

## Python API (same as C++, in Python naming style)

```python
from evbot import *
robot = EVBot()
robot.begin()
robot.wait_for_button()
for i in range(4):
    robot.forward(50)
    robot.turn_right(90)
robot.light(GREEN)
```

| C++ | Python |
|---|---|
| begin, forward, backward, stop, wait, beep | same names |
| turnLeft / turnRight | turn_left / turn_right |
| setSpeed | set_speed |
| light(color) / light(r,g,b) | same |
| buttonPressed / waitForButton | button_pressed / wait_for_button |
| heading, distance, battery | same (return float) |
| say(...) / say(label, n) | say(...) / say(label, n); `print()` also works |
| drive, spinLeft/Right, arcLeft/Right | drive, spin_left/right, arc_left/right |
| resetDistance / resetHeading | reset_distance / reset_heading |
| SLOW MEDIUM FAST, RED ... WHITE, OFF | same constants |

`EVBot()` always returns the same robot. Bad values behave as in C++: the C++ code prints
"Oops! ..." and turns the light red, and the program carries on. Wrong argument *types*,
such as `robot.forward("ten")`, raise a short `TypeError` in plain words.

## Folder layout (all new)

```
EVBotPy/
  PLAN.md
  README.md
  firmware/
    boards/EVBOT_S3/          board.json, mpconfigboard.{h,cmake}, sdkconfig.board, partitions.csv
    evbot_module/             micropython.cmake, modevbot.cpp, wireless_mp.cpp
    arduino_shim/             Arduino.h, Wire.h, Preferences.h, WString.h, shim .cpp files
    frozen/                   boot.py, evbot_ble.py (incl. dupterm BLE stream), manifest.py
    build.sh                  docker run espressif/idf:<pinned> ... (Windows: Docker Desktop or WSL2)
    dist/                     built .bin for teachers (bootloader, partitions, app, or one merged image)
  examples/                   01_hello_light.py ... 13_my_steering.py, teacher_car_check.py, teacher_car_setup.py, teacher_beep_test.py
  extras/
    EVBotPyUploader.html      student page (Web Bluetooth)
    EVBotPyFlasher.html       teacher page: flash the firmware over USB (esptool-js, Web Serial)
    CheatSheet.md
    TeacherGuide.md
```

## BLE protocol (Python version)

This uses a **different service UUID** from the C++ uploader, so neither page connects
to the wrong firmware. The Python page also lists the C++ UUID in `optionalServices`.
If it finds that service, it says "This car runs the C++ firmware - use EVBotUploader or
flash Python".

| Characteristic | Direction | Contents |
|---|---|---|
| CMD | write | `PIN:<pin>`, `INFO`, `FILE_BEGIN:<name>:<bytes>`, `FILE_END:<crc32>`, `RUN`, `STOP`, `GET:<name>`, `LS`, `RESTART` |
| DATA | write / write without response | file bytes |
| STATUS | notify | `AUTH:OK/BAD`, `INFO:name=..;ver=..;fw=py`, `FILE:READY`, `FILE:ACK:<n>`, `FILE:OK`, `FILE:ERR:<text>`, `RUNNING`, `STOPPED`, `DONE`, `ERROR` |
| LOG | notify | stdout (`print`, `say`, tracebacks, REPL output) |
| TERM | write without response | REPL keyboard input (Live tab), including Ctrl-C |

Every command except `PIN` and `INFO` needs the PIN first, on each connection, as in C++.

- `RUN` saves the file as `main.py` and does a soft reset.
- `STOP` calls `evbot._emergency_stop()` (C: motor lockout, then
  `mp_sched_keyboard_interrupt()`).

Programs are only a few KB, so an upload takes under a second. There is no firmware OTA
over BLE in the first version.

## USB (second way)

The car runs standard MicroPython, so USB needs nothing special on the car side. Three
ways work:

1. **The same page over USB.** It has a "Connect: Bluetooth | USB" switch, and USB uses
   Web Serial. The page talks to MicroPython's raw REPL, as mpremote does:
   - write the file with `open('main.py','w').write(...)`, in chunks;
   - run it with a soft reset (Ctrl-D);
   - stop it with Ctrl-C.

   No PIN is asked for, because having the cable is enough. The Program and Live tabs,
   the output box and the Stop button behave the same as over Bluetooth.
2. **Thonny**, with its "MicroPython (ESP32)" interpreter. It is good for older students
   and has a file browser and a debugger-lite.
3. **mpremote**, for teachers and scripts: `mpremote cp main.py : + reset`.

Notes:
- Find out which USB port the car exposes:
  - the native USB-OTG port gives TinyUSB CDC;
  - the UART port has a USB-serial chip.

  Both carry the REPL. Build the firmware for whichever one the car's cable uses (phase 0).
- The page's transport layer has two parts. A shared "terminal" interface (write
  bytes / receive bytes) runs over either BLE TERM+LOG or Web Serial. A small
  file/run/stop layer sits on top. Both links then share all the page's code, apart
  from connecting.

## Upload page (EVBotPyUploader.html)

A single offline HTML file, like the C++ one. It needs Chrome or Edge, using Web
Bluetooth or Web Serial. It has:

- Connect over Bluetooth (with PIN; the last car is remembered) or USB.
- **Program tab** and **Live tab**:
  - The Live tab is a terminal with a one-line command box. Up/Down gives history, and
    Tab completion uses MicroPython's own completion. A row of quick buttons inserts
    `robot.forward(10)`, `turn_left(90)`, `light(GREEN)` and similar.
  - Code tried in Live can be copied into the Program tab with one click.
  - Live is turned off while a program runs; pressing Stop turns it back on.
- A code editor: a simple editor written into the page, with line numbers, auto-indent,
  Tab = 4 spaces, and Python colouring. No CDN, so it works offline.
- Open/Save a `.py` file on the laptop, plus a dropdown of the lesson examples built into
  the page.
- Buttons: **Run** (upload + run), **Stop** (big and red, also the Esc key), **Get program
  from car**.
- An output box. In a traceback the page links "line N" to that line in the editor, and
  an error banner explains it in simple words.
- A hidden teacher panel (`#teacher`) to set the car's name and PIN and see battery and
  calibration info.

## Phases

### Phase 0 - check it can work (spikes, no student code)

1. The module is an N16R8. Confirm it with `esptool flash_id`, and find out which USB
   port the car's cable uses (native OTG or UART). Pick the MicroPython release and its
   IDF version, and pin both in `build.sh`.
2. Build stock MicroPython for ESP32_GENERIC_S3 (SPIRAM_OCT) with the custom 16 MB
   partition table, and flash it. Check:
   - the REPL over USB in Thonny;
   - 8 MB of PSRAM visible in `gc.mem_free()`;
   - BLE advertising;
   - that `esp32.NVS("scioly_ev")` still reads the calibration after the switch.
3. Do a compile-only build of `EVBot/src` against an empty shim to list every missing
   symbol.
4. Find out how BLE IRQs reach Python on the esp32 port (sync event + GIL, or the
   scheduler). Confirm that releasing the GIL inside `delay()` lets a STOP write get
   through while C++ code is blocking.

**Done when:** the car runs stock MicroPython, the NVS values survive, and the list of
missing symbols is known.

### Phase 1 - shim and engine on MicroPython

- Write the shim on ESP-IDF drivers:
  - GPIO;
  - the new i2c_master driver for two buses, or the legacy driver;
  - gptimer for the step timers. The ISR stays in IRAM, and `timerAlarm` must be callable
    from the ISR;
  - `esp_timer`;
  - nvs;
  - led_strip/RMT for the WS2812;
  - adc_oneshot with calibration.
- Make sure `machine.Timer` and `machine.I2C` cannot claim the same timers or I2C ports.
- Write a minimal binding: `begin`, `forward`, `turn_left/right`, `light`.

**Done when** these pass on the car over USB (Thonny or mpremote):
- a square test;
- distance and turn errors match the C++ SquareTest (turns within about 0.3 deg);
- no low-speed shiver;
- pulse spacing checked on a scope or logic analyser if one is available.

### Phase 2 - full API and safety

- Bind every command in the table above, including the quick re-`begin()`.
- Make the `delay()` interrupt point work, and check that every blocking move ends
  quickly under lockout: forward, backward, turns, arcs, beep, gyro re-zero.
- Add the end-of-program stop hook and argument type checks.

**Done when** each of these stops the car within about 100 ms and leaves it ready for the
next run:
- Ctrl-C during `forward(200)`;
- Ctrl-C during `turn_left(720)`;
- Ctrl-C during `wait_for_button()`;
- an error in the middle of `drive()`;
- a program that ends while `drive()` is running.

### Phase 3 - BLE service (Python, frozen)

- Write `evbot_ble.py` with the protocol above: name/PIN from NVS `evbot`, file write with
  a CRC check, RUN/STOP/GET/LS.
- Add the dupterm BLE stream (LOG out, TERM in), which gives a full REPL over Bluetooth.
- `boot.py` starts BLE only when WL_SW is on (checked every 0.5 s, as in C++). It must
  never block `main.py`.

**Done when:**
- the page-less test (nRF Connect, or a small script) can upload, run, stop and read
  output;
- an upload-and-run cycle takes under 2 s;
- the REPL over BLE answers `1+1`;
- Ctrl-C over TERM stops `robot.forward(200)`.

### Phase 4 - EVBotPyUploader.html

Build the page described above. First build the terminal transport (BLE and Web Serial),
then Program, then Live. **Done when:**
- a student can, without a USB cable: open a lesson, run it, see `print` output live, hit
  Stop, fix an error by clicking its line, and run it again;
- the same works over a USB cable;
- in Live, `robot.forward(20)` drives and Stop works;
- it works on Windows and Mac in Chrome and Edge.

### Phase 5 - lessons and docs

- Port lessons 01-13 to `.py`, following the C++ lessons one for one so a class can use
  either language.
- Write the teacher scripts (car check, car setup, beep test).
- Write `CheatSheet.md` (Python) and `TeacherGuide.md`, covering:
  - flashing;
  - switching between C++ and Python;
  - the "car stops when the program ends" difference;
  - using the Live tab in class (a warm-up before writing programs);
  - USB as the second way to program the car: the page's USB mode, Thonny setup, and
    mpremote;
  - common Python errors kids hit, like indentation and missing `()`.

**Done when:** every lesson runs on the car through the page.

### Phase 6 - flasher and release

- `EVBotPyFlasher.html`: esptool-js over Web Serial flashes the merged image at 0x0 from a
  file the teacher picks. It must not erase NVS.
- Switching back to C++ is a normal Arduino IDE upload over USB.
- Commit the built images to `firmware/dist/` with a version number. `INFO` reports the
  version, and the page warns if the version is old.

## Risks and how they are handled

| Risk | Handling |
|---|---|
| A future C++ library change uses an Arduino function the shim lacks | The Python build fails at compile time, saying which one. Add it to the shim. |
| Step ISR jitter from MicroPython (GC, flash writes during file save) | ISR and its data stay in IRAM. Flash writes disable cache, so no file writes happen while wheels turn: upload is refused while a program runs. Measure in phase 1. |
| GIL / BLE event delivery differs from expectations | Phase 0 spike 4 checks it before any page work. |
| Octal PSRAM + flash writes stall the step ISR | ISR, its data and the shim timer code go in IRAM/DRAM, never PSRAM. No file writes while wheels turn. Measure pulse gaps during a file save in phase 1. |
| A Live-tab `drive()` keeps going after the student forgets `stop()` | The Stop button and Esc are always visible. Closing the tab or disconnecting sends Ctrl-C + `robot.stop()`, and the car side stops the motors when BLE disconnects. |
| Thonny's file save during a run | Thonny interrupts the program first, with Ctrl-C, so the engine stops before the flash write. Check this in phase 5. |
| Teachers mixing up firmwares | Different BLE UUIDs, the page detects the C++ firmware, and INFO reports `fw=py`. |
| Students' long `while True` loops without waits starving BLE | The MicroPython VM checks pending events regularly, so STOP still works. The teacher guide shows `robot.wait(0.05)` in loops. |

## Answered (2026-10-20)

- Module: N16R8 (16 MB flash, 8 MB octal PSRAM).
- Live command tab: yes.
- USB: yes, as a second way, through the page's USB mode, Thonny, or mpremote.

## Still open

- Which USB port the car's cable uses (native OTG or UART). Phase 0 finds out.
