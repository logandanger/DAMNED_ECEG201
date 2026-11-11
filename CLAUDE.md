# CLAUDE.md

Project: interactive OS and desktop simulator for the DANGER synth, prototype #1, on the Bucknell ECEG 201 DAMNED board (ESP32-S3) plus Adafruit I2C breakouts.

Read `README.md` first. It documents the whole circuit (power, pin map, display, buttons, I2C, audio, LEDs, USB, CAN, risks). Do not ask for the KiCad files again; they are in `hardware/kicad/`. Machine-readable netlist: `hardware/netlist/netlist.json`, ESP32 pin map: `hardware/netlist/esp32_pinmap.csv`. Sketch: `docs/prototype1-sketch.jpg`.

Decisions:
- Stack: C++, PlatformIO + ESP-IDF, LVGL at 320x240, SDL2 desktop simulator sharing `core/` with firmware via a `hal/` layer.
- Prototype #1: 12 plain on/off keys via AW9523 (no velocity yet, but key events carry a velocity field), 2 I2C encoders, 6 on-board buttons (hardware-debounced, active-low), TLV320DAC3100 audio (PAM8302 dropped), USB-C power only.
- Sound engine: generative beat maker on preloaded samples (no synthesis on the device). Behavior and control map: README section 2. Tunables: `core/config/beat_config.hpp`.
- Simulator is an on-screen replica with the same number of controls; the user maps functions.

User preferences: blunt and short answers, code without explanation unless asked, SI units, LaTeX equations, verify uncertain facts, minimal Swiss-style design.
