# Architecture

## System overview

```mermaid
flowchart LR
    PULSE["Pulse sensor<br/>GPIO 36"] --> HR["HeartRateMonitor"]
    HR --> HIGH["BpmAlertGate<br/>high"]
    HR --> LOW["BpmAlertGate<br/>low"]
    BTN["SOS button<br/>GPIO 4"] --> DEB["Debouncer"]
    GPS["GPS module<br/>UART1"] --> TG["TinyGPS++"]

    HIGH --> Q["AlertQueue<br/>priority"]
    LOW --> Q
    DEB --> Q
    TG -->|"first fix"| Q
    TG -.->|"location"| FMT["formatAlert"]

    Q --> FMT --> MODEM["AtModem<br/>state machine"]
    MODEM --> SMS(["SMS"])
    MODEM --> CALL(["Phone call"])
```

`main.cpp` wires these together in a single non-blocking `loop()`. Nothing calls `delay()`.

## Main loop

```mermaid
flowchart TD
    A["loop starts"] --> B["Feed GPS bytes to TinyGPS++"]
    B --> C{"5 ms since<br/>last ADC sample?"}
    C -->|yes| D["Read pulse sensor,<br/>feed HeartRateMonitor"]
    C -->|no| E
    D --> E["Read BPM, 0 if no signal"]
    E --> F{"SOS button<br/>pressed?"}
    F -->|yes| G["Queue SOS"]
    F -->|no| H
    G --> H{"High or low<br/>gate fired?"}
    H -->|yes| I["Queue BPM alert"]
    H -->|no| J
    I --> J{"First GPS fix?"}
    J -->|yes| K["Queue GPS fix message"]
    J -->|no| L
    K --> L["modem.update"]
    L --> M["Send next queued SMS,<br/>or dial if a call is pending"]
    M --> A
```

## Code split

| Location | Role | Arduino dependency |
|---|---|---|
| `lib/tracker_core/heart_rate.h` | `HeartRateMonitor`, `BpmAlertGate` | none |
| `lib/tracker_core/alert.h` | `Alert`, `AlertQueue`, `formatAlert` | none |
| `lib/tracker_core/debounce.h` | `Debouncer` | none |
| `lib/tracker_core/at_modem.h` | AT-command state machine, templated on the serial port | none |
| `src/serial_port.h` | Adapts `HardwareSerial` to the modem's port interface | yes |
| `src/main.cpp` | Sensor loop and alert plumbing | yes |

The pure logic is kept separate so it can be unit tested on a computer. `AtModem` takes the serial port as a template parameter, so tests drive it with a fake modem instead of real hardware.

```mermaid
flowchart TB
    subgraph arduino["Needs Arduino and hardware"]
        MAIN["src/main.cpp"]
        SP["src/serial_port.h"]
    end
    subgraph core["lib/tracker_core, pure C++"]
        HRH["heart_rate.h"]
        ALH["alert.h"]
        DBH["debounce.h"]
        ATH["at_modem.h"]
    end
    MAIN --> HRH & ALH & DBH & ATH
    MAIN --> SP
    SP -.->|"satisfies Port interface"| ATH
    TESTS["tests/test_logic.cpp<br/>runs on the host"] --> HRH & ALH & DBH & ATH
    TESTS -.->|"FakePort"| ATH
```

## Heart rate

1. Sample the ADC every 5 ms.
2. Every 2 s, take the min and max of the window. The thresholds sit at 40% and 60% of that range, so they follow the signal.
3. If the range is below `PULSE_MIN_AMPLITUDE`, treat it as no signal.
4. A rising crossing of the upper threshold is a beat. Crossings within 300 ms of the last beat are ignored.
5. BPM is `60000 / average` of the last 8 inter-beat intervals. It needs at least 4 intervals, and reports 0 if no beat for 3 s.

```mermaid
flowchart TD
    S["New ADC sample"] --> W["Update window min and max"]
    W --> R{"Window over 2 s?"}
    R -->|yes| T["Store range,<br/>start new window"]
    R -->|no| G
    T --> G{"Range known and<br/>at least min amplitude?"}
    G -->|no| NS["No signal<br/>BPM reads 0"]
    G -->|yes| LV{"Sample below<br/>40% level?"}
    LV -->|yes| RE["Re-arm detector"]
    LV -->|no| UP{"Sample above 60% level<br/>and detector armed?"}
    UP -->|no| DONE["Wait for next sample"]
    UP -->|yes| RF{"At least 300 ms since<br/>last beat?"}
    RF -->|no| DONE
    RF -->|yes| IB{"Gap since last beat<br/>over 2 s?"}
    IB -->|yes| RS["Restart averaging"]
    IB -->|no| PU["Store inter-beat interval"]
    RS --> BEAT["Record beat time"]
    PU --> BEAT
    BEAT --> OUT["BPM = 60000 / average interval<br/>needs 4 intervals, 0 if no beat for 3 s"]
```

## Alert gates

`BpmAlertGate` fires once when BPM stays beyond a limit for a sustained time: above it for the high gate, below it for the low gate. It re-arms after BPM recovers past `limit ∓ hysteresis`, and enforces a cooldown. A reading of 0 (no signal) is never a breach, so a lifted sensor cannot raise a low-BPM alert. The shortest measurable rate is about 30 BPM, since a gap over 2 s restarts averaging.

With the defaults, BPM falls into these zones:

```mermaid
flowchart LR
    Z1["Under 50<br/>LOW zone<br/>alert after 10 s"] --- Z2["50 to 54<br/>low re-arm band"]
    Z2 --- Z3["55 to 85<br/>NORMAL"]
    Z3 --- Z4["86 to 90<br/>high re-arm band"]
    Z4 --- Z5["Over 90<br/>HIGH zone<br/>alert after 10 s"]
    N0["0 = no signal<br/>never an alert"]
    style Z1 fill:#f8d7da,stroke:#b02a37,color:#000
    style Z5 fill:#f8d7da,stroke:#b02a37,color:#000
    style Z3 fill:#d1e7dd,stroke:#0f5132,color:#000
    style Z2 fill:#fff3cd,stroke:#997404,color:#000
    style Z4 fill:#fff3cd,stroke:#997404,color:#000
    style N0 fill:#e2e3e5,stroke:#41464b,color:#000
```

In a re-arm band the gate keeps its current state. The sustain timer is not reset, and a fired gate stays disarmed. Each gate is a small state machine:

```mermaid
stateDiagram-v2
    [*] --> Armed
    Armed --> Counting: BPM beyond limit
    Counting --> Fired: beyond limit for the sustain time and cooldown over
    Counting --> Armed: BPM recovers past the hysteresis band, or signal lost
    Fired --> Armed: BPM recovers past the hysteresis band
```

## Alert queue

Four slots, highest priority first (SOS, low BPM, high BPM, then GPS fix), FIFO within a priority. When full, the oldest lowest-priority entry is evicted. An incoming alert is dropped only if everything queued outranks it.

```mermaid
flowchart TD
    P["push alert"] --> F{"Queue full?"}
    F -->|no| ADD["Append"]
    F -->|yes| V["Find oldest entry with<br/>the lowest priority"]
    V --> C{"Victim outranks<br/>incoming alert?"}
    C -->|yes| DROP["Drop incoming alert"]
    C -->|no| EV["Evict victim, append incoming"]

    O["pop alert"] --> E{"Empty?"}
    E -->|yes| NONE["Nothing to send"]
    E -->|no| HI["Take highest priority,<br/>oldest first"]
```

## GSM modem

### Start-up

```mermaid
stateDiagram-v2
    [*] --> Boot
    Boot --> Probe: send AT
    Probe --> Echo: OK, send ATE0
    Echo --> TextMode: OK, send AT+CMGF=1
    TextMode --> Register: OK, send AT+CREG?
    Register --> Ready: registered, reply has 0,1 or 0,5
    Register --> Boot: not registered yet, retry in 3 s
    Probe --> Boot: error or timeout, retry in 2 s
    Echo --> Boot: error or timeout, retry in 2 s
    TextMode --> Boot: error or timeout, retry in 2 s
```

### Sending an SMS

```mermaid
sequenceDiagram
    participant M as main.cpp
    participant A as AtModem
    participant G as GSM module
    M->>A: send number and text
    A->>G: AT+CMGS with number
    G-->>A: prompt greater-than sign
    A->>G: message body, then Ctrl+Z
    alt network confirms
        G-->>A: +CMGS and OK
        A-->>M: result Sent
    else ERROR, or timeout at the prompt (5 s) or at confirmation (60 s)
        A-->>M: result Failed
        M->>M: requeue until attempt limit
    end
```

- Replies accumulate in a buffer, so a reply split across loop passes is still matched.
- A step succeeds on its expected token (`OK`, `>`, `+CMGS:`), fails on `ERROR`, and fails on timeout. A timeout is never reported as success.
- A failed send is re-queued, up to the attempt limits in `config.h`.

### Phone call

After an alert SMS is sent, `main.cpp` sets a call-pending flag for the alert types enabled by `CALL_ON_SOS` / `CALL_ON_BPM`. When the SMS queue is empty, the modem dials.

```mermaid
stateDiagram-v2
    Ready --> CallDial: dial, send ATD number
    CallDial --> CallRinging: OK, module accepted the call
    CallDial --> Hangup: ERROR or timeout, call failed
    CallRinging --> Ready: NO CARRIER, BUSY or NO ANSWER
    CallRinging --> Hangup: ring time over
    CallRinging --> Hangup: hangUp, a newer alert is waiting
    Hangup --> Ready: send ATH, OK or timeout
```

- If a new alert is queued while ringing, `hangUp()` drops the call so the SMS is not delayed.
- Several alerts in a row produce one call, not one each.
- The call is best effort: a failed call is logged and not retried. The SMS is the reliable channel.

### How an alert flows end to end

```mermaid
flowchart TD
    T(["Trigger: SOS, low BPM or high BPM"]) --> Q["Alert queued by priority"]
    Q --> RDY{"Modem ready<br/>and idle?"}
    RDY -->|no| WAIT["Wait, keep sampling sensors"]
    WAIT --> RDY
    RDY -->|yes| SEND["Send SMS"]
    SEND --> OK{"Sent?"}
    OK -->|no| RT{"Attempts left?"}
    RT -->|yes| Q
    RT -->|no| GU["Give up, log it"]
    OK -->|yes| WC{"Call enabled for<br/>this alert type?"}
    WC -->|no| END(["Done"])
    WC -->|yes| CP["Set call pending"]
    CP --> QE{"Queue empty?"}
    QE -->|no| SEND
    QE -->|yes| DIAL["Dial contact"]
    DIAL --> END
```

## SMS format

A single-part SMS under 160 characters:

```
ALERT: SOS - manual button press
BPM: 88
https://maps.google.com/?q=5.421234,100.336789
```

Without a fix the last line is `Location unknown (no GPS fix)`. A fix older than `LOCATION_STALE_MS` is prefixed with `Last known:`.
