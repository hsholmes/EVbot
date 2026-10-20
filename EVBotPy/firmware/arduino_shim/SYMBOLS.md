# Arduino symbols the engine needs (phase 0, spike 3)

Found by reading `src/` on 2026-10-20 (no compiler on this laptop yet). It covers
`EVBot.cpp` and `internal/{Board,Encoders,Gyro,Steppers,Motion,Engine}.cpp`.
`Wireless.cpp` is left out, because `wireless_mp.cpp` replaces it. Check this
list with the compile-only build once the toolchain is installed (the plan's
original spike 3).

## Arduino.h

| Symbol | Used in | ESP-IDF basis |
|---|---|---|
| `millis()`, `micros()` (return `unsigned long`) | everywhere | `esp_timer_get_time()` |
| `delay(ms)` | Board, Gyro, Steppers, Motion, EVBot | `vTaskDelay`. On the MicroPython task it is also the interrupt point (PLAN design point 1). On any other task it only sleeps. |
| `delayMicroseconds(us)` | Steppers `setDirections` | `esp_rom_delay_us` |
| `pinMode(pin, INPUT_PULLUP / OUTPUT)` | Board, Steppers | `gpio_config` |
| `digitalWrite`, `digitalRead`, `HIGH`, `LOW` | Board, Steppers | `gpio_set_level`, `gpio_get_level` |
| `rgbLedWrite(pin, r, g, b)` | Board | `led_strip` (RMT), WS2812 on GPIO 48 |
| `analogReadMilliVolts(pin)` | Board | `adc_oneshot` + `adc_cali` curve fitting. GPIO 13 is **ADC2** channel 2 on the S3. |
| `hw_timer_t`, `timerBegin(hz)`, `timerAttachInterrupt(t, fn)`, `timerAlarm(t, ticks, reload, count)` | Steppers | `gptimer`. `timerAlarm` is called **from the ISR**, so it must be in IRAM and ISR-safe (`gptimer_set_alarm_action`). |
| `ARDUINO_ISR_ATTR` | Steppers | `IRAM_ATTR` |
| `min`, `max`, `constrain` | Steppers, Engine, Motion, EVBot | templates. `std::min`/`std::max` need the same type on both sides, as on arduino-esp32 3.x (a comment in Steppers.cpp relies on this). |
| `PI`, `DEG_TO_RAD` | evbot_config.h, Motion | constants |
| `String`: `String(const char*)`, `String(int / long / unsigned long)`, `String(double, digits)`, `+`, `+=`, `c_str()` | Engine, EVBot | a small class on `std::string`/`snprintf` (WString.h) |
| `Serial.begin(baud)`, `operator bool`, `print(const char*)`, `print(float, digits)`, `println(const char* / String)`, `printf(...)` | Engine, Motion, EVBot | writes to MicroPython stdout (`mp_hal_stdout_tx_strn`). `begin` does nothing and `bool` is always true, so the 2 s USB wait in `EVBot::begin()` ends at once. |
| FreeRTOS: `TaskHandle_t`, `xTaskCreatePinnedToCore`, `vTaskDelay` | Engine | arduino-esp32's Arduino.h pulls these in. The shim includes `freertos/FreeRTOS.h` + `freertos/task.h`. |
| C library: `isfinite`, `ceil`, `sqrt`, `sin`, `fabs`, `vsnprintf`, `INT16_MIN` | Gyro, Motion, EVBot | Arduino.h pulls in `<math.h>`, `<stdio.h>`, `<stdint.h>`. The shim does too. |

## Wire.h

`TwoWire` class with global objects `Wire` (bus 0) and `Wire1` (bus 1):
`begin(sda, scl)`, `setClock(hz)`, `end()`, `beginTransmission(addr)`,
`write(byte)`, `endTransmission(bool stop = true)` (returns 0 = OK),
`requestFrom(addr, (uint8_t)n)` (returns count read), `read()`, `available()`.

The engine always writes a register address with `endTransmission(false)` and
then calls `requestFrom`. The shim can buffer the written bytes and do one
`i2c_master_transmit_receive` at `requestFrom`. Only the sensor task uses
I2C after `begin()`.

## Preferences.h

Only `begin(ns, readOnly)`, `getFloat(key, default)`, `end()` (Engine
`loadSavedTuning`). `getString`/`putString` are used only in `Wireless.cpp`,
which is not compiled. On the Python side, Python code reads the car name and
PIN with `esp32.NVS`.

## ESP-IDF headers the engine already includes directly

`esp_system.h` (`esp_reset_reason`), `esp_attr.h` (`RTC_NOINIT_ATTR`),
`esp_timer.h`, `driver/gpio.h`. These need nothing from the shim.

## internal/Wireless.h (stub in wireless_mp.cpp)

The engine calls `begin()`, `print(text)` and `waitForPageAfterRestart(ms)`.
The header also declares `running()`, `connected()`, `updating()`,
`setIdentity()` and `carName()`. Stub all eight, so a future engine call
links. `print()` does nothing, because `Serial` already reaches the page
through stdout. `carName()` needs `String`.

## Notes for phase 1

- **Tick rate.** ESP-IDF's default `CONFIG_FREERTOS_HZ` is 100, and MicroPython's
  `sdkconfig.base` does not change it. Arduino builds use 1000. At 100 Hz,
  `delay(1)` in the Motion loops becomes 0 or 10 ms, and the sensor task's
  `vTaskDelay(1)` becomes 10 ms. The gyro would then be read about 100 times a
  second instead of about 400. The board's `sdkconfig.board` must set
  `CONFIG_FREERTOS_HZ=1000`.
- **Cores.** MicroPython pins its task to core 1 on dual-core chips. NimBLE
  is also on core 1. `steppers::begin()` (called from the MicroPython task) puts
  the step interrupts on core 1, as the C++ version requires.
- **Sensor task and `delay()`.** `gyro::serviceRezero()` runs on the sensor task
  and calls `delay()`. The shim's `delay()` must check which task called it
  (`xTaskGetCurrentTaskHandle() != mp_main_task_handle` means sleep only).
- **Ctrl-C during the first `begin()`.** The gyro calibration loops don't check
  the lockout, so Ctrl-C during the 7 s calibration takes effect only when
  the calibration ends. That's fine, but the teacher guide should say so.
- **ADC2.** If a student creates `machine.ADC(Pin(13))`, it would try to claim
  the same ADC2 unit. Make the shim's ADC handle the only one, or document it.
