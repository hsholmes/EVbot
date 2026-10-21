#include "Wire.h"

TwoWire Wire(0);
TwoWire Wire1(1);

namespace {
const int TIMEOUT_MS = 20;
}

bool TwoWire::begin(int sda, int scl) {
  if (bus_ != nullptr) return true;
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = (i2c_port_num_t)port_;
  cfg.sda_io_num = (gpio_num_t)sda;
  cfg.scl_io_num = (gpio_num_t)scl;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;
  if (i2c_new_master_bus(&cfg, &bus_) != ESP_OK) {
    bus_ = nullptr;
    return false;
  }
  return true;
}

void TwoWire::end() {
  removeDevices();
  if (bus_ != nullptr) {
    i2c_del_master_bus(bus_);
    bus_ = nullptr;
  }
  txPending_ = false;
  rxLen_ = rxPos_ = 0;
}

void TwoWire::setClock(uint32_t hz) {
  if (hz == clockHz_) return;
  clockHz_ = hz;
  removeDevices();  // devices carry their own speed; they're made again on next use
}

void TwoWire::removeDevices() {
  for (int i = 0; i < deviceCount_; i++) i2c_master_bus_rm_device(devices_[i].handle);
  deviceCount_ = 0;
}

i2c_master_dev_handle_t TwoWire::device(uint8_t address) {
  if (bus_ == nullptr) return nullptr;
  for (int i = 0; i < deviceCount_; i++) {
    if (devices_[i].address == address) return devices_[i].handle;
  }
  if (deviceCount_ == MAX_DEVICES) return nullptr;
  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = address;
  cfg.scl_speed_hz = clockHz_;
  i2c_master_dev_handle_t handle;
  if (i2c_master_bus_add_device(bus_, &cfg, &handle) != ESP_OK) return nullptr;
  devices_[deviceCount_].address = address;
  devices_[deviceCount_].handle = handle;
  deviceCount_++;
  return handle;
}

void TwoWire::beginTransmission(uint8_t address) {
  txAddress_ = address;
  txLen_ = 0;
  txPending_ = false;
}

size_t TwoWire::write(uint8_t value) {
  if (txLen_ >= BUF_SIZE) return 0;
  txBuf_[txLen_++] = value;
  return 1;
}

// Returns 0 on success, like Arduino (4 = other error).
uint8_t TwoWire::endTransmission(bool sendStop) {
  if (!sendStop) {
    txPending_ = true;  // sent together with the next requestFrom()
    return 0;
  }
  i2c_master_dev_handle_t dev = device(txAddress_);
  if (dev == nullptr) return 4;
  esp_err_t err = i2c_master_transmit(dev, txBuf_, txLen_, TIMEOUT_MS);
  txLen_ = 0;
  return err == ESP_OK ? 0 : 4;
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity) {
  rxLen_ = rxPos_ = 0;
  if (quantity > BUF_SIZE) quantity = BUF_SIZE;
  i2c_master_dev_handle_t dev = device(address);
  if (dev == nullptr) {
    txPending_ = false;
    return 0;
  }
  esp_err_t err;
  if (txPending_ && txAddress_ == address && txLen_ > 0) {
    err = i2c_master_transmit_receive(dev, txBuf_, txLen_, rxBuf_, quantity, TIMEOUT_MS);
  } else {
    err = i2c_master_receive(dev, rxBuf_, quantity, TIMEOUT_MS);
  }
  txPending_ = false;
  txLen_ = 0;
  if (err != ESP_OK) return 0;
  rxLen_ = quantity;
  return quantity;
}

int TwoWire::available() { return (int)(rxLen_ - rxPos_); }

int TwoWire::read() { return rxPos_ < rxLen_ ? rxBuf_[rxPos_++] : -1; }
