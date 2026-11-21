# Contributing

1. `cp include/config.example.h include/config.h` (gitignored, never commit it).
2. Put hardware-independent logic in `lib/tracker_core/` and cover it in `tests/`.
3. Run `make -C tests` and `pio run` before opening a PR. CI runs both.
4. Update `docs/` and `CHANGELOG.md` when behaviour or settings change.

Never commit phone numbers, WiFi passwords or tokens.
