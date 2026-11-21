# Changelog

## v2 (overhaul)

### Fixed
- An SMS timeout was reported as "sent successfully". It is now a failure, with retries.
- A modem reply split across loop passes was lost. Replies now accumulate in a buffer.
- An SOS pressed while another SMS was sending was dropped. A priority queue now sends SOS first.
- With no GPS fix, the SMS reported `0,0`. It now says the location is unknown.
- A real phone number was hardcoded in source and README. It now lives in a gitignored `config.h`.

### Changed
- GPS moved from SoftwareSerial to hardware UART1. Wiring is unchanged.
- Heart rate: fixed ADC thresholds and 30 s count replaced by an adaptive threshold and rolling inter-beat average. No signal reads as 0.
- High-BPM alert needs a sustained rise (default 10 s) and has a 5 min cooldown.
- GSM driver adds an init handshake and a network-registration check.
- Project moved to PlatformIO. Logic split into `lib/tracker_core`, drivers in `src/`.
- Documentation split into `docs/` and aligned with the HeartSaver IDP proposal. It now states clearly which proposal items (cloud, Android app, phone call, Wi-Fi location, low-BPM alert) are not implemented.

### Added
- Low heart-rate alert (matches the proposal flowchart). A lifted sensor never triggers it.
- Automatic phone call after SOS and BPM alerts (`ATD`/`ATH`), configurable, dropped early if a newer alert is waiting.
- Host unit tests (`make -C tests`), including the GSM state machine against a fake modem.
- GitHub Actions CI: host tests and firmware build.
- `CONTRIBUTING.md`, `.editorconfig`.
- Mermaid diagrams (flowcharts, state and sequence diagrams) across the README and docs.
- `docs/` pages and this changelog, including a user guide, a hardware evaluation plan, and a proposal-versus-implementation status table.
- `include/config.example.h`.

### Moved
- v1 sketches are in `legacy/`.

## v1

Single Arduino sketch: GPS, GSM and pulse sensor with a non-blocking SMS state machine.
