// Stands in for src/internal/Wireless.cpp, which is not compiled here: it
// owns NimBLE, and MicroPython's bluetooth module needs NimBLE for itself.
// The Python BLE service (frozen evbot_ble.py, phase 3) does the uploader
// page's job instead. Messages still reach the page: Serial writes to
// MicroPython's stdout, which the BLE service attaches to its LOG channel.

#include "Wireless.h"

namespace evbot {
namespace wireless {

void begin() {}
bool running() { return false; }
bool connected() { return false; }
bool updating() { return false; }
bool waitForPageAfterRestart(unsigned long) { return false; }
void print(const char*) {}
bool setIdentity(const char*, const char*) { return false; }
String carName() { return String("EVBot"); }

}  // namespace wireless
}  // namespace evbot
