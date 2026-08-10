# Wiring — MW-001 (from the production release record; verify on site)

Board: Waveshare ESP32-S3-POE-ETH-8DI-8DO.
Sensor: LJ12A3-4-Z/BX, NPN, Normally Open, inductive proximity.

| Connection | Terminal |
|---|---|
| Sensor output (black) | DI1 |
| Sensor 0V (blue) | DI COM |
| Sensor +V (brown) | field-side supply, 7–36 V terminal block |

- DI1 maps to GPIO4 through the board's bidirectional optocoupler; the DI
  stage inverts (input active ⇒ GPIO reads LOW). PCNT counts rising edges —
  one count per target-release edge; polarity does not affect count-per-event.
- Only DI1 is used. DO/RS485/CAN/PoE-Ethernet are unused by this firmware.
- **Site verification required before trusting counts** (validation matrix
  A2/A3): measure sensor output, DI1, and supply voltages in both sensor
  states. The actual site supply rail is UNVERIFIED at packaging time.
