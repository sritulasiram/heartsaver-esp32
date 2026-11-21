/*
 * ESP32 Health & Safety Tracker
 * Fixes:
 *  - GSM now uses HardwareSerial (Serial2) instead of SoftwareSerial
 *  - GPS uses SoftwareSerial (only one instance now)
 *  - RST_BUTTON has INPUT_PULLUP + debounce
 *  - Non-blocking AT command state machine for SMS
 *  - BPM calculation corrected (beats/30s * 2)
 *  - Per-trigger smsTriggered flags (GPS, button, BPM) so they don't suppress each other
 *  - Removed unused defaultMessage[]
 */

#include <TinyGPS++.h>
#include <SoftwareSerial.h>
#include <HardwareSerial.h>

// ── Pin definitions ─────────────────────────────────────────────────────────
#define GPS_RX          14
#define GPS_TX          15
#define GSM_RX          16   // Serial2 RX (hardware)
#define GSM_TX          17   // Serial2 TX (hardware)
#define PULSE_PIN       36
#define RST_BUTTON      4
#define LED_PIN         2

// ── Config ──────────────────────────────────────────────────────────────────
const char PHONE_NUMBER[] = "+601129513169";

// ── Serial / GPS ─────────────────────────────────────────────────────────────
SoftwareSerial gpsSerial(GPS_RX, GPS_TX);   // GPS only — one SoftwareSerial
// GSM → Serial2 (hardware), initialised in setup()
TinyGPSPlus gps;

// ── GPS state ────────────────────────────────────────────────────────────────
double lastLat  = 0.0;
double lastLng  = 0.0;
bool   gpsFix   = false;
bool   gpsSmsTriggered = false;

// ── Button state ─────────────────────────────────────────────────────────────
bool   btnSmsTriggered    = false;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;
int    lastButtonState    = HIGH;
int    buttonState        = HIGH;

// ── Heart rate state ─────────────────────────────────────────────────────────
unsigned long prevMillisSample = 0;
unsigned long prevMillisReport = 0;
const long    SAMPLE_INTERVAL  = 5;      // ms between ADC reads
const long    REPORT_INTERVAL  = 1000;   // ms between report ticks

const int UPPER_THRESHOLD = 3190;
const int LOWER_THRESHOLD = 3100;

int  pulseSignal    = 0;
bool aboveThreshold = false;
int  beatCount      = 0;
int  bpmTimer       = 0;
int  bpmVal         = 0;
bool bpmSmsTriggered = false;

// ── Non-blocking SMS state machine ───────────────────────────────────────────
enum SmsState {
  SMS_IDLE,
  SMS_SET_TEXT_MODE,
  SMS_AWAIT_TEXT_MODE,
  SMS_SET_RECIPIENT,
  SMS_AWAIT_RECIPIENT,
  SMS_SEND_BODY,
  SMS_AWAIT_BODY,
  SMS_SEND_CTRL_Z,
  SMS_AWAIT_DONE
};

SmsState    smsState       = SMS_IDLE;
unsigned long smsStateTime = 0;
const unsigned long SMS_CMD_TIMEOUT = 3000;  // ms to wait for each AT response

// Queued SMS data
bool   smsPending   = false;
double pendingLat   = 0.0;
double pendingLng   = 0.0;
int    pendingBpm   = 0;
String pendingReason = "";

// ── Helpers ──────────────────────────────────────────────────────────────────
void queueSMS(double lat, double lng, int bpm, const String& reason) {
  if (smsPending || smsState != SMS_IDLE) {
    // Already one queued — skip (or you could store a queue)
    Serial.println("[SMS] Already pending, skipping new request.");
    return;
  }
  smsPending    = true;
  pendingLat    = lat;
  pendingLng    = lng;
  pendingBpm    = bpm;
  pendingReason = reason;
  Serial.println("[SMS] Queued: " + reason);
}

void tickSMS() {
  unsigned long now = millis();

  if (smsState == SMS_IDLE) {
    if (!smsPending) return;
    smsPending = false;

    // Start the sequence
    digitalWrite(LED_PIN, HIGH);
    Serial2.println("AT+CMGF=1");
    Serial.println("[GSM] AT+CMGF=1");
    smsState     = SMS_AWAIT_TEXT_MODE;
    smsStateTime = now;
    return;
  }

  // Drain Serial2 into a response buffer
  String resp = "";
  while (Serial2.available()) {
    resp += (char)Serial2.read();
  }

  switch (smsState) {

    case SMS_AWAIT_TEXT_MODE:
      if (resp.indexOf("OK") >= 0 || now - smsStateTime > SMS_CMD_TIMEOUT) {
        String cmd = "AT+CMGS=\"" + String(PHONE_NUMBER) + "\"";
        Serial2.println(cmd);
        Serial.println("[GSM] " + cmd);
        smsState     = SMS_AWAIT_RECIPIENT;
        smsStateTime = now;
      }
      break;

    case SMS_AWAIT_RECIPIENT:
      // Module replies with "> " prompt when ready for body
      if (resp.indexOf(">") >= 0 || now - smsStateTime > SMS_CMD_TIMEOUT) {
        String mapsLink = "https://maps.google.com/?q=" +
                          String(pendingLat, 6) + "," + String(pendingLng, 6);
        String body = "ALERT: " + pendingReason + "\n"
                    + "BPM: " + String(pendingBpm) + "\n"
                    + "Lat: " + String(pendingLat, 6) + "\n"
                    + "Lng: " + String(pendingLng, 6) + "\n"
                    + mapsLink;
        Serial2.print(body);
        Serial.println("[GSM] Body sent.");
        smsState     = SMS_AWAIT_BODY;
        smsStateTime = now;
      }
      break;

    case SMS_AWAIT_BODY:
      // Small pause then send Ctrl+Z
      if (now - smsStateTime > 500) {
        Serial2.write(26);   // Ctrl+Z
        Serial.println("[GSM] Ctrl+Z sent.");
        smsState     = SMS_AWAIT_DONE;
        smsStateTime = now;
      }
      break;

    case SMS_AWAIT_DONE:
      if (resp.indexOf("+CMGS") >= 0 || now - smsStateTime > SMS_CMD_TIMEOUT) {
        Serial.println("[GSM] SMS sent successfully.");
        digitalWrite(LED_PIN, LOW);
        smsState = SMS_IDLE;
      } else if (resp.indexOf("ERROR") >= 0) {
        Serial.println("[GSM] SMS failed.");
        digitalWrite(LED_PIN, LOW);
        smsState = SMS_IDLE;
      }
      break;

    default:
      break;
  }
}

// ── GPS ──────────────────────────────────────────────────────────────────────
void tickGPS() {
  while (gpsSerial.available() > 0) {
    if (gps.encode(gpsSerial.read())) {
      if (gps.location.isValid()) {
        lastLat = gps.location.lat();
        lastLng = gps.location.lng();
        gpsFix  = true;

        Serial.printf("[GPS] Lat: %.6f  Lng: %.6f\n", lastLat, lastLng);
        Serial.printf("[GPS] https://maps.google.com/?q=%.6f,%.6f\n", lastLat, lastLng);

        if (!gpsSmsTriggered) {
          queueSMS(lastLat, lastLng, bpmVal, "GPS Fix");
          gpsSmsTriggered = true;
        }
      }
    }
  }
}

// ── Button ───────────────────────────────────────────────────────────────────
void tickButton() {
  int reading = digitalRead(RST_BUTTON);
  unsigned long now = millis();

  if (reading != lastButtonState) {
    lastDebounceTime = now;
  }

  if (now - lastDebounceTime > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        // Button just pressed
        Serial.println("[BTN] Manual trigger.");
        if (!btnSmsTriggered) {
          queueSMS(lastLat, lastLng, bpmVal, "Manual button press");
          btnSmsTriggered = true;
        }
      } else {
        // Button released — reset so it can trigger again next press
        btnSmsTriggered = false;
      }
    }
  }

  lastButtonState = reading;
}

// ── Heart Rate ────────────────────────────────────────────────────────────────
void tickHeartRate() {
  unsigned long now = millis();

  // Sampling tick
  if (now - prevMillisSample >= SAMPLE_INTERVAL) {
    prevMillisSample = now;
    pulseSignal = analogRead(PULSE_PIN);

    if (pulseSignal > UPPER_THRESHOLD && !aboveThreshold) {
      beatCount++;
      aboveThreshold = true;
    }
    if (pulseSignal < LOWER_THRESHOLD) {
      aboveThreshold = false;
    }
  }

  // Reporting tick
  if (now - prevMillisReport >= REPORT_INTERVAL) {
    prevMillisReport = now;
    bpmTimer++;

    if (bpmTimer >= 30) {
      bpmTimer = 0;

      // beatCount = beats in ~30 s → multiply by 2 for BPM
      bpmVal = beatCount * 2;
      beatCount = 0;

      Serial.printf("[HR] BPM: %d\n", bpmVal);

      if (bpmVal > 90) {
        if (!bpmSmsTriggered) {
          queueSMS(lastLat, lastLng, bpmVal, "High BPM alert");
          bpmSmsTriggered = true;
        }
      } else {
        // Reset so the next high-BPM event can trigger again
        bpmSmsTriggered = false;
      }
    }
  }
}

// ── Setup / Loop ──────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600);
  Serial2.begin(19200, SERIAL_8N1, GSM_RX, GSM_TX);  // Hardware UART for GSM

  pinMode(LED_PIN,    OUTPUT);
  pinMode(RST_BUTTON, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);

  Serial.println("[BOOT] ESP32 Health Tracker ready.");
}

void loop() {
  tickGPS();
  tickButton();
  tickHeartRate();
  tickSMS();   // Non-blocking SMS state machine runs every loop iteration
}
