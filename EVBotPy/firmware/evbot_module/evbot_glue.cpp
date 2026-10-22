// The C++ side of the evbot module: one EVBot object, driven from Python.

#include <Arduino.h>

// The quick re-begin (PLAN.md, design point 3) has to reset a few private
// EVBot fields that the C++ library has no public way to reset. EVBot.h is
// not changed for the Python version, so the binding opens them up here.
// The standard headers are already included above, so only EVBot.h is
// affected.
#define private public
#include "EVBot.h"
#undef private

#include <string>

#include "internal/Gyro.h"
#include "internal/Steppers.h"
#include "evbot_glue.h"

namespace {

EVBot robot;
// The engine (timers, sensor task, gyro calibration) starts once per
// power-on and keeps running across MicroPython soft resets. This static
// survives them too, because it is not on the MicroPython heap.
bool engineStarted = false;

}  // namespace

// The library's "Oops!" messages name its C++ commands. Students here write
// Python, so print the Python names instead (turnLeft -> turn_left, ...).
void arduino_shim_rewrite(std::string &text) {
  static const char *const names[][2] = {
    {"setSpeed", "set_speed"},
    {"turnLeft", "turn_left"},
    {"turnRight", "turn_right"},
    {"spinLeft", "spin_left"},
    {"spinRight", "spin_right"},
    {"arcLeft", "arc_left"},
    {"arcRight", "arc_right"},
    {"resetDistance", "reset_distance"},
    {"resetHeading", "reset_heading"},
    {"waitForButton", "wait_for_button"},
    {"buttonPressed", "button_pressed"},
    {"put robot.begin(); at the start of setup() first", "put robot.begin() at the start of your program first"},
  };
  for (const auto &n : names) {
    size_t at;
    while ((at = text.find(n[0])) != std::string::npos) text.replace(at, strlen(n[0]), n[1]);
  }
}

// Called by the shim's delay() when Ctrl-C or STOP is waiting: stop the
// wheels so the engine's move loop ends by itself.
extern "C" void arduino_shim_interrupt_pending(void) {
  if (engineStarted) evbot::steppers::lockout(true);
}

extern "C" {

void evbot_glue_begin(void) {
  if (!engineStarted) {
    robot.begin();  // full start: ~7 s of gyro calibration after power-on
    engineStarted = true;
    return;
  }
  // Quick start for every later program: the engine is already running.
  robot.stop();
  evbot::steppers::lockout(false);  // a Ctrl-C in the last program left it locked
  robot = EVBot();                  // fresh speed, light ownership, home position
  robot.started = true;
  robot.resetHeading();
  robot.resetDistance();
  robot.statusLight(BLUE);
  Serial.println("[EVBot] Ready!");
}

void evbot_glue_clear_interrupt(void) {
  if (engineStarted) evbot::steppers::lockout(false);
}

void evbot_glue_forward(float cm) { robot.forward(cm); }
void evbot_glue_backward(float cm) { robot.backward(cm); }
void evbot_glue_turn_left(float degrees) { robot.turnLeft(degrees); }
void evbot_glue_turn_right(float degrees) { robot.turnRight(degrees); }
void evbot_glue_set_speed(int speed) { robot.setSpeed(speed); }
void evbot_glue_stop(void) { robot.stop(); }
void evbot_glue_light(uint32_t color) { robot.light((EVBotColor)color); }
void evbot_glue_light_rgb(int red, int green, int blue) { robot.light(red, green, blue); }
bool evbot_glue_button_pressed(void) { return robot.buttonPressed(); }
float evbot_glue_heading(void) { return robot.heading(); }
float evbot_glue_distance(void) { return robot.distance(); }
float evbot_glue_battery(void) { return robot.battery(); }

void evbot_glue_drive(float left, float right) { robot.drive(left, right); }
void evbot_glue_spin_left(void) { robot.spinLeft(); }
void evbot_glue_spin_right(void) { robot.spinRight(); }
void evbot_glue_arc_left(float radius_cm, float degrees) { robot.arcLeft(radius_cm, degrees); }
void evbot_glue_arc_right(float radius_cm, float degrees) { robot.arcRight(radius_cm, degrees); }
void evbot_glue_reset_distance(void) { robot.resetDistance(); }
void evbot_glue_reset_heading(void) { robot.resetHeading(); }
void evbot_glue_beep(void) { robot.beep(); }
void evbot_glue_beep_note(float frequency, float seconds) { robot.beep(frequency, seconds); }
void evbot_glue_wait_oops(float seconds) { robot.wait(seconds); }

void evbot_glue_program_end(void) {
  if (engineStarted) robot.stop();
}

}  // extern "C"
