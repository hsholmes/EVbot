#pragma once

// C interface between the Python module tables (modevbot.c) and the C++
// engine (evbot_glue.cpp). There is one robot; every call goes to it.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void evbot_glue_begin(void);
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

// evbot_run's "finally": the program ended, stop the car.
void evbot_glue_program_end(void);

#ifdef __cplusplus
}
#endif
