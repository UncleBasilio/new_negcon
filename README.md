# neGcon-style USB HID Gamepad for ESP32-S3 (Zephyr)

A demo/learning project: a USB HID gamepad inspired by the classic Namco **neGcon**
controller, running on an **ESP32-S3** under **Zephyr 4.5** (new USB device stack,
`USB_DEVICE_STACK_NEXT`), written in **C++20**.

The firmware exposes the same control set as the original neGcon over USB:

| Control | Type | HID mapping |
|---|---|---|
| Twist (two halves rotation) | analog, center = 0 | axis **X** |
| Button I | analog 0..127 | axis **Y** |
| Button II | analog 0..127 | axis **Z** |
| Shoulder L | analog 0..127 | axis **Rx** |
| D-pad (4) + Start, A, B, R | digital | 8 buttons (byte 0) |

Plug it into a PC and it enumerates as a standard HID game controller — no drivers,
ready for emulators (DuckStation etc.) and `joy.cpl`.

## Project status

The USB side is **complete and tested**: enumeration, HID report descriptor,
asynchronous report submission, `GET_REPORT` control handling — all verified on
real hardware and Windows (`joy.cpl` shows live axes and buttons).
Sensor reading (encoders, potentiometers, buttons) is intentionally left to the
student this project mentors.

## Architecture

The USB side is isolated in a single C++ class. The sensor code only ever sees
its public contract:

- **`src/negcon_hid.hpp`** — the interface:

```cpp
class NegconHid {
public:
    struct Input {
        uint8_t buttons;   // 8 digital buttons, bitmask
        int8_t  twist;     // twist axis, center = 0
        int8_t  btn_i;     // analog button I:  0 (released) .. 127
        int8_t  btn_ii;    // analog button II: 0 .. 127
        int8_t  btn_l;     // analog shoulder L: 0 .. 127
    };

    int  init();                      // register HID device, enable USB
    int  send(const Input &in);       // 0 = accepted, -EBUSY = previous
                                      // report still in flight; the caller
                                      // decides: retry, spin, or drop
    void counters(uint32_t &posted, uint32_t &done) const;  // diagnostics
};
```

- **`src/negcon_hid.cpp`** — the internals:
  - HID report descriptor (48 bytes) built by a `consteval` function — the
    descriptor is computed at compile time; the layout matches `Input`
    byte-for-byte (`static_assert` guards this), so submission is a `memcpy`.
  - `hid_device_ops` callbacks: `iface_ready`, `input_report_done`, and the
    **mandatory** `get_report` (the Zephyr HID class refuses to initialize
    without it; it answers the host's `GET_REPORT` control request with the
    last submitted state).
  - **Try-send** instead of a blocking queue or silent overwrite: two atomic
    32-bit counters, `posted` and `done`. "Busy" is simply `posted != done`.
    `send()` increments `posted` *before* submitting (and rolls back on
    failure), the USB stack thread increments `done` in `input_report_done`.
    A report in flight is never overwritten — no torn frames on the wire.

- **`src/main.cpp`** — a demo generator (sawtooth on all axes, cycling buttons)
  that stands in for the student's sensor code.

The report submission runs fully asynchronously: the main loop never waits for
the USB stack; the class just tells it whether the channel accepted the report.

## Building

Created from the Zephyr `samples/subsys/usb/hid-mouse` sample
(Workbench for Zephyr). Target board: `esp32s3_devkitc/esp32s3/procpu`
(its devicetree already enables `zephyr_udc0`, no overlay needed).

C++ support in `prj.conf` (note the actual Kconfig names — `CONFIG_CPLUSPLUS`
does not exist):

```conf
CONFIG_CPP=y
CONFIG_STD_CPP20=y
CONFIG_GLIBCXX_LIBCPP=y   # works with picolibc, which ESP32-S3 forces
```

```sh
west build -b esp32s3_devkitc/esp32s3/procpu
west flash
```

Then open `joy.cpl` (Windows) or your favorite emulator and watch the axes move.

## Windows pitfall: MAX_PATH vs. Zephyr SDK (worth a read)

If the build fails with a bizarre

```
bits/os_defines.h: No such file or directory
```

while the file demonstrably exists and sits in the include search path — you
have hit **MAX_PATH**, not a missing file:

- GCC selects its multilib variant by optimization flags; `-Os` (Zephyr's
  default) selects the `space` variant
  (`gcc -print-multi-lib` shows `space;@Os`), adding `space/` to the
  C++ header paths.
- GCC on Windows does not normalize `/../` segments in include paths, so the
  effective path keeps every parent-directory hop.
- With a deep SDK install path, `bits/os_defines.h` ends up at exactly
  **260 characters** — one over the 259 usable limit — while the neighboring
  `c++config.h` at 259 opens fine. Same directory, one-character difference.

Fixes: shorten the SDK path (e.g. install to `C:\sdk` or create a
`mklink /J` junction and point Workbench at it). Enabling
`LongPathsEnabled` in the registry/group policy does **not** help GCC —
it has no `longPathAware` manifest.

## TODO / ideas

- Descriptor variant "B": merged I/II single axis (like commercial PSX→USB
  adapters do), selectable via a Kconfig option
- Custom USB product string ("neGcon" instead of "USBD sample")
- Optional: status indication on the onboard WS2812 (GPIO48) via `led_strip`
  — the devkit has no plain LED, hence no `led0` alias

## References

- [atty303/negcon-usbhid](https://github.com/atty303/negcon-usbhid) — open-source
  neGcon→USB adapter (AVR + V-USB); the descriptor layout used as a reference
- USB HID Usage Tables (usb.org) — usage pages and axis usages
- Zephyr `include/zephyr/usb/class/hid.h` and `usbd_hid.h` — when in doubt,
  trust the header (and the compiler's "expected type" diagnostics), not memory

## Credits

This project was designed and written with the active participation of
**Kimi (Moonshot AI)**: protocol research, HID descriptor design, Zephyr USB
stack integration, the C++ class rework, and debugging — including the MAX_PATH
investigation above — were done in collaborative chat sessions, then verified
on real hardware by a human.
