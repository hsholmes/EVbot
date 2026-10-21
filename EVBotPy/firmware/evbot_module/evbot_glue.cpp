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

void evbot_glue_program_end(void) {
  if (engineStarted) robot.stop();
}

}  // extern "C"
