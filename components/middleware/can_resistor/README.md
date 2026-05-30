# can_resistor

CAN terminal resistor control middleware.

`CanResistor::instance()` owns the GPIO output and persists the enabled state in
the `can_term` NVS key. Call `init()` once with the configured GPIO before using
`get()`, `set()`, or `toggle()`.
