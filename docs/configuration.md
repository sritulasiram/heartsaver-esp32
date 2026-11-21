# Configuration

Copy `include/config.example.h` to `include/config.h` and edit. `config.h` is gitignored.

| Setting | Default | Meaning |
|---|---|---|
| `ALERT_PHONE_NUMBER` | placeholder | Recipient in international format, e.g. `+60123456789` |
| `GPS_RX_PIN`, `GPS_TX_PIN` | 14, 15 | GPS UART pins (ESP32 side) |
| `GSM_RX_PIN`, `GSM_TX_PIN` | 16, 17 | GSM UART pins (ESP32 side) |
| `PULSE_PIN` | 36 | Pulse sensor ADC pin (use an ADC1 pin) |
| `SOS_BUTTON_PIN` | 4 | Button to GND |
| `LED_PIN` | 2 | Status LED |
| `GPS_BAUD`, `GSM_BAUD` | 9600, 19200 | Serial speeds |
| `PULSE_MIN_AMPLITUDE` | 150 | Minimum peak-to-peak ADC counts to trust the signal. Raise it if noise causes false readings, lower it if a real pulse is ignored. |
| `HIGH_BPM_THRESHOLD` | 90 | BPM above this counts as high |
| `HIGH_BPM_SUSTAIN_MS` | 10000 | BPM must stay high this long before alerting |
| `HIGH_BPM_HYSTERESIS` | 5 | BPM must fall this far below the threshold to re-arm |
| `ALERT_COOLDOWN_MS` | 300000 | Minimum gap between BPM alerts (each of high and low) |
| `LOW_BPM_THRESHOLD` | 50 | BPM below this counts as low |
| `LOW_BPM_SUSTAIN_MS` | 10000 | BPM must stay low this long before alerting |
| `LOW_BPM_HYSTERESIS` | 5 | BPM must rise this far above the threshold to re-arm |
| `CALL_ON_SOS` | 1 | Ring the contact after an SOS SMS |
| `CALL_ON_BPM` | 1 | Ring the contact after a high or low BPM SMS |
| `CALL_DURATION_MS` | 25000 | Ring time before the device hangs up |
| `SEND_SMS_ON_FIRST_FIX` | 1 | Send an SMS on the first GPS fix after boot |
| `SMS_MAX_ATTEMPTS` | 3 | Send attempts for non-SOS alerts |
| `SOS_MAX_ATTEMPTS` | 10 | Send attempts for SOS alerts |
| `LOCATION_STALE_MS` | 30000 | Fix older than this is labelled "Last known" |

## Security

Do not commit real phone numbers, WiFi passwords or API tokens. Keep them in `config.h` only. If a number was already committed to a public repository, it stays in the git history after you remove it. Rotate it if that matters.
