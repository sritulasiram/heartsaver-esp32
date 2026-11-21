#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>

// Adapts HardwareSerial to the Port interface expected by AtModem.
class SerialPort {
 public:
  explicit SerialPort(HardwareSerial& s) : s_(s) {}
  void begin(uint32_t baud, int rx, int tx) { s_.begin(baud, SERIAL_8N1, rx, tx); }
  int available() { return s_.available(); }
  int read() { return s_.read(); }
  void write(uint8_t b) { s_.write(b); }
  void print(const char* t) { s_.print(t); }
  void println(const char* t) {
    Serial.printf("[GSM] > %s\n", t);
    s_.println(t);
  }

 private:
  HardwareSerial& s_;
};
