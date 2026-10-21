#pragma once

// Arduino's Wire (I2C master) on ESP-IDF's i2c_master driver - the same
// driver MicroPython's machine.I2C uses, so the two can share the firmware.
// Wire is I2C port 0, Wire1 is port 1. A student's machine.I2C(0) or (1)
// would find its port taken by the engine.
//
// The engine always writes a register address with endTransmission(false)
// and then calls requestFrom(). Those two become one write-then-read
// transaction, as in arduino-esp32.

#include "Arduino.h"
#include "driver/i2c_master.h"

class TwoWire {
 public:
  explicit TwoWire(int port) : port_(port) {}

  bool begin(int sda, int scl);
  void end();
  void setClock(uint32_t hz);

  void beginTransmission(uint8_t address);
  size_t write(uint8_t value);
  uint8_t endTransmission(bool sendStop = true);
  uint8_t requestFrom(uint8_t address, uint8_t quantity);
  int available();
  int read();

 private:
  static const int MAX_DEVICES = 4;
  static const size_t BUF_SIZE = 32;

  i2c_master_dev_handle_t device(uint8_t address);
  void removeDevices();

  int port_;
  uint32_t clockHz_ = 100000;
  i2c_master_bus_handle_t bus_ = nullptr;
  struct {
    uint8_t address;
    i2c_master_dev_handle_t handle;
  } devices_[MAX_DEVICES] = {};
  int deviceCount_ = 0;

  uint8_t txAddress_ = 0;
  uint8_t txBuf_[BUF_SIZE];
  size_t txLen_ = 0;
  bool txPending_ = false;  // written with endTransmission(false), not sent yet

  uint8_t rxBuf_[BUF_SIZE];
  size_t rxLen_ = 0;
  size_t rxPos_ = 0;
};

extern TwoWire Wire;
extern TwoWire Wire1;
