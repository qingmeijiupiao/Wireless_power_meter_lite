# energy_meter

Shared UI/Web metering session built on top of the LP Core lifetime counters.

- `snapshot()` returns relative `uAh`, `uWh`, and metering time.
- `reset()` stores a new shared baseline without modifying LP Core counters.
- Screen and Web use the same baseline, so reset operations are visible to both.
