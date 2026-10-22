#pragma once

// C interface between the Python module tables (modevbot.c) and the C++
// engine (evbot_glue.cpp). There is one robot; every call goes to it.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void evbot_glue_begin(void);

// A Ctrl-C (or STOP) locks the wheels out so the C++ move that was running
// ends. Every new move calls this first: the interrupted one is over.
void evbot_glue_clear_interrupt(void);

void evbot_glue_forward(float cm);
void evbot_glue_backward(float cm);
void evbot_glue_turn_left(float degrees);
void evbot_glue_turn_right(float degrees);
void evbot_glue_set_speed(int speed);
void evbot_glue_stop(void);
void evbot_glue_light(uint32_t color);
void evbot_glue_light_rgb(int red, int green, int blue);
bool evbot_glue_button_pressed(void);
float evbot_glue_heading(void);
float evbot_glue_distance(void);
float evbot_glue_battery(void);

void evbot_glue_drive(float left, float right);
void evbot_glue_spin_left(void);
void evbot_glue_spin_right(void);
void evbot_glue_arc_left(float radius_cm, float degrees);
void evbot_glue_arc_right(float radius_cm, float degrees);
void evbot_glue_reset_distance(void);
void evbot_glue_reset_heading(void);
void evbot_glue_beep(void);
void evbot_glue_beep_note(float frequency, float seconds);

// wait() with a negative time: let the C++ library say "Oops!" as usual.
// (Real waits are done by the module with mp_hal_delay_ms.)
void evbot_glue_wait_oops(float seconds);

// evbot_run's "finally": the program ended, stop the car.
void evbot_glue_program_end(void);

#ifdef __cplusplus
}
#endif
