// The evbot Python module:
//
//   from evbot import *
//   robot = EVBot()
//   robot.begin()
//   robot.forward(50)
//
// Phase 1: begin, moves, light and a few readings. Phase 2 adds the rest
// (PLAN.md, "Python API").

#include "py/runtime.h"
#include "evbot_glue.h"

// Moves block in C++. Ctrl-C during one stops the wheels (see the shim's
// delay()); raise the KeyboardInterrupt here, once C++ has returned.
static void raise_if_interrupted(void) {
    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
}

// ---------- the EVBot type: EVBot() always returns the same robot ----------

typedef struct {
    mp_obj_base_t base;
} evbot_obj_t;

extern const mp_obj_type_t evbot_type;
static const evbot_obj_t evbot_singleton = {{&evbot_type}};

static mp_obj_t evbot_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    return MP_OBJ_FROM_PTR(&evbot_singleton);
}

static mp_obj_t evbot_begin(mp_obj_t self) {
    evbot_glue_begin();
    raise_if_interrupted();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(evbot_begin_obj, evbot_begin);

#define EVBOT_MOVE(name)                                            \
    static mp_obj_t evbot_##name(mp_obj_t self, mp_obj_t amount) {  \
        evbot_glue_##name(mp_obj_get_float(amount));                \
        raise_if_interrupted();                                     \
        return mp_const_none;                                       \
    }                                                               \
    static MP_DEFINE_CONST_FUN_OBJ_2(evbot_##name##_obj, evbot_##name);

EVBOT_MOVE(forward)
EVBOT_MOVE(backward)
EVBOT_MOVE(turn_left)
EVBOT_MOVE(turn_right)

static mp_obj_t evbot_set_speed(mp_obj_t self, mp_obj_t speed) {
    evbot_glue_set_speed(mp_obj_get_int(speed));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(evbot_set_speed_obj, evbot_set_speed);

static mp_obj_t evbot_stop(mp_obj_t self) {
    evbot_glue_stop();
    raise_if_interrupted();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(evbot_stop_obj, evbot_stop);

// light(color) or light(red, green, blue)
static mp_obj_t evbot_light(size_t n_args, const mp_obj_t *args) {
    if (n_args == 2) {
        evbot_glue_light((uint32_t)mp_obj_get_int(args[1]));
    } else if (n_args == 4) {
        evbot_glue_light_rgb(mp_obj_get_int(args[1]), mp_obj_get_int(args[2]), mp_obj_get_int(args[3]));
    } else {
        mp_raise_TypeError(MP_ERROR_TEXT("light() needs a color, like light(GREEN), or red, green, blue"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(evbot_light_obj, 2, 4, evbot_light);

static mp_obj_t evbot_button_pressed(mp_obj_t self) {
    return mp_obj_new_bool(evbot_glue_button_pressed());
}
static MP_DEFINE_CONST_FUN_OBJ_1(evbot_button_pressed_obj, evbot_button_pressed);

static mp_obj_t evbot_heading(mp_obj_t self) {
    return mp_obj_new_float(evbot_glue_heading());
}
static MP_DEFINE_CONST_FUN_OBJ_1(evbot_heading_obj, evbot_heading);

static mp_obj_t evbot_distance(mp_obj_t self) {
    return mp_obj_new_float(evbot_glue_distance());
}
static MP_DEFINE_CONST_FUN_OBJ_1(evbot_distance_obj, evbot_distance);

static mp_obj_t evbot_battery(mp_obj_t self) {
    return mp_obj_new_float(evbot_glue_battery());
}
static MP_DEFINE_CONST_FUN_OBJ_1(evbot_battery_obj, evbot_battery);

static const mp_rom_map_elem_t evbot_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_begin), MP_ROM_PTR(&evbot_begin_obj) },
    { MP_ROM_QSTR(MP_QSTR_forward), MP_ROM_PTR(&evbot_forward_obj) },
    { MP_ROM_QSTR(MP_QSTR_backward), MP_ROM_PTR(&evbot_backward_obj) },
    { MP_ROM_QSTR(MP_QSTR_turn_left), MP_ROM_PTR(&evbot_turn_left_obj) },
    { MP_ROM_QSTR(MP_QSTR_turn_right), MP_ROM_PTR(&evbot_turn_right_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_speed), MP_ROM_PTR(&evbot_set_speed_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&evbot_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_light), MP_ROM_PTR(&evbot_light_obj) },
    { MP_ROM_QSTR(MP_QSTR_button_pressed), MP_ROM_PTR(&evbot_button_pressed_obj) },
    { MP_ROM_QSTR(MP_QSTR_heading), MP_ROM_PTR(&evbot_heading_obj) },
    { MP_ROM_QSTR(MP_QSTR_distance), MP_ROM_PTR(&evbot_distance_obj) },
    { MP_ROM_QSTR(MP_QSTR_battery), MP_ROM_PTR(&evbot_battery_obj) },
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

static const mp_rom_map_elem_t evbot_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_evbot) },
    { MP_ROM_QSTR(MP_QSTR_EVBot), MP_ROM_PTR(&evbot_type) },
    { MP_ROM_QSTR(MP_QSTR__program_end), MP_ROM_PTR(&evbot_program_end_obj) },

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
