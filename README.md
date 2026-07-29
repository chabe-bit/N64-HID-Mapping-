# N64 HID Mapping

A small Win32 Raw Input / HID test program for reading an N64 controller through a USB adapter and mapping its HID input into a simple N64-style gamepad state.

The program registers for HID gamepad/joystick input, receives `WM_INPUT` messages, parses HID reports with the Windows HID parser APIs, and exposes button pressed/released/down state for N64 controls.

## Features

- Win32 Raw Input device registration
- HID gamepad / joystick input handling
- HID preparsed data and capability parsing
- Button usage mapping for N64 controls
- Analog stick normalization
- D-pad hat switch handling
- Pressed / released button edge detection

## Mapped Controls

The current mapping targets an N64 controller layout:

- A
- B
- Z
- Start
- L / R
- C-Up / C-Down / C-Left / C-Right
- D-Pad
- Analog stick

Button usage values may differ depending on the USB adapter. If your controller maps differently, update the `N64_GAMEPAD_*` constants in `main.c`.
