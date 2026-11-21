#include <Arduino.h>
#include <TinyGPS++.h>

#include "alert.h"
#include "config.h"
#include "at_modem.h"
#include "debounce.h"
#include "heart_rate.h"
#include "serial_port.h"

static TinyGPSPlus gps;
static HardwareSerial gpsSerial(1);
static SerialPort gsmPort(Serial2);
static void logGsm(const char* m) { Serial.printf("[GSM] %s\n", m); }
static AtModem<SerialPort> modem(gsmPort, logGsm);

static HeartRateMonitor heart(PULSE_MIN_AMPLITUDE);
static BpmAlertGate bpmGate(HIGH_BPM_THRESHOLD, HIGH_BPM_SUSTAIN_MS, HIGH_BPM_HYSTERESIS,
                            ALERT_COOLDOWN_MS);
static BpmAlertGate lowBpmGate(LOW_BPM_THRESHOLD, LOW_BPM_SUSTAIN_MS, LOW_BPM_HYSTERESIS,
                               ALERT_COOLDOWN_MS, /*below=*/true);
static Debouncer sosButton(50);
static AlertQueue<4> queue;

static Alert inFlight;
static bool haveInFlight = false;
static bool callPending = false;  // place a call once the SMS queue is empty
static bool fixAlertSent = false;
static uint32_t lastSample = 0, lastStatus = 0;

static Alert makeAlert(AlertReason reason, int bpm) {
  Alert a;
  a.reason = reason;
  a.bpm = bpm;
  if (gps.location.isValid()) {
    a.hasLocation = true;
    a.lat = gps.location.lat();
    a.lng = gps.location.lng();
    a.locAgeMs = gps.location.age();
  }
  return a;
}

static void enqueue(AlertReason reason, int bpm) {
  if (queue.push(makeAlert(reason, bpm))) Serial.printf("[ALERT] queued: %s\n", alertReasonText(reason));
  else Serial.println("[ALERT] queue full, dropped");
}

static void pumpSms() {
  switch (modem.takeResult()) {
    case AtModem<SerialPort>::Result::Sent:
      // SMS first, then ring the contact for the alerts that warrant it.
      if (haveInFlight && wantsCall(inFlight.reason, CALL_ON_SOS, CALL_ON_BPM)) callPending = true;
      haveInFlight = false;
      break;
    case AtModem<SerialPort>::Result::Failed: {
      const uint8_t max = inFlight.reason == AlertReason::Sos ? SOS_MAX_ATTEMPTS : SMS_MAX_ATTEMPTS;
      if (haveInFlight && ++inFlight.attempts < max) queue.push(inFlight);
      else Serial.println("[ALERT] giving up");
      haveInFlight = false;
      break;
    }
    default:
      break;
  }

  if (modem.takeCallResult() == AtModem<SerialPort>::Result::Failed)
    Serial.println("[ALERT] call failed");

  // A newer alert is waiting: drop the call so its SMS is not delayed.
  if (modem.calling() && queue.size() > 0) modem.hangUp(millis());

  if (modem.ready() && !haveInFlight && queue.pop(inFlight)) {
    char text[161];
    formatAlert(text, sizeof(text), inFlight, LOCATION_STALE_MS);
    haveInFlight = modem.send(ALERT_PHONE_NUMBER, text, millis());
  }

  if (callPending && modem.ready() && !haveInFlight && queue.size() == 0 &&
      modem.dial(ALERT_PHONE_NUMBER, CALL_DURATION_MS, millis()))
    callPending = false;

  digitalWrite(LED_PIN, modem.busy() ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(SOS_BUTTON_PIN, INPUT_PULLUP);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  gsmPort.begin(GSM_BAUD, GSM_RX_PIN, GSM_TX_PIN);
  Serial.println("[BOOT] ESP32 health tracker");
}

void loop() {
  const uint32_t now = millis();

  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  if (now - lastSample >= 5) {
    lastSample = now;
    heart.feed(analogRead(PULSE_PIN), now);
  }
  const int bpm = heart.bpm(now);

  if (sosButton.update(digitalRead(SOS_BUTTON_PIN) == LOW, now)) enqueue(AlertReason::Sos, bpm);
  if (bpmGate.update(bpm, now)) enqueue(AlertReason::HighBpm, bpm);
  if (lowBpmGate.update(bpm, now)) enqueue(AlertReason::LowBpm, bpm);
#if SEND_SMS_ON_FIRST_FIX
  if (!fixAlertSent && gps.location.isValid()) {
    fixAlertSent = true;
    enqueue(AlertReason::GpsFix, bpm);
  }
#endif

  modem.update(now);
  pumpSms();

  if (now - lastStatus >= 5000) {
    lastStatus = now;
    Serial.printf("[STAT] bpm=%d fix=%d sats=%lu gsm=%s queued=%u\n", bpm, gps.location.isValid(),
                  static_cast<unsigned long>(gps.satellites.value()), modem.ready() ? "ready" : "init",
                  queue.size());
  }
}
