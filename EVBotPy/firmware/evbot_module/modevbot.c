// The evbot Python module:
//
//   from evbot import *
//   robot = EVBot()
//   robot.begin()
//   robot.forward(50)
//
// The same commands as the C++ library, in Python naming style (PLAN.md,
// "Python API"). Bad *values* behave as in C++: "Oops! ..." and a red light,
// and the program carries on. Wrong *types* or a wrong number of values
// raise a short TypeError in plain words.

#include "py/mphal.h"
#include "py/runtime.h"
#include "extmod/modbluetooth.h"
#include "shared/runtime/interrupt_char.h"
#include "nvs.h"
#include "host/ble_att.h"
#include "evbot_glue.h"

// ---------- helpers ----------

// Moves block in C++. Ctrl-C during one stops the wheels (see the shim's
// delay()); raise the KeyboardInterrupt here, once C++ has returned.
static void raise_if_interrupted(void) {
    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
}

// n_args counts self, so robot.forward(20) has n_args == 2.
static void check_count(size_t n_args, size_t lo, size_t hi, const char *example) {
    if (n_args - 1 < lo || n_args - 1 > hi) {
        mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("use it like this: robot.%s"), example);
    }
}

static mp_float_t number(mp_obj_t o, const char *example) {
    if (!mp_obj_is_int(o) && !mp_obj_is_float(o)) {
        mp_raise_msg_varg(&mp_type_TypeError,
            MP_ERROR_TEXT("that needs a number, like robot.%s (you gave a %s)"), example, mp_obj_get_type_str(o));
    }
    return mp_obj_get_float(o);
}

static mp_int_t whole_number(mp_obj_t o, const char *example) {
    if (!mp_obj_is_int(o)) {
        mp_raise_msg_varg(&mp_type_TypeError,
            MP_ERROR_TEXT("that needs a whole number, like robot.%s (you gave a %s)"), example, mp_obj_get_type_str(o));
    }
    return mp_obj_get_int(o);
}

#define METHOD(name) static MP_DEFINE_CONST_FUN_OBJ_VAR(evbot_##name##_obj, 1, evbot_##name)

// ---------- the EVBot type: EVBot() always returns the same robot ----------

typedef struct {
    mp_obj_base_t base;
} evbot_obj_t;

extern const mp_obj_type_t evbot_type;
static const evbot_obj_t evbot_singleton = {{&evbot_type}};

static mp_obj_t evbot_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    if (n_args != 0 || n_kw != 0) {
        mp_raise_TypeError(MP_ERROR_TEXT("use it like this: robot = EVBot()"));
    }
    return MP_OBJ_FROM_PTR(&evbot_singleton);
}

// ---------- getting started ----------

static mp_obj_t evbot_begin(size_t n_args, const mp_obj_t *args) {
    check_count(n_args, 0, 0, "begin()");
    evbot_glue_begin(mp_bluetooth_is_active());
    raise_if_interrupted();
    return mp_const_none;
}
METHOD(begin);

// ---------- level 1: moving ----------

#define MOVE(name, example)                                          \
    static mp_obj_t evbot_##name(size_t n_args, const mp_obj_t *args) { \
        check_count(n_args, 1, 1, example);                          \
        mp_float_t amount = number(args[1], example);                \
        evbot_glue_clear_interrupt();                                \
        evbot_glue_##name(amount);                                   \
        raise_if_interrupted();                                      \
        return mp_const_none;                                        \
    }                                                                \
    METHOD(name);

MOVE(forward, "forward(20)")
MOVE(backward, "backward(20)")
MOVE(turn_left, "turn_left(90)")
MOVE(turn_right, "turn_right(90)")

static mp_obj_t evbot_set_speed(size_t n_args, const mp_obj_t *args) {
    check_count(n_args, 1, 1, "set_speed(5)");
    evbot_glue_set_speed(whole_number(args[1], "set_speed(5)"));
    return mp_const_none;
}
METHOD(set_speed);

// Not the C++ wait(): MicroPython's own delay lets Ctrl-C, Bluetooth and
// scheduled callbacks through while it waits.
static mp_obj_t evbot_wait(size_t n_args, const mp_obj_t *args) {
    check_count(n_args, 1, 1, "wait(0.5)");
    mp_float_t seconds = number(args[1], "wait(0.5)");
    if (seconds < 0) {
        evbot_glue_wait_oops(seconds);
        return mp_const_none;
    }
    mp_hal_delay_ms((mp_uint_t)(seconds * 1000));
    return mp_const_none;
}
METHOD(wait);

static mp_obj_t evbot_stop(size_t n_args, const mp_obj_t *args) {
    check_count(n_args, 0, 0, "stop()");
    evbot_glue_stop();
    raise_if_interrupted();
    return mp_const_none;
}
METHOD(stop);

// ---------- level 2: senses ----------

// light(color) or light(red, green, blue)
static mp_obj_t evbot_light(size_t n_args, const mp_obj_t *args) {
    if (n_args == 2) {
        evbot_glue_light((uint32_t)whole_number(args[1], "light(GREEN)"));
    } else if (n_args == 4) {
        evbot_glue_light_rgb(whole_number(args[1], "light(255, 0, 255)"),
            whole_number(args[2], "light(255, 0, 255)"),
            whole_number(args[3], "light(255, 0, 255)"));
    } else {
        mp_raise_TypeError(MP_ERROR_TEXT("use it like this: robot.light(GREEN) or robot.light(255, 0, 255)"));
    }
    return mp_const_none;
}
METHOD(light);

static mp_obj_t evbot_button_pressed(size_t n_args, const mp_obj_t *args) {
    check_count(n_args, 0, 0, "button_pressed()");
    return mp_obj_new_bool(evbot_glue_button_pressed());
}
METHOD(button_pressed);

// Not the C++ waitForButton(): its loop would never end once Ctrl-C has
// locked the wheels. mp_hal_delay_ms raises the KeyboardInterrupt instead.
static mp_obj_t evbot_wait_for_button(size_t n_args, const mp_obj_t *args) {
    check_count(n_args, 0, 0, "wait_for_button()");
    mp_printf(&mp_plat_print, "[EVBot] Press the START button...\n");
    while (!evbot_glue_button_pressed()) {
        mp_hal_delay_ms(10);
    }
    while (evbot_glue_button_pressed()) {  // wait for release, so the press doesn't bump the car mid-move
        mp_hal_delay_ms(10);
    }
    mp_hal_delay_ms(300);
    return mp_const_none;
}
METHOD(wait_for_button);

#define READING(name)                                                \
    static mp_obj_t evbot_##name(size_t n_args, const mp_obj_t *args) { \
        check_count(n_args, 0, 0, #name "()");                       \
        return mp_obj_new_float(evbot_glue_##name());                \
    }                                                                \
    METHOD(name);

READING(heading)
READING(distance)
READING(battery)

// say(anything), or say(label, number) -> "label: 12.34". Decimals are shown
// with 2 places, as in C++. print() works too.
static void say_value(const mp_print_t *print, mp_obj_t o) {
    if (mp_obj_is_float(o)) {
        mp_printf(print, "%.2f", (double)mp_obj_get_float(o));
    } else {
        mp_obj_print_helper(print, o, PRINT_STR);
    }
}

static mp_obj_t evbot_say(size_t n_args, const mp_obj_t *args) {
    if (n_args == 3 && mp_obj_is_str(args[1]) && (mp_obj_is_int(args[2]) || mp_obj_is_float(args[2]))) {
        mp_obj_print_helper(&mp_plat_print, args[1], PRINT_STR);
        mp_printf(&mp_plat_print, ": %.2f\n", (double)mp_obj_get_float(args[2]));
        return mp_const_none;
    }
    for (size_t i = 1; i < n_args; i++) {
        if (i > 1) {
            mp_print_str(&mp_plat_print, " ");
        }
        say_value(&mp_plat_print, args[i]);
    }
    mp_print_str(&mp_plat_print, "\n");
    return mp_const_none;
}
METHOD(say);

// ---------- level 3: explorer ----------

static mp_obj_t evbot_drive(size_t n_args, const mp_obj_t *args) {
    check_count(n_args, 2, 2, "drive(5, 5)");
    mp_float_t left = number(args[1], "drive(5, 5)");
    mp_float_t right = number(args[2], "drive(5, 5)");
    evbot_glue_clear_interrupt();
    evbot_glue_drive(left, right);
    raise_if_interrupted();
    return mp_const_none;
}
METHOD(drive);

#define NO_ARGS(name, example)                                       \
    static mp_obj_t evbot_##name(size_t n_args, const mp_obj_t *args) { \
        check_count(n_args, 0, 0, example);                          \
        evbot_glue_clear_interrupt();                                \
        evbot_glue_##name();                                         \
        raise_if_interrupted();                                      \
        return mp_const_none;                                        \
    }                                                                \
    METHOD(name);

NO_ARGS(spin_left, "spin_left()")
NO_ARGS(spin_right, "spin_right()")

#define ARC(name, example)                                           \
    static mp_obj_t evbot_##name(size_t n_args, const mp_obj_t *args) { \
        check_count(n_args, 2, 2, example);                          \
        mp_float_t radius = number(args[1], example);                \
        mp_float_t degrees = number(args[2], example);               \
        evbot_glue_clear_interrupt();                                \
        evbot_glue_##name(radius, degrees);                          \
        raise_if_interrupted();                                      \
        return mp_const_none;                                        \
    }                                                                \
    METHOD(name);

ARC(arc_left, "arc_left(30, 90)")
ARC(arc_right, "arc_right(30, 90)")

#define RESET(name)                                                  \
    static mp_obj_t evbot_##name(size_t n_args, const mp_obj_t *args) { \
        check_count(n_args, 0, 0, #name "()");                       \
        evbot_glue_##name();                                         \
        return mp_const_none;                                        \
    }                                                                \
    METHOD(name);

RESET(reset_distance)
RESET(reset_heading)

// beep() or beep(frequency, seconds)
static mp_obj_t evbot_beep(size_t n_args, const mp_obj_t *args) {
    if (n_args != 1 && n_args != 3) {
        mp_raise_TypeError(MP_ERROR_TEXT("use it like this: robot.beep() or robot.beep(440, 0.5)"));
    }
    evbot_glue_clear_interrupt();
    if (n_args == 1) {
        evbot_glue_beep();
    } else {
        evbot_glue_beep_note(number(args[1], "beep(440, 0.5)"), number(args[2], "beep(440, 0.5)"));
    }
    raise_if_interrupted();
    return mp_const_none;
}
METHOD(beep);

static const mp_rom_map_elem_t evbot_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_begin), MP_ROM_PTR(&evbot_begin_obj) },
    // level 1
    { MP_ROM_QSTR(MP_QSTR_forward), MP_ROM_PTR(&evbot_forward_obj) },
    { MP_ROM_QSTR(MP_QSTR_backward), MP_ROM_PTR(&evbot_backward_obj) },
    { MP_ROM_QSTR(MP_QSTR_turn_left), MP_ROM_PTR(&evbot_turn_left_obj) },
    { MP_ROM_QSTR(MP_QSTR_turn_right), MP_ROM_PTR(&evbot_turn_right_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_speed), MP_ROM_PTR(&evbot_set_speed_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait), MP_ROM_PTR(&evbot_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&evbot_stop_obj) },
    // level 2
    { MP_ROM_QSTR(MP_QSTR_light), MP_ROM_PTR(&evbot_light_obj) },
    { MP_ROM_QSTR(MP_QSTR_button_pressed), MP_ROM_PTR(&evbot_button_pressed_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait_for_button), MP_ROM_PTR(&evbot_wait_for_button_obj) },
    { MP_ROM_QSTR(MP_QSTR_heading), MP_ROM_PTR(&evbot_heading_obj) },
    { MP_ROM_QSTR(MP_QSTR_distance), MP_ROM_PTR(&evbot_distance_obj) },
    { MP_ROM_QSTR(MP_QSTR_battery), MP_ROM_PTR(&evbot_battery_obj) },
    { MP_ROM_QSTR(MP_QSTR_say), MP_ROM_PTR(&evbot_say_obj) },
    // level 3
    { MP_ROM_QSTR(MP_QSTR_drive), MP_ROM_PTR(&evbot_drive_obj) },
    { MP_ROM_QSTR(MP_QSTR_spin_left), MP_ROM_PTR(&evbot_spin_left_obj) },
    { MP_ROM_QSTR(MP_QSTR_spin_right), MP_ROM_PTR(&evbot_spin_right_obj) },
    { MP_ROM_QSTR(MP_QSTR_arc_left), MP_ROM_PTR(&evbot_arc_left_obj) },
    { MP_ROM_QSTR(MP_QSTR_arc_right), MP_ROM_PTR(&evbot_arc_right_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset_distance), MP_ROM_PTR(&evbot_reset_distance_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset_heading), MP_ROM_PTR(&evbot_reset_heading_obj) },
    { MP_ROM_QSTR(MP_QSTR_beep), MP_ROM_PTR(&evbot_beep_obj) },
};
static MP_DEFINE_CONST_DICT(evbot_locals_dict, evbot_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    evbot_type,
    MP_QSTR_EVBot,
    MP_TYPE_FLAG_NONE,
    make_new, evbot_make_new,
    locals_dict, &evbot_locals_dict
    );

// ---------- module ----------

static mp_obj_t evbot_program_end(void) {
    evbot_glue_program_end();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(evbot_program_end_obj, evbot_program_end);

// For evbot_ble (the page's STOP button, and Ctrl-C over Bluetooth). Safe
// from the Bluetooth task. Locks the wheels out and, if Python code is
// running, interrupts it - the same as Ctrl-C on USB. Returns True if it
// interrupted running code: then a Ctrl-C byte has been dealt with and must
// not also be typed into the prompt.
static mp_obj_t evbot_emergency_stop(void) {
    evbot_glue_emergency_stop();
    if (mp_interrupt_char == -1) {
        return mp_const_false;  // nothing running: the prompt is waiting for input
    }
    mp_sched_keyboard_interrupt();
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_0(evbot_emergency_stop_obj, evbot_emergency_stop);

// Uploads are refused while the wheels turn: writing flash pauses the step
// interrupts (PLAN.md, design point 7).
static mp_obj_t evbot_wheels_turning(void) {
    return mp_obj_new_bool(evbot_glue_wheels_turning());
}
static MP_DEFINE_CONST_FUN_OBJ_0(evbot_wheels_turning_obj, evbot_wheels_turning);

// The connection's real ATT MTU. MicroPython only reports it in an event,
// which doesn't always arrive (the C++ firmware saw the same on Windows), so
// ask NimBLE each time, as the C++ version does.
static mp_obj_t evbot_ble_mtu(mp_obj_t conn_in) {
    uint16_t mtu = ble_att_mtu((uint16_t)mp_obj_get_int(conn_in));
    return MP_OBJ_NEW_SMALL_INT(mtu ? mtu : 23);
}
static MP_DEFINE_CONST_FUN_OBJ_1(evbot_ble_mtu_obj, evbot_ble_mtu);

// Let other tasks run for ms milliseconds, without running scheduled code or
// raising a pending Ctrl-C (time.sleep_ms does both). evbot_ble waits like
// this for Bluetooth buffers inside the terminal's write(), where an
// exception would make MicroPython detach the terminal.
static mp_obj_t evbot_idle_ms(mp_obj_t ms_in) {
    MP_THREAD_GIL_EXIT();
    vTaskDelay(pdMS_TO_TICKS(mp_obj_get_int(ms_in)));
    MP_THREAD_GIL_ENTER();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(evbot_idle_ms_obj, evbot_idle_ms);

// The C++ library stores the car's name and PIN as NVS strings, which
// MicroPython's esp32.NVS can't read (it has only int and blob getters).
// _nvs_str(namespace, key) -> str, or None if it isn't there.
static mp_obj_t evbot_nvs_str(mp_obj_t ns_in, mp_obj_t key_in) {
    nvs_handle_t handle;
    if (nvs_open(mp_obj_str_get_str(ns_in), NVS_READONLY, &handle) != ESP_OK) {
        return mp_const_none;
    }
    char buf[64];
    size_t len = sizeof(buf);
    esp_err_t err = nvs_get_str(handle, mp_obj_str_get_str(key_in), buf, &len);
    nvs_close(handle);
    if (err != ESP_OK || len == 0) {
        return mp_const_none;
    }
    return mp_obj_new_str(buf, len - 1);  // len counts the trailing 0
}
static MP_DEFINE_CONST_FUN_OBJ_2(evbot_nvs_str_obj, evbot_nvs_str);

// _nvs_set_str(namespace, key, value), stored the way the C++ library reads it.
static mp_obj_t evbot_nvs_set_str(mp_obj_t ns_in, mp_obj_t key_in, mp_obj_t value_in) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(mp_obj_str_get_str(ns_in), NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, mp_obj_str_get_str(key_in), mp_obj_str_get_str(value_in));
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        mp_raise_OSError(err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(evbot_nvs_set_str_obj, evbot_nvs_set_str);

static const mp_rom_map_elem_t evbot_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_evbot) },
    { MP_ROM_QSTR(MP_QSTR_EVBot), MP_ROM_PTR(&evbot_type) },
    { MP_ROM_QSTR(MP_QSTR__program_end), MP_ROM_PTR(&evbot_program_end_obj) },
    { MP_ROM_QSTR(MP_QSTR__emergency_stop), MP_ROM_PTR(&evbot_emergency_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR__wheels_turning), MP_ROM_PTR(&evbot_wheels_turning_obj) },
    { MP_ROM_QSTR(MP_QSTR__ble_mtu), MP_ROM_PTR(&evbot_ble_mtu_obj) },
    { MP_ROM_QSTR(MP_QSTR__idle_ms), MP_ROM_PTR(&evbot_idle_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR__nvs_str), MP_ROM_PTR(&evbot_nvs_str_obj) },
    { MP_ROM_QSTR(MP_QSTR__nvs_set_str), MP_ROM_PTR(&evbot_nvs_set_str_obj) },

    // Speeds for set_speed() (any number 1-10 works too)
    { MP_ROM_QSTR(MP_QSTR_SLOW), MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_MEDIUM), MP_ROM_INT(5) },
    { MP_ROM_QSTR(MP_QSTR_FAST), MP_ROM_INT(8) },

    // Colors for light() - the same values as EVBot.h
    { MP_ROM_QSTR(MP_QSTR_OFF), MP_ROM_INT(0x000000) },
    { MP_ROM_QSTR(MP_QSTR_RED), MP_ROM_INT(0xFF0000) },
    { MP_ROM_QSTR(MP_QSTR_ORANGE), MP_ROM_INT(0xFF6000) },
    { MP_ROM_QSTR(MP_QSTR_YELLOW), MP_ROM_INT(0xFFC000) },
    { MP_ROM_QSTR(MP_QSTR_GREEN), MP_ROM_INT(0x00FF00) },
    { MP_ROM_QSTR(MP_QSTR_CYAN), MP_ROM_INT(0x00FFFF) },
    { MP_ROM_QSTR(MP_QSTR_BLUE), MP_ROM_INT(0x0000FF) },
    { MP_ROM_QSTR(MP_QSTR_PURPLE), MP_ROM_INT(0x8000FF) },
    { MP_ROM_QSTR(MP_QSTR_PINK), MP_ROM_INT(0xFF40A0) },
    { MP_ROM_QSTR(MP_QSTR_WHITE), MP_ROM_INT(0xFFFFFF) },
};
static MP_DEFINE_CONST_DICT(evbot_module_globals, evbot_module_globals_table);

const mp_obj_module_t evbot_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&evbot_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_evbot, evbot_user_cmodule);
