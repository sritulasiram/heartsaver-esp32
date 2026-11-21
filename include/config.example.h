// Copy this file to include/config.h (gitignored) and edit.
#pragma once

// ── Recipient (international format) ────────────────────────────────────────
#define ALERT_PHONE_NUMBER "+10000000000"

// ── Pins ────────────────────────────────────────────────────────────────────
#define GPS_RX_PIN   14   // ESP32 RX  <- GPS TX
#define GPS_TX_PIN   15   // ESP32 TX  -> GPS RX
#define GSM_RX_PIN   16   // ESP32 RX  <- GSM TX
#define GSM_TX_PIN   17   // ESP32 TX  -> GSM RX
#define PULSE_PIN    36   // ADC1, input only
#define SOS_BUTTON_PIN 4  // to GND, INPUT_PULLUP
#define LED_PIN      2

// ── Serial speeds ───────────────────────────────────────────────────────────
#define GPS_BAUD 9600
#define GSM_BAUD 19200

// ── Heart rate ──────────────────────────────────────────────────────────────
#define PULSE_MIN_AMPLITUDE   150     // min peak-to-peak ADC counts to trust signal
#define HIGH_BPM_THRESHOLD    90
#define HIGH_BPM_SUSTAIN_MS   10000UL // must stay high this long before alerting
#define HIGH_BPM_HYSTERESIS   5       // BPM must drop this far below to re-arm
#define ALERT_COOLDOWN_MS     300000UL // shared by the high and low BPM alerts

// Low heart rate (bradycardia). A reading of 0 means "no signal" and never alerts.
#define LOW_BPM_THRESHOLD     50
#define LOW_BPM_SUSTAIN_MS    10000UL
#define LOW_BPM_HYSTERESIS    5

// Automatic phone call to ALERT_PHONE_NUMBER after the alert SMS is sent
#define CALL_ON_SOS           1
#define CALL_ON_BPM           1       // high or low heart rate alerts
#define CALL_DURATION_MS      25000UL // ring time before hanging up

// ── Behaviour ───────────────────────────────────────────────────────────────
#define SEND_SMS_ON_FIRST_FIX 1
#define SMS_MAX_ATTEMPTS      3
#define SOS_MAX_ATTEMPTS      10
#define LOCATION_STALE_MS     30000UL
