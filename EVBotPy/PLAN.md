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

 frozen Python: main.py + evbot_run.py + evbot_ble.py   (program runner, BLE upload service)
 wireless_mp.cpp: stub for internal/Wireless.h (replaces Wireless.cpp)
```

### Why a shim, rather than porting or copying the engine

The engine uses only a small set of Arduino functions. Counted in `EVBot/src/internal`:
`millis/micros/delay`, `pinMode/digitalWrite/digitalRead`, `Wire` (begin, setClock,
beginTransmission, write, endTransmission, requestFrom, read, available),
`Preferences` (begin, getFloat, end), `timerBegin/timerAttachInterrupt/timerAlarm`,
`rgbLedWrite`, `analogReadMilliVolts`, `String`, and `Serial.print/println/printf`.
Re-creating those on ESP-IDF is roughly 400 lines and leaves the library untouched.
The full list, with the ESP-IDF function behind each one, is in
`firmware/arduino_shim/SYMBOLS.md`.

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

4. **The car stops when a program ends** (decided 2026-10-20). Programs are run by
   a frozen helper, `evbot_run.run("main.py")`. It reads the file, runs it in a
   fresh namespace inside `try/finally`, and the `finally` stops the car. There is no
   soft reset, so Bluetooth stays connected (see "Phase 0 findings"). This covers a
   normal finish, an error, Ctrl-C or the STOP button, and matters most after
   `drive()` / `spin_left()`, which keep going on their own. In C++ the car keeps
   driving after `setup()` ends; in Python it stops. The teacher guide documents
   this difference.
   - **Run from the page** (Bluetooth or USB): the page writes `main.py`, then types
     `import evbot_run; evbot_run.run("main.py")` into the raw REPL.
   - **Power-on and Ctrl-D:** `pyexec_file_if_exists("main.py")` looks for a frozen
     `main.py` before the file on the car. The firmware freezes a two-line `main.py`
     that starts BLE and calls `evbot_run.run("main.py")`. `open()` reads the
     student's file on the car, so it keeps the usual name, and nothing runs twice.
   - **Thonny's Run button** executes the editor's script straight in the REPL,
     without `evbot_run`. A `drive()` left running there keeps going until Stop. The
     guide says so.

5. **Output and input path (one terminal for USB and Bluetooth).** The shim's `Serial`
   writes to MicroPython stdout, which already goes to USB. `evbot_ble` also attaches a
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

   Layout (`firmware/boards/EVBOT_S3/partitions.csv`): the same as Arduino's
   "16M Flash (3MB APP/9.9MB FATFS)" scheme. That is `nvs` (0x9000, 20 KB = 0x5000, as in
   every Arduino scheme), `otadata` (0xe000), `ota_0` 3 MB, `ota_1` 3 MB, then `vfs` for
   the remaining ~9.9 MB. There is no `phy_init`, as in Arduino. Firmware OTA is not in version 1. The two app slots
   are reserved now anyway: changing the partition table later would wipe every
   student's files.

7. **PSRAM.** The MicroPython heap lives in the 8 MB PSRAM. The step ISR, its variables
   and the shim's timer code must stay in internal RAM/IRAM. While flash is being
   written, the cache is off, and with octal PSRAM that also blocks PSRAM. That's why
   file saves are refused while the wheels turn.

8. **Live tab = REPL with a ready robot.** When the Live tab opens, the page quietly
   sends `from evbot import *; robot = EVBot(); robot.begin()`. The student then types
   `robot.forward(20)` or `robot.light(RED)`. The engine is already running, so
   `begin()` is instant. Live lines don't go through `evbot_run`, so nothing stops the
   car between lines. Instead, Stop and Esc send Ctrl-C followed by
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
    frozen/                   main.py (starts BLE, then evbot_run), evbot_run.py, evbot_ble.py (incl. dupterm BLE stream), manifest.py
    build.ps1                 docker run espressif/idf:v5.5.2 (Docker Desktop); MicroPython v1.29.0
    docker-build.sh           what build.ps1 runs inside the container
    build-out/                local build output (gitignored)
    dist/                     released .bin parts for teachers (bootloader, partition table, otadata, app)
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
| CMD | write | `PIN:<pin>`, `INFO`, `FILE_BEGIN:<name>:<bytes>`, `FILE_END:<crc32>`, `STOP`, `GET:<name>`, `LS`, `RESTART` |
| DATA | write / write without response | file bytes |
| STATUS | notify | `AUTH:OK/BAD`, `INFO:name=..;ver=..;fw=py`, `FILE:READY`, `FILE:ACK:<n>`, `FILE:OK`, `FILE:ERR:<text>`, `RUNNING`, `STOPPED`, `DONE`, `ERROR` |
| LOG | notify | stdout (`print`, `say`, tracebacks, REPL output) |
| TERM | write without response | REPL keyboard input (Live tab), including Ctrl-C |

Every command except `PIN` and `INFO` needs the PIN first, on each connection, as in C++.

- There is no `RUN` command. To run, the page sends `FILE_BEGIN:main.py`... and then
  types `evbot_run.run("main.py")` into the raw REPL over TERM, exactly as it does
  over USB. `evbot_run` reports `RUNNING` and then `DONE` / `STOPPED` / `ERROR` on
  STATUS when BLE is up.
- `STOP` calls `evbot._emergency_stop()` (C: motor lockout, then
  `mp_sched_keyboard_interrupt()`).
- The TERM write handler checks for 0x03 itself and does the same as `STOP`. A
  running program doesn't read stdin, so Ctrl-C would otherwise wait in the
  dupterm buffer. USB CDC already handles Ctrl-C in its receive interrupt.
- A soft reset (`RESTART`, or Ctrl-D) still drops BLE (`mp_bluetooth_deinit`).
  The page reconnects as the C++ page does after an upload.

Programs are only a few KB, so an upload takes under a second. There is no firmware OTA
over BLE in the first version.

## USB (second way)

The car runs standard MicroPython, so USB needs nothing special on the car side. Three
ways work:

1. **The same page over USB.** It has a "Connect: Bluetooth | USB" switch, and USB uses
   Web Serial. The page talks to MicroPython's raw REPL, as mpremote does:
   - write the file with `open('main.py','w').write(...)`, in chunks;
   - run it with `evbot_run.run("main.py")`, the same call as over Bluetooth;
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
   port the car's cable uses (native OTG or UART). The MicroPython release and the IDF
   version are pinned in `build.ps1`.
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
- Add `evbot._program_end()`, which `evbot_run` calls in its `finally`. It ends free
  driving, stops the wheels and does nothing before the first `begin()`. Also add
  the argument type checks.

**Done when** each of these stops the car within about 100 ms and leaves it ready for the
next run:
- Ctrl-C during `forward(200)`;
- Ctrl-C during `turn_left(720)`;
- Ctrl-C during `wait_for_button()`;
- an error in the middle of `drive()`;
- a program that ends while `drive()` is running.

### Phase 3 - BLE service (Python, frozen)

- Write `evbot_ble.py` with the protocol above: name/PIN from NVS `evbot`, file write with
  a CRC check, STOP/GET/LS.
- Add the dupterm BLE stream (LOG out, TERM in), which gives a full REPL over Bluetooth.
- The frozen `main.py` starts BLE only when WL_SW is on (checked every 0.5 s, as in C++),
  then calls `evbot_run.run("main.py")`. Starting BLE must never block the program.

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

- `EVBotPyFlasher.html`: esptool-js over Web Serial flashes the four parts at the offsets
  in `flash-offsets.txt`, from files the teacher picks. **Never the merged
  `firmware.bin`**: MicroPython's `makeimg.py` fills 0x9000-0x10000 with 0xFF, which
  wipes NVS (the calibration and car name/PIN). `ota_data_initial.bin` resets
  `otadata`, in case a C++ Bluetooth upload left the car booting from `ota_1`.
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

## Phase 0 findings so far (2026-10-20, from reading the source)

These come from reading the engine and MicroPython's esp32 port (master, around
v1.29.0). Nothing has been built or run yet.

- **Versions pinned.** MicroPython v1.29.0 (the latest release), on the ESP-IDF
  version that release's README recommends, v5.5.2 (`espressif/idf:v5.5.2`).
  That is the same IDF 5.5 line that arduino-esp32 3.3.12 uses. (MicroPython's
  master branch now recommends v5.5.5. Move up together with the next MicroPython
  release.)
- **Spike 3 is done on paper.** `firmware/arduino_shim/SYMBOLS.md` lists every
  Arduino symbol the engine uses. The list is shorter than this plan first
  said: Preferences needs only `begin/getFloat/end`.
- **Spike 4 is answered by the source.** The esp32 port uses
  `MICROPY_PY_BLUETOOTH_USE_SYNC_EVENTS`. BLE IRQ handlers run on the NimBLE
  task, which is pinned to core 1 like the MicroPython task, and take the GIL
  rather than waiting for the scheduler. So a STOP write is handled while C++
  code blocks, as long as the shim's `delay()` releases the GIL. A test on the
  car is still needed.
- **New: tick rate.** MicroPython leaves FreeRTOS at 100 Hz, and Arduino uses
  1000 Hz. The engine's `delay(1)` loops and the sensor task depend on 1 ms
  ticks. `sdkconfig.board` sets `CONFIG_FREERTOS_HZ=1000`.
- **Problem 1: there is no end-of-program hook.** The esp32 `main.c` has no
  `MICROPY_BOARD_END_SOFT_RESET`, so design point 4 doesn't work as written.
  Also, when `main.py` finishes, MicroPython drops to the REPL *without* a soft
  reset. A car left in `drive()` would keep driving until the next reset.
  (`gc_sweep_all()` does run C finalisers at a soft reset. That covers Ctrl-D,
  but not a program that simply ends.)
- **Problem 2: a soft reset drops Bluetooth.** `soft_reset_exit` calls
  `mp_bluetooth_deinit()`. If RUN does a soft reset (as the BLE protocol
  section says), every run disconnects the page. The page would then have to
  reconnect and could miss the program's first messages.

**Decided (2026-10-20): run through `evbot_run`, never by soft reset.** Design
point 4 has the details. One idea from the first draft was dropped: having
`boot.py` raise `SystemExit` to skip `main.py`. `pyexec` treats `SystemExit` in
boot as a forced exit and soft-resets, so that would loop. A frozen `main.py`
is used instead.

- **First build works (2026-10-20).** It's stock MicroPython v1.29.0 plus the EVBOT_S3
  board and the frozen `main.py`/`evbot_run.py`, with no engine yet. Checked in the
  build's sdkconfig: 16 MB flash, octal PSRAM, 240 MHz, `FREERTOS_HZ=1000`, NimBLE on
  core 1. The partition table matches `partitions.csv`. `micropython.bin` is 1.72 MB,
  55% of a 3 MB app slot. The first try failed to link (`mp_module_string`) because of
  MicroPython bug #19667: `BUILD=` on the make command line leaks into the mpy-cross
  sub-make. `docker-build.sh` now uses the port's default build folder.
- **Flashed to a car and checked (2026-10-21, car MAC 28:84:85:45:f5:40).**
  - `esptool flash-id`: ESP32-S3 rev 0.2, 16 MB flash, 8 MB in-package PSRAM. It is an
    N16R8.
  - Flashed the four parts at 115200 baud, and the hash was verified.
  - On the REPL:
    - the banner prints `EVBot car (ESP32-S3 N16R8)`;
    - the frozen `main.py` + `evbot_run` print "No program on the robot yet";
    - 240 MHz;
    - **8120 KB heap (the PSRAM works)**;
    - the partitions are as designed, running from `ota_0`;
    - the 10 MB file system was formatted on first boot;
    - BLE is active and advertising.
  - **The NVS calibration survived the switch.** `scioly_ev`: `ticks_per_m` 17168,
    `kp_heading` 50, `kx_xtrack` 20, `kp_enc` 5, read with `esp32.NVS.get_blob` as
    4-byte floats.
  - **New: `esp32.NVS` can't read strings.** Arduino `putString` stores `evbot/car_name`
    and `pin` as NVS strings, and MicroPython's `esp32.NVS` has only int and blob
    getters (`ESP_ERR_NVS_NOT_FOUND`). The evbot C module needs a small
    `nvs_get_str` helper for `evbot_ble` (phase 3).
  - **USB:** the car board has two USB sockets. The UART one (CP210x, here COM12) and
    the native one (USB Serial/JTAG, COM3 earlier) both give the REPL. Full-flash reads
    through a USB hub failed with "Corrupt data". Flash with the car plugged straight
    into the laptop.
  - Backups are in `firmware/car-backups/` (gitignored): this car's NVS before the
    switch.

## Phase 1 progress (2026-10-22)

- **Shim + engine + binding build:** `arduino_shim/` (Arduino.h/.cpp, Wire, Preferences,
  WString), `evbot_module/` (modevbot.c tables, evbot_glue.cpp, wireless_mp.cpp,
  micropython.cmake), built with `build.ps1 -Engine`. The engine compiles from `src/`
  unchanged, with no warnings. `micropython.bin` is 1.75 MB (+27 KB).
- Three build fixes were needed:
  - `freertos/idf_additions.h` for `xTaskCreatePinnedToCore`;
  - a clean build when the engine setting changes (stale `frozen_content.c` gave
    "redeclaration of MP_QSTR_evbot");
  - Docker's `Wsl/Service/CreateInstance/0x80072746` (vpnkit-bridge connection reset)
    was cleared by restarting the laptop.
- The quick re-`begin()` needs private EVBot fields. `evbot_glue.cpp` opens them with
  `#define private public` around `#include "EVBot.h"`, so EVBot.h stays unchanged.
- **Start-up on the car:**
  - first `begin()` 7.36 s (full gyro calibration); gyro, both encoder magnets and the
    step timers are OK;
  - second `begin()` 2 ms;
  - battery reads 11.9 V with the main switch on, the light works, and `EVBot()` is a
    singleton.
- **First drive (SLOW):**

  | Move | Time | Heading after | Distance after |
  |---|---|---|---|
  | `forward(20)` | 1.95 s | +0.11 deg | 20.00 cm |
  | `turn_left(90)` | 2.06 s | -90.10 deg | |
  | `turn_right(90)` | 2.04 s | +0.04 deg | |
  | `backward(20)` | 1.93 s | | 0.01 cm |

  Turns are within 0.1 deg, against 0.3 deg in the C++ SquareTest.
- **Square test (MEDIUM, 4 x forward(40) + turn_right(90)), 18.0 s:**
  - every side took 2.53 s and every turn 1.95-2.01 s, steady from side to side;
  - turns ended at 90.17 / 180.12 / 270.09 / 360.11 deg, so each was within 0.17 deg
    and they don't add up;
  - straights held their heading within 0.3 deg;
  - the odometer read 160.05 cm for 160 cm.
- **Physically:** the car ended within about 1-2 cm of the start mark, pointing the same
  way. It drove smoothly with no shiver.
- **Phase 1 is done (2026-10-22).** Only the pulse-spacing check on a scope or logic
  analyser is left, and it's optional; do it if one turns up. That includes measuring
  pulse gaps during a file save, from the risk table.

## Answered (2026-10-20)

- Module: N16R8 (16 MB flash, 8 MB octal PSRAM).
- Live command tab: yes.
- USB: yes, as a second way, through the page's USB mode, Thonny, or mpremote.

## Still open

- Spike 4 on the car (STOP gets through while C++ blocks). It needs the engine, so
  it moves into phase 1.

## Build machine (decided 2026-10-20)

Docker Desktop on Windows, with `firmware/build.ps1`. The MicroPython checkout and
the build folder live in the Docker volume `evbotpy-build`, which is faster than a
shared Windows folder. Output goes to `firmware/build-out/`. `firmware/.gitattributes`
keeps LF line endings, because the container's bash, Kconfig and partition tool fail
on CRLF.
