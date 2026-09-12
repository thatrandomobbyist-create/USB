# Safety and failsafe behavior

At startup, pins 2-13 are configured as `INPUT` (high impedance). Pins 0 and
1 are never controlled by this firmware because they are the Uno USB/UART
connection.

The firmware watchdog defaults to 500 ms (`WATCHDOG_TIMEOUT_MS` in the sketch).
If no valid command is received during that interval, every pin that was
configured as an output is driven LOW. Input pins are not changed. The serial
parser is reset and the firmware continues listening for valid frames. This is
a software failsafe, not an electrical safety guarantee.

Do not connect any GPIO to voltages outside the Uno/ATmega328P permitted range.
Use a current-limiting resistor for LEDs. Buttons should use an appropriate
pull-up or pull-down. Motors, relays, solenoids, lamps, and other high-current
or inductive loads require a transistor/MOSFET/relay driver, flyback protection,
and a suitable external supply. Never power such loads directly from an Uno
GPIO pin.

USB, Windows scheduling, serial buffering, and application stalls can add
latency and jitter. Windows is not a hard real-time operating system. For
timing-critical waveforms, send parameters to the Uno and generate the
waveform locally with hardware timers rather than issuing one Windows command
per edge.
