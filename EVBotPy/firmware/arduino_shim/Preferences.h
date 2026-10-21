#pragma once

// Arduino's Preferences (NVS), read side only: the engine reads the
// competition firmware's calibration. Arduino stores floats as 4-byte blobs.

#include "Arduino.h"
#include "nvs.h"

class Preferences {
 public:
  ~Preferences() { end(); }

  bool begin(const char *name, bool readOnly = false) {
    end();
    return nvs_open(name, readOnly ? NVS_READONLY : NVS_READWRITE, &handle_) == ESP_OK && (open_ = true);
  }

  void end() {
    if (open_) nvs_close(handle_);
    open_ = false;
  }

  float getFloat(const char *key, float defaultValue = NAN) {
    float value;
    size_t len = sizeof(value);
    if (!open_ || nvs_get_blob(handle_, key, &value, &len) != ESP_OK || len != sizeof(value)) return defaultValue;
    return value;
  }

 private:
  nvs_handle_t handle_ = 0;
  bool open_ = false;
};
