# USB

USB is a small split architecture: a Windows C++17 application owns the
main program and the Arduino Uno only executes a fixed binary GPIO protocol.
The reusable Windows library is in `UnoGPIO/`; the firmware is in
`Arduino/UnoGPIO_Firmware/`.

## Upload the firmware

1. Install the Arduino IDE and select **Arduino Uno** and the correct USB
   processor/port under **Tools**.
2. Open `Arduino/UnoGPIO_Firmware/UnoGPIO_Firmware.ino`.
3. Compile and upload. The sketch uses the hardware serial interface at
   115200 baud.

## Find the COM port

With the Uno connected, open Windows Device Manager and expand **Ports
(COM & LPT)**. Use the COM number shown for **Arduino Uno** (for example
`COM3`). Close the Arduino Serial Monitor before running the application.

## Build and run

From a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\WindowsExample.exe
```

The example opens `COM3`; edit `examples/WindowsExample/main.cpp` for another
port. It configures pin 13, reads pin 7, blinks at a modest rate, and then
demonstrates a 500 Hz command loop. That loop illustrates pacing only and does
not claim deterministic 500 Hz timing.

`UnoGPIO` opens the port in its constructor, is non-copyable and movable, and
closes the native handle with RAII. It maintains a local output direction/value
state, validates pins 2-13, checks response framing, sequence, length, and
CRC, and reports serial failures as `unogpio::Error`.

## Simple LED/button test

For an LED, connect Uno pin 13 through a resistor (about 220-1k ohm) to the LED
anode and connect its cathode to GND. For a button, connect pin 7 to GND and
use an external pull-up resistor to 5 V (or modify the firmware/library
design deliberately to support an internal pull-up). Keep every signal within
the Uno voltage/current limits; read `SAFETY.md` before connecting external
hardware.

## Integrate into another C++ project

Copy `UnoGPIO/UnoGPIO.h` and `UnoGPIO/UnoGPIO.cpp` into the project, add the
source to the Windows target, and add the UnoGPIO directory to the include
paths. Link against the normal Windows system libraries used by the compiler;
the implementation uses Win32 serial APIs and has no Python or Arduino
dependency on the PC.

Example:

```cpp
#include "UnoGPIO.h"
unogpio::UnoGPIO gpio("COM3");
gpio.setOutput(13);
gpio.write(13, true);
gpio.setInput(7);
bool pressed = gpio.read(7);
```

## Test strategy

The protocol can be tested without hardware by feeding byte vectors to a small
host-side parser test harness (or a serial loopback/mock transport): valid
frames must produce ACK/read responses; bad CRC, invalid length, unknown
commands, and garbage before a valid start byte must be rejected. Also test
sequence mismatches, partial reads, disconnects/reconnects, host timeouts,
invalid pin numbers, rapid repeated commands, and sustained 1000 Hz traffic.
On hardware, verify startup inputs, the 500 ms watchdog LOW behavior, parser
recovery after corrupted bytes, and reconnect after unplugging/replugging USB.
An automated test matrix should include 100/250/500/1000 Hz command rates and
measure observed latency/jitter rather than assuming real-time behavior.
