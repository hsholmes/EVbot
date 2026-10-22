#pragma once

// A small stand-in for the arduino-esp32 core, written on plain ESP-IDF, so
// the EVBot engine in src/ compiles unchanged inside MicroPython. It has only
// what the engine uses; SYMBOLS.md lists it. If a future engine change needs
// more, the build fails naming the missing function - add it here.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"  // xTaskCreatePinnedToCore
#include "esp_attr.h"
#include "esp_system.h"

#ifdef __cplusplus
#include <algorithm>
#include <cmath>
using std::isfinite;
using std::isinf;
using std::isnan;
using std::max;
using std::min;
#endif

#define HIGH 0x1
#define LOW 0x0

#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05

#define PI 3.1415926535897932384626433832795
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

// Same definition as arduino-esp32: a macro, so mixed argument types work.
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

#define ARDUINO_ISR_ATTR IRAM_ATTR

// ---- time ----
unsigned long millis();
unsigned long micros();
// On the MicroPython task this also lets Bluetooth and Ctrl-C through (see
// Arduino.cpp). Anywhere else it only sleeps.
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);

// ---- pins ----
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
uint32_t analogReadMilliVolts(uint8_t pin);
void rgbLedWrite(uint8_t pin, uint8_t red, uint8_t green, uint8_t blue);

// ---- hardware timers (gptimer) ----
struct hw_timer_s;
typedef struct hw_timer_s hw_timer_t;
hw_timer_t *timerBegin(uint32_t frequency);
void timerAttachInterrupt(hw_timer_t *timer, void (*userFunc)(void));
// Safe to call from the timer's own interrupt (the engine re-arms from there).
void timerAlarm(hw_timer_t *timer, uint64_t alarm_value, bool autoreload, uint64_t reload_count);

#ifdef __cplusplus
#include "WString.h"

// Serial writes to MicroPython's stdout: USB, and the Bluetooth terminal once
// the BLE service attaches it.
class ShimSerial {
 public:
  void begin(unsigned long baud) { (void)baud; }
  operator bool() const { return true; }  // no "wait for the Serial Monitor"
  size_t print(const char *text);
  size_t print(const String &text) { return print(text.c_str()); }
  size_t print(char c);
  size_t print(int n);
  size_t print(long n);
  size_t print(unsigned long n);
  size_t print(double n, int digits = 2);
  size_t println();
  template <typename T>
  size_t println(T value) { size_t n = print(value); return n + println(); }
  size_t println(double n, int digits) { size_t r = print(n, digits); return r + println(); }
  size_t printf(const char *format, ...) __attribute__((format(printf, 2, 3)));
};

extern ShimSerial Serial;
#endif
