// Host-side unit tests for the pure logic. Run: make -C tests
#include <assert.h>
#include <math.h>
#include <string.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "alert.h"
#include "at_modem.h"
#include "debounce.h"
#include "heart_rate.h"

// Synthetic pulse: a bump of `amp` once per `periodMs`, sampled every 5 ms.
static int feedPulse(HeartRateMonitor& hr, int bpm, uint32_t fromMs, uint32_t durMs) {
  const uint32_t period = 60000 / bpm;
  uint32_t t = fromMs;
  for (; t < fromMs + durMs; t += 5) {
    const uint32_t phase = t % period;
    const int v = phase < 60 ? 3400 : 2900;
    hr.feed(v, t);
  }
  return t;
}

static void testHeartRate() {
  HeartRateMonitor hr(150);
  uint32_t t = feedPulse(hr, 75, 1000, 20000);
  const int b = hr.bpm(t);
  assert(b >= 72 && b <= 78);

  // flat line = signal lost
  for (uint32_t i = t; i < t + 5000; i += 5) hr.feed(3000, i);
  assert(hr.bpm(t + 5000) == 0);

  // noise below min amplitude never counts
  HeartRateMonitor quiet(150);
  for (uint32_t i = 0; i < 20000; i += 5) quiet.feed(3000 + ((i / 5) % 2) * 20, i);
  assert(quiet.bpm(20000) == 0);
}

static void testGate() {
  BpmAlertGate g(90, 10000, 5, 300000);
  assert(!g.update(100, 0));
  assert(!g.update(100, 9000));
  assert(g.update(100, 10000));    // sustained -> fires
  assert(!g.update(100, 20000));   // once only
  assert(!g.update(80, 30000));    // re-armed
  assert(!g.update(100, 31000));
  assert(!g.update(100, 45000));   // sustained again but inside cooldown
  assert(g.update(100, 311000));   // after cooldown, still high -> fires
  BpmAlertGate h(90, 0, 5, 0);
  assert(!h.update(0, 0));         // no signal never fires
  assert(!h.update(88, 1));        // inside hysteresis band, not above
}

static void testDebounce() {
  Debouncer d(50);
  assert(!d.update(true, 100));
  assert(!d.update(true, 120));
  assert(d.update(true, 160));     // stable press -> edge once
  assert(!d.update(true, 300));
  assert(!d.update(false, 310));
  assert(!d.update(false, 400));   // release
  assert(!d.update(true, 410));    // bounce
  assert(!d.update(false, 415));
  assert(!d.update(true, 420));
  assert(d.update(true, 500));
}

static void testQueue() {
  AlertQueue<2> q;
  Alert fix, bpm, sos;
  fix.reason = AlertReason::GpsFix;
  bpm.reason = AlertReason::HighBpm;
  sos.reason = AlertReason::Sos;
  assert(q.push(fix));
  assert(q.push(bpm));
  assert(q.push(sos));            // evicts lowest (fix)
  assert(q.size() == 2);
  Alert out;
  assert(q.pop(out) && out.reason == AlertReason::Sos);
  assert(q.pop(out) && out.reason == AlertReason::HighBpm);
  assert(!q.pop(out));
  assert(q.push(sos) && q.push(sos));
  assert(!q.push(fix));           // full of higher priority -> incoming dropped
}

static void testFormat() {
  char buf[161];
  Alert a;
  a.reason = AlertReason::Sos;
  a.bpm = 88;
  a.hasLocation = true;
  a.lat = 5.421234;
  a.lng = 100.336789;
  int n = formatAlert(buf, sizeof(buf), a);
  assert(n < 160);
  assert(strstr(buf, "SOS") && strstr(buf, "BPM: 88") && strstr(buf, "q=5.421234,100.336789"));
  a.locAgeMs = 60000;
  formatAlert(buf, sizeof(buf), a);
  assert(strstr(buf, "Last known"));
  a.hasLocation = false;
  a.bpm = 0;
  formatAlert(buf, sizeof(buf), a);
  assert(strstr(buf, "BPM: --") && strstr(buf, "no GPS fix"));
}


// ── Fake serial modem for AtModem ───────────────────────────────────────────
struct FakePort {
  std::string pending, rx, body;
  std::vector<std::string> commands;
  std::vector<uint8_t> bytes;
  std::function<std::string(const std::string&)> respond;

  int available() { return static_cast<int>(rx.size()); }
  int read() {
    int c = static_cast<unsigned char>(rx.front());
    rx.erase(rx.begin());
    return c;
  }
  void write(uint8_t b) {
    bytes.push_back(b);
    if (b == 26 && respond) pending += respond("<CTRLZ>");
  }
  void print(const char* s) { body += s; }
  void println(const char* s) {
    commands.push_back(s);
    if (respond) pending += respond(s);
  }
  // Deliver up to n pending chars to the modem's receive buffer.
  void pump(size_t n) {
    const size_t k = n < pending.size() ? n : pending.size();
    rx += pending.substr(0, k);
    pending.erase(0, k);
  }
};

using Modem = AtModem<FakePort>;

static std::string happyModem(const std::string& cmd) {
  if (cmd == "AT+CREG?") return "\r\n+CREG: 0,1\r\n\r\nOK\r\n";
  if (cmd.rfind("AT+CMGS", 0) == 0) return "\r\n> ";
  if (cmd == "<CTRLZ>") return "\r\n+CMGS: 7\r\n\r\nOK\r\n";
  return "\r\nOK\r\n";
}

// Runs the modem from t0 to t1 in 10 ms steps, delivering `chars` chars per step.
static uint32_t run(Modem& m, FakePort& f, uint32_t t0, uint32_t t1, size_t chars = 1000) {
  uint32_t t = t0;
  for (; t < t1; t += 10) {
    f.pump(chars);
    m.update(t);
  }
  return t;
}

static void testModemInitAndSend() {
  FakePort f;
  f.respond = happyModem;
  Modem m(f);
  assert(!m.ready());
  uint32_t t = run(m, f, 0, 1000);
  assert(m.ready());
  assert(f.commands.size() == 4 && f.commands[0] == "AT" && f.commands[1] == "ATE0" &&
         f.commands[2] == "AT+CMGF=1" && f.commands[3] == "AT+CREG?");

  assert(m.send("+10000000000", "hello", t));
  assert(m.busy() && !m.send("+1", "x", t));  // refuses a second send
  run(m, f, t, t + 500);
  assert(f.commands.back() == "AT+CMGS=\"+10000000000\"");
  assert(f.body == "hello" && f.bytes.back() == 26);
  assert(m.takeResult() == Modem::Result::Sent);
  assert(m.takeResult() == Modem::Result::None);  // reported once
  assert(m.ready());
}

static void testModemSplitReplies() {
  FakePort f;
  f.respond = happyModem;
  Modem m(f);
  run(m, f, 0, 4000, 1);  // one char per 10 ms: every reply arrives fragmented
  assert(m.ready());
}

static void testModemWaitsForNetwork() {
  FakePort f;
  bool registered = false;
  f.respond = [&](const std::string& c) {
    if (c == "AT+CREG?") return std::string(registered ? "\r\n+CREG: 0,5\r\n\r\nOK\r\n" : "\r\n+CREG: 0,2\r\n\r\nOK\r\n");
    return happyModem(c);
  };
  Modem m(f);
  uint32_t t = run(m, f, 0, 8000);
  assert(!m.ready());      // searching: not ready, but still retrying
  registered = true;       // roaming (5) counts as registered
  run(m, f, t, t + 8000);
  assert(m.ready());
}

static void testModemInitTimeoutRetries() {
  FakePort f;  // silent modem
  Modem m(f);
  run(m, f, 0, 10000);
  assert(!m.ready());
  assert(f.commands.size() >= 3);  // kept retrying "AT"
  f.respond = happyModem;          // modem wakes up
  run(m, f, 10000, 20000);
  assert(m.ready());
}

static void testModemSendErrors() {
  // ERROR at the '>' prompt -> failed, ESC sent, no body.
  FakePort f;
  f.respond = [](const std::string& c) {
    if (c.rfind("AT+CMGS", 0) == 0) return std::string("\r\n+CMS ERROR: 330\r\n");
    return happyModem(c);
  };
  Modem m(f);
  uint32_t t = run(m, f, 0, 1000);
  assert(m.ready() && m.send("+1", "x", t));
  run(m, f, t, t + 200);
  assert(m.takeResult() == Modem::Result::Failed);
  assert(f.bytes.back() == 27 && f.body.empty() && m.ready());

  // Network never confirms: must report Failed after the timeout, never Sent.
  FakePort g;
  g.respond = [](const std::string& c) {
    if (c == "<CTRLZ>") return std::string();
    return happyModem(c);
  };
  Modem n(g);
  t = run(n, g, 0, 1000);
  assert(n.send("+1", "x", t));
  t = run(n, g, t, t + 30000);
  assert(n.busy() && n.takeResult() == Modem::Result::None);  // still waiting
  run(n, g, t, t + 40000);
  assert(n.takeResult() == Modem::Result::Failed && n.ready());
}

static void testLowBpmGate() {
  BpmAlertGate g(50, 10000, 5, 300000, true);
  assert(!g.update(0, 0));          // no signal is never a low-BPM breach
  assert(!g.update(0, 20000));
  assert(!g.update(45, 21000));
  assert(!g.update(45, 30000));     // not sustained yet
  assert(g.update(45, 31000));      // sustained low -> fires
  assert(!g.update(45, 50000));     // once only
  assert(!g.update(52, 60000));     // inside hysteresis band: stays disarmed
  assert(!g.update(45, 61000));
  assert(!g.update(60, 70000));     // recovered (>= 55): re-armed
  assert(!g.update(45, 71000));
  assert(!g.update(45, 90000));     // sustained but inside cooldown
  assert(g.update(45, 331000));     // after cooldown -> fires again
  assert(!g.update(50, 400000));    // exactly at the limit is not low
  BpmAlertGate lost(50, 0, 5, 0, true);
  assert(!lost.update(0, 1));       // lifted sensor
  assert(!lost.update(80, 2));      // normal rate
}

static void testWantsCall() {
  assert(wantsCall(AlertReason::Sos, true, false));
  assert(!wantsCall(AlertReason::Sos, false, true));
  assert(wantsCall(AlertReason::LowBpm, false, true));
  assert(wantsCall(AlertReason::HighBpm, false, true));
  assert(!wantsCall(AlertReason::HighBpm, true, false));
  assert(!wantsCall(AlertReason::GpsFix, true, true));  // status message, never a call
  // Low heart rate outranks high; SOS outranks everything.
  assert(alertPriority(AlertReason::Sos) > alertPriority(AlertReason::LowBpm));
  assert(alertPriority(AlertReason::LowBpm) > alertPriority(AlertReason::HighBpm));
  char buf[161];
  Alert a;
  a.reason = AlertReason::LowBpm;
  a.bpm = 42;
  formatAlert(buf, sizeof(buf), a);
  assert(strstr(buf, "Low heart rate") && strstr(buf, "BPM: 42"));
}

static Modem* readyModem(FakePort& f, uint32_t& t) {
  f.respond = happyModem;
  Modem* m = new Modem(f);
  t = run(*m, f, 0, 1000);
  assert(m->ready());
  return m;
}

static void testModemCalls() {
  uint32_t t;
  // Call that the callee declines/ends: finishes before the ring timeout.
  {
    FakePort f;
    Modem* m = readyModem(f, t);
    assert(m->dial("+10000000000", 20000, t));
    assert(m->busy() && m->calling() && !m->send("+1", "x", t));
    t = run(*m, f, t, t + 200);
    assert(f.commands.back() == "ATD+10000000000;");
    assert(m->calling());
    f.pending += "\r\nNO CARRIER\r\n";
    t = run(*m, f, t, t + 200);
    assert(m->ready() && m->takeCallResult() == Modem::Result::Sent);
    assert(m->takeCallResult() == Modem::Result::None);
    delete m;
  }
  // Rings the full duration, then hangs up with ATH.
  {
    FakePort f;
    Modem* m = readyModem(f, t);
    assert(m->dial("+1", 5000, t));
    t = run(*m, f, t, t + 4000);
    assert(m->calling());                      // still ringing
    t = run(*m, f, t, t + 2000);
    assert(f.commands.back() == "ATH" && m->ready());
    assert(m->takeCallResult() == Modem::Result::Sent);
    delete m;
  }
  // Early hang-up so an urgent SMS can go out.
  {
    FakePort f;
    Modem* m = readyModem(f, t);
    assert(m->dial("+1", 60000, t));
    t = run(*m, f, t, t + 500);
    assert(m->calling());
    m->hangUp(t);
    t = run(*m, f, t, t + 200);
    assert(f.commands.back() == "ATH" && m->ready());
    assert(m->send("+1", "urgent", t));        // SMS can now proceed
    delete m;
  }
  // Dial rejected: reports failure and still releases the line.
  {
    FakePort f;
    f.respond = [](const std::string& c) {
      if (c.rfind("ATD", 0) == 0) return std::string("\r\nERROR\r\n");
      return happyModem(c);
    };
    Modem m(f);
    t = run(m, f, 0, 1000);
    assert(m.dial("+1", 5000, t));
    t = run(m, f, t, t + 500);
    assert(m.takeCallResult() == Modem::Result::Failed);
    assert(f.commands.back() == "ATH" && m.ready());
  }
  // hangUp with no call in progress does nothing; dial refused until ready.
  {
    FakePort f;
    f.respond = happyModem;
    Modem m(f);
    assert(!m.dial("+1", 1000, 0));
    m.hangUp(0);
    assert(f.commands.empty());
  }
}

int main() {
  testHeartRate();
  testGate();
  testDebounce();
  testQueue();
  testFormat();
  testModemInitAndSend();
  testModemSplitReplies();
  testModemWaitsForNetwork();
  testModemInitTimeoutRetries();
  testModemSendErrors();
  testLowBpmGate();
  testWantsCall();
  testModemCalls();
  puts("all tests passed");
  return 0;
}
