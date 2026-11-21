// Non-blocking SIM800/SIM900 AT-command state machine. Pure C++: the serial port
// is a template parameter so the logic can be tested against a fake modem.
//
// Port must provide:
//   int  available();
//   int  read();
//   void write(uint8_t b);
//   void print(const char* s);
//   void println(const char* s);
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

template <typename Port>
class AtModem {
 public:
  enum class Result : uint8_t { None, Sent, Failed };
  using Logger = void (*)(const char*);

  explicit AtModem(Port& port, Logger log = nullptr) : port_(port), log_(log) {}

  void update(uint32_t now) {
    drain();
    switch (state_) {
      case State::Boot:
        if (due(nextAt_, now)) {
          command("AT", "OK", 1000, now);
          state_ = State::Probe;
        }
        break;

      case State::Probe:
      case State::Echo:
      case State::TextMode: {
        const Poll p = poll(now);
        if (p == Poll::Pending) break;
        if (p != Poll::Ok) {
          note("init step failed, retrying");
          state_ = State::Boot;
          nextAt_ = now + 2000;
          break;
        }
        if (state_ == State::Probe) {
          command("ATE0", "OK", 1000, now);
          state_ = State::Echo;
        } else if (state_ == State::Echo) {
          command("AT+CMGF=1", "OK", 1000, now);
          state_ = State::TextMode;
        } else {
          command("AT+CREG?", "OK", 2000, now);
          state_ = State::Register;
        }
        break;
      }

      case State::Register: {
        const Poll p = poll(now);
        if (p == Poll::Pending) break;
        const char* reg = strstr(rx_, "+CREG:");
        const bool registered = reg && (strstr(reg, ",1") || strstr(reg, ",5"));
        if (p == Poll::Ok && registered) {
          note("registered, ready");
          state_ = State::Ready;
        } else {
          note("waiting for network");
          state_ = State::Boot;
          nextAt_ = now + 3000;
        }
        break;
      }

      case State::Ready:
        break;

      case State::SmsPrompt: {
        const Poll p = poll(now);
        if (p == Poll::Pending) break;
        if (p == Poll::Ok) {
          clearRx();
          port_.print(text_);
          port_.write(26);  // Ctrl+Z
          expect_ = "+CMGS:";
          deadline_ = now + 60000;  // network can take a while
          state_ = State::SmsWait;
        } else {
          port_.write(27);  // ESC: cancel
          result_ = Result::Failed;
          state_ = State::Ready;
        }
        break;
      }

      case State::SmsWait: {
        const Poll p = poll(now);
        if (p == Poll::Pending) break;
        result_ = (p == Poll::Ok) ? Result::Sent : Result::Failed;
        note(p == Poll::Ok ? "SMS sent" : "SMS failed");
        state_ = State::Ready;
        break;
      }

      case State::CallDial: {
        const Poll p = poll(now);  // "OK" = the module accepted the dial command
        if (p == Poll::Pending) break;
        if (p == Poll::Ok) {
          clearRx();
          note("calling");
          deadline_ = now + callMs_;
          state_ = State::CallRinging;
        } else {
          note("call failed");
          callResult_ = Result::Failed;
          startHangup(now);  // make sure the line is released
        }
        break;
      }

      case State::CallRinging:
        if (strstr(rx_, "NO CARRIER") || strstr(rx_, "BUSY") || strstr(rx_, "NO ANSWER")) {
          note("call ended");
          callResult_ = Result::Sent;
          state_ = State::Ready;
        } else if (due(deadline_, now)) {
          callResult_ = Result::Sent;  // rang for the full duration
          startHangup(now);
        }
        break;

      case State::Hangup: {
        const Poll p = poll(now);
        if (p == Poll::Pending) break;
        state_ = State::Ready;  // OK, error or timeout: nothing more we can do
        break;
      }
    }
  }

  bool ready() const { return state_ == State::Ready; }
  // True while an SMS is being sent or a call is in progress.
  bool busy() const { return state_ >= State::SmsPrompt; }
  bool calling() const { return state_ == State::CallDial || state_ == State::CallRinging; }

  // Starts sending; returns false if the modem is not ready.
  bool send(const char* number, const char* text, uint32_t now) {
    if (state_ != State::Ready) return false;
    snprintf(text_, sizeof(text_), "%s", text);
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
    command(cmd, ">", 5000, now);
    state_ = State::SmsPrompt;
    return true;
  }

  // Places a voice call and hangs up after `ringMs`. Returns false if not ready.
  bool dial(const char* number, uint32_t ringMs, uint32_t now) {
    if (state_ != State::Ready) return false;
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "ATD%s;", number);
    callMs_ = ringMs;
    command(cmd, "OK", 10000, now);
    state_ = State::CallDial;
    return true;
  }

  // Ends a call early (e.g. so an urgent SMS can go out). No effect otherwise.
  void hangUp(uint32_t now) {
    if (calling()) {
      callResult_ = Result::Sent;
      startHangup(now);
    }
  }

  // Outcome of the last call, returned once, then None. Sent = call was placed.
  Result takeCallResult() {
    const Result r = callResult_;
    callResult_ = Result::None;
    return r;
  }

  // Outcome of the last send, returned once, then None.
  Result takeResult() {
    const Result r = result_;
    result_ = Result::None;
    return r;
  }

 private:
  // Order matters: busy() treats SmsPrompt and everything after it as busy.
  enum class State : uint8_t {
    Boot, Probe, Echo, TextMode, Register, Ready,
    SmsPrompt, SmsWait, CallDial, CallRinging, Hangup
  };
  enum class Poll : uint8_t { Pending, Ok, Error, Timeout };

  void startHangup(uint32_t now) {
    command("ATH", "OK", 2000, now);
    state_ = State::Hangup;
  }

  static bool due(uint32_t at, uint32_t now) { return static_cast<int32_t>(now - at) >= 0; }
  void note(const char* s) {
    if (log_) log_(s);
  }
  void clearRx() {
    rxLen_ = 0;
    rx_[0] = '\0';
  }

  void drain() {
    while (port_.available() > 0) {
      if (rxLen_ >= sizeof(rx_) - 1) {  // keep newest half
        const size_t half = rxLen_ / 2;
        memmove(rx_, rx_ + half, rxLen_ - half);
        rxLen_ -= half;
      }
      rx_[rxLen_++] = static_cast<char>(port_.read());
      rx_[rxLen_] = '\0';
    }
  }

  void command(const char* cmd, const char* expect, uint32_t timeoutMs, uint32_t now) {
    clearRx();
    port_.println(cmd);
    expect_ = expect;
    deadline_ = now + timeoutMs;
  }

  Poll poll(uint32_t now) const {
    if (strstr(rx_, expect_)) return Poll::Ok;
    if (strstr(rx_, "ERROR")) return Poll::Error;
    if (due(deadline_, now)) return Poll::Timeout;
    return Poll::Pending;
  }

  Port& port_;
  Logger log_;
  State state_ = State::Boot;
  uint32_t nextAt_ = 0, deadline_ = 0;
  const char* expect_ = "OK";
  char rx_[160] = {};
  size_t rxLen_ = 0;
  char text_[161] = {};
  uint32_t callMs_ = 0;
  Result result_ = Result::None;
  Result callResult_ = Result::None;
};
