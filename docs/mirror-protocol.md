# Mirror protocol

Streams the device's screen to `danger_sim --mirror <port>` over USB CDC. Version 1. Code: `core/mirror/` (shared), `firmware/main/mirror_task.cpp` (device), `sim/mirror_client.cpp` (host). Compile it out with `pio run -e danger-nomirror`.

## Link

- ESP32-S3 USB-Serial-JTAG CDC port, the same one as flashing and logs. No TinyUSB dependency.
- The host holds DTR and RTS deasserted, because they drive reset on this peripheral. Opening the port must not reboot the box.
- Log text can appear between packets. The parser skips it.

## Session

The device sends nothing until asked, so an idle USB cable costs nothing.

| Host to device (4 ASCII bytes) | Meaning |
|---|---|
| `DMFF` | Start or continue a session and resend the full screen |
| `DMKA` | Keepalive |

The host sends `DMFF` on connect and after any error, and `DMKA` every 1 s. The session ends 3 s after the last command.

## Packets, device to host

All fields are little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Magic `D5 4D` |
| 2 | 1 | Type: 1 Hello, 2 Rect, 3 FrameEnd |
| 3 | 1 | Flags. Rect: bit 0 = RLE. Hello: protocol version |
| 4 | 2 | Sequence, +1 per packet |
| 6 | 8 | x, y, w, h (Hello: w and h are the screen size) |
| 14 | 2 | Payload length |
| 16 | 2 | CRC-16/CCITT-FALSE of bytes 0 to 15 |
| 18 | n | Payload |
| 18+n | 2 | CRC-16/CCITT-FALSE of the payload |

The header has its own CRC so that a corrupted length cannot make the parser swallow good packets. Rect payloads are RGB565 in row-major order, limited to 4096 pixels (8 KiB raw).

The host copies Rect packets into a back buffer and shows it on FrameEnd, so frames never tear.

## Compression

PackBits over 16-bit pixels, used only when it is smaller than raw. Control byte $c$:

- $c < 128$: $c + 1$ literal pixels follow.
- $c \ge 128$: one pixel follows, repeated $c - 125$ times (3 to 130).

## Dirty rectangles

The sender tracks 16 x 16 px tiles, 20 x 15 in all, as one 32-bit mask per tile row. LVGL flushes copy into a shadow framebuffer and set tile bits. Every 50 ms the sender takes the bits, merges runs along each tile row into rectangles, and sends them with a FrameEnd.

Because it works from a shadow copy, a stalled link never blocks the UI. Updates coalesce, and the screen catches up with its newest state. A short write re-marks the unsent tiles.

Measured from the test suite:

| Update | Size on the wire |
|---|---|
| Full noisy frame, the worst case | 154,240 B |
| Flat full frame | 2,420 B |
| 10 x 5 px change | 474 B |

## Errors

CRC failure or a sequence gap makes the host request a full frame. Bytes that are not a valid packet are skipped.

## Isolation from the box

| Concern | Mechanism |
|---|---|
| Audio | Mirror runs on core 0, audio on core 1 at far higher priority |
| Keys and encoders | Input task has priority 10; mirror has 2 |
| UI | Only cost is a `memcpy` of flushed pixels; no waiting on USB |
| RAM | 153,600 B shadow, as 15 separate bands of 10,240 B because no single block that large remains free. If allocation fails, the mirror stays off, logs why, and the rest runs normally |

## Limits

- USB-MIDI (README section 11) needs TinyUSB, which takes over the USB PHY from USB-Serial-JTAG. If MIDI arrives, the mirror has to move to a TinyUSB CDC interface. Only `usb_write` and `poll_commands` change.
- Not yet run against real hardware. The host path has been run end to end from a recording made with `--mirror-record`; the serial link itself has not.
