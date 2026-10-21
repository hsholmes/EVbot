#include "WString.h"

#include <stdio.h>

namespace {

std::string formatted(const char *format, double n, unsigned int decimals) {
  char buf[40];
  snprintf(buf, sizeof(buf), format, (int)decimals, n);
  return buf;
}

}  // namespace

String::String(int n) : s_(std::to_string(n)) {}
String::String(unsigned int n) : s_(std::to_string(n)) {}
String::String(long n) : s_(std::to_string(n)) {}
String::String(unsigned long n) : s_(std::to_string(n)) {}
String::String(float n, unsigned int decimals) : s_(formatted("%.*f", n, decimals)) {}
String::String(double n, unsigned int decimals) : s_(formatted("%.*f", n, decimals)) {}
