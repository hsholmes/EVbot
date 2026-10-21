#pragma once

// Just enough of Arduino's String for the engine: build text from strings
// and numbers, join it with +, and hand it on with c_str().

#include <string>

class String {
 public:
  String() {}
  String(const char *text) : s_(text ? text : "") {}
  String(char c) : s_(1, c) {}
  String(int n);
  String(unsigned int n);
  String(long n);
  String(unsigned long n);
  String(float n, unsigned int decimals = 2);
  String(double n, unsigned int decimals = 2);

  const char *c_str() const { return s_.c_str(); }
  unsigned int length() const { return s_.length(); }

  String &operator+=(const String &other) { s_ += other.s_; return *this; }
  String &operator+=(const char *text) { if (text) s_ += text; return *this; }
  String &operator+=(char c) { s_ += c; return *this; }

  friend String operator+(const String &a, const String &b) { String r(a); r += b; return r; }
  friend String operator+(const String &a, const char *b) { String r(a); r += b; return r; }
  friend String operator+(const char *a, const String &b) { String r(a); r += b; return r; }

  bool operator==(const String &other) const { return s_ == other.s_; }
  bool operator==(const char *text) const { return s_ == (text ? text : ""); }

 private:
  std::string s_;
};
