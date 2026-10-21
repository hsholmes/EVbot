#include "Arduino.h"

#include <stdarg.h>

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/semphr.h"

extern "C" {
#include "py/mphal.h"
#include "py/mpstate.h"
#include "py/mpthread.h"
#include "py/runtime.h"
void machine_bitstream_high_low(gpio_num_t pin, uint32_t *timing_ns, const uint8_t *buf, size_t len);
}

ShimSerial Serial;

// Called by delay() on the MicroPython task when Ctrl-C (or the page's STOP)
// is waiting. The evbot module stops the wheels here, so the engine's move
// loop ends on its own and the exception is raised once the C++ call
// returns - it never jumps through C++ code. Weak: does nothing until the
// module defines it.
extern "C" void __attribute__((weak)) arduino_shim_interrupt_pending(void) {}

// ---------- time ----------

unsigned long millis() { return (unsigned long)(esp_timer_get_time() / 1000ULL); }
unsigned long micros() { return (unsigned long)esp_timer_get_time(); }

void delayMicroseconds(uint32_t us) { esp_rom_delay_us(us); }

void delay(uint32_t ms) {
  if (xTaskGetCurrentTaskHandle() != mp_main_task_handle) {
    vTaskDelay(pdMS_TO_TICKS(ms));  // the sensor task, or anything else: just sleep
    return;
  }
  // The MicroPython task. Let go of the GIL while waiting, so Bluetooth event
  // handlers (which run on the NimBLE task) can run, then check for Ctrl-C.
  MP_THREAD_GIL_EXIT();
  vTaskDelay(pdMS_TO_TICKS(ms));
  MP_THREAD_GIL_ENTER();
  if (MP_STATE_MAIN_THREAD(mp_pending_exception) != MP_OBJ_NULL) {
    arduino_shim_interrupt_pending();
  }
}

// ---------- pins ----------

void pinMode(uint8_t pin, uint8_t mode) {
  gpio_config_t conf = {};
  conf.pin_bit_mask = 1ULL << pin;
  // Outputs can be read back, as in Arduino.
  conf.mode = mode == OUTPUT ? GPIO_MODE_INPUT_OUTPUT : GPIO_MODE_INPUT;
  conf.pull_up_en = mode == INPUT_PULLUP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  conf.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&conf);
}

void digitalWrite(uint8_t pin, uint8_t val) { gpio_set_level((gpio_num_t)pin, val); }

int digitalRead(uint8_t pin) { return gpio_get_level((gpio_num_t)pin); }

// WS2812 colour LED, through MicroPython's own bitstream code (RMT), with the
// timings MicroPython's neopixel module uses. The LED takes green first.
void rgbLedWrite(uint8_t pin, uint8_t red, uint8_t green, uint8_t blue) {
  static uint32_t timing_ns[4] = {400, 850, 800, 450};
  uint8_t grb[3] = {green, red, blue};
  gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
  machine_bitstream_high_low((gpio_num_t)pin, timing_ns, grb, sizeof(grb));
}

// Battery sense. Like Arduino: 12 dB attenuation (full range) and the chip's
// factory calibration. The sensor task and the program both read it, so a
// mutex keeps the reads apart.
uint32_t analogReadMilliVolts(uint8_t pin) {
  struct Channel {
    int pin = -1;
    adc_oneshot_unit_handle_t unit = nullptr;
    adc_channel_t channel;
    adc_cali_handle_t cali = nullptr;
  };
  static Channel ch;
  static SemaphoreHandle_t lock = xSemaphoreCreateMutex();

  xSemaphoreTake(lock, portMAX_DELAY);
  if (ch.pin != pin) {
    adc_unit_t unitId;
    if (adc_oneshot_io_to_channel(pin, &unitId, &ch.channel) != ESP_OK) {
      xSemaphoreGive(lock);
      return 0;
    }
    adc_oneshot_unit_init_cfg_t unitCfg = {};
    unitCfg.unit_id = unitId;
    if (ch.unit == nullptr && adc_oneshot_new_unit(&unitCfg, &ch.unit) != ESP_OK) {
      ch.unit = nullptr;
      xSemaphoreGive(lock);
      return 0;
    }
    adc_oneshot_chan_cfg_t chanCfg = {};
    chanCfg.atten = ADC_ATTEN_DB_12;
    chanCfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(ch.unit, ch.channel, &chanCfg);
    adc_cali_curve_fitting_config_t caliCfg = {};
    caliCfg.unit_id = unitId;
    caliCfg.chan = ch.channel;
    caliCfg.atten = ADC_ATTEN_DB_12;
    caliCfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&caliCfg, &ch.cali) != ESP_OK) ch.cali = nullptr;
    ch.pin = pin;
  }
  int raw = 0, mv = 0;
  if (adc_oneshot_read(ch.unit, ch.channel, &raw) == ESP_OK) {
    if (ch.cali == nullptr || adc_cali_raw_to_voltage(ch.cali, raw, &mv) != ESP_OK) {
      mv = raw * 3100 / 4095;  // no calibration in eFuse: rough full-range guess
    }
  }
  xSemaphoreGive(lock);
  return mv;
}

// ---------- hardware timers ----------

struct hw_timer_s {
  gptimer_handle_t handle;
  void (*userFunc)(void);
};

static bool IRAM_ATTR timerOnAlarm(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *ctx) {
  hw_timer_t *timer = (hw_timer_t *)ctx;
  if (timer->userFunc) timer->userFunc();
  return false;  // no higher-priority task woken
}

hw_timer_t *timerBegin(uint32_t frequency) {
  hw_timer_t *timer = (hw_timer_t *)heap_caps_calloc(1, sizeof(hw_timer_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (timer == nullptr) return nullptr;
  gptimer_config_t cfg = {};
  cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  cfg.direction = GPTIMER_COUNT_UP;
  cfg.resolution_hz = frequency;
  if (gptimer_new_timer(&cfg, &timer->handle) != ESP_OK) {
    heap_caps_free(timer);
    return nullptr;
  }
  return timer;
}

// The interrupt is set up on the core that calls this. EVBot calls it from
// the MicroPython task, which runs on core 1 - as the C++ version needs.
void timerAttachInterrupt(hw_timer_t *timer, void (*userFunc)(void)) {
  if (timer == nullptr) return;
  timer->userFunc = userFunc;
  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = timerOnAlarm;
  gptimer_register_event_callbacks(timer->handle, &cbs, timer);
  gptimer_enable(timer->handle);
  gptimer_start(timer->handle);
}

void IRAM_ATTR timerAlarm(hw_timer_t *timer, uint64_t alarm_value, bool autoreload, uint64_t reload_count) {
  gptimer_alarm_config_t alarm = {};
  alarm.alarm_count = alarm_value;
  alarm.reload_count = reload_count;
  alarm.flags.auto_reload_on_alarm = autoreload;
  gptimer_set_alarm_action(timer->handle, &alarm);
}

// ---------- Serial ----------

size_t ShimSerial::print(const char *text) {
  size_t len = strlen(text);
  mp_hal_stdout_tx_strn_cooked(text, len);
  return len;
}

size_t ShimSerial::print(char c) { return print(String(c).c_str()); }
size_t ShimSerial::print(int n) { return print(String(n).c_str()); }
size_t ShimSerial::print(long n) { return print(String(n).c_str()); }
size_t ShimSerial::print(unsigned long n) { return print(String(n).c_str()); }
size_t ShimSerial::print(double n, int digits) { return print(String(n, digits).c_str()); }
size_t ShimSerial::println() { return print("\n"); }

size_t ShimSerial::printf(const char *format, ...) {
  char buf[256];
  va_list args;
  va_start(args, format);
  int n = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  print(buf);
  return n < 0 ? 0 : (size_t)n;
}
