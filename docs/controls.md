# Controls

All mappings live in [`core/config/control_map.hpp`](../core/config/control_map.hpp). Timing values live in [`core/config/beat_config.hpp`](../core/config/beat_config.hpp).

## 1. Map

| Control | Action | Computer keyboard |
|---|---|---|
| Key1, Key3, Key6, Key8 (C# D# F# G#, the top-left 4) | Drum pads: kick, snare, hat, crash. Live over the loop, not captured. | `W E T Y` |
| Key0, 2, 4, 5, 7, 9, 10, 11 (C D E F G A A# B) | Current instrument, live and captured as the seed. Velocity from the key event (fixed 100 on prototype #1). | `A S D F G H U J` |
| A tap | Instrument cursor up | `X` or `Return` |
| A hold 0.6 s | Select the instrument under the cursor | hold `X` |
| B tap | Instrument cursor down | `Z` or `Backspace` |
| A + B released after 1 s | Clear current layer | `X` + `Z` |
| A + B held 3 s | Clear everything | `X` + `Z` |
| Left encoder | Reverb amount | `K` / `L` |
| Right encoder | BPM | `,` / `.` |
| D-pad, encoder pushes | Unassigned | |

Mouse on the replica: click keys and buttons. **Right-click a button to latch it**, which allows A + B with one pointer. Drag an encoder vertically or scroll over it to turn.

Melodic keys keep their chromatic pitch, so the 8 melodic keys give C D E F G A A# B. Change `kKeySemitone` to remap them.

## 2. A/B chord logic

State machine in [`core/ui/buttons.cpp`](../core/ui/buttons.cpp). Times from `beat_config.hpp`.

| State | Event | Result |
|---|---|---|
| Idle | A down | **A only**, timer starts |
| Idle | B down | **B only** |
| A only | A up before 0.6 s | **Up**, back to Idle |
| A only | 0.6 s reached | **Select**, fires while held, then Locked |
| B only | B up | **Down**, back to Idle |
| A only or B only | the other button down | **Chord**, timer restarts. The pending tap is dropped. |
| Chord | either button up before 1 s | nothing; Locked |
| Chord | either button up between 1 s and 3 s | **Clear layer**; Locked |
| Chord | 3 s reached | **Clear all**, fires while held; Locked |
| Locked | anything | nothing until both buttons are up, then Idle |

Why this cannot conflict:

- **Taps fire on release.** A chord is detected when the second button goes down, which is always before the first is released. So A + B never produces Up, Down or Select, whatever the skew between the two presses.
- **Hold actions are exclusive.** Select needs A alone; the clears need both. Once Select fires, B is ignored until both are up.
- **One action per press cycle.** After any action, Locked blocks everything until both buttons are released.
- **Debounce:** none in software. The on-board buttons go through the MC14490 hardware debouncer (README section 7), and the simulator's inputs are clean.

Cost: Up and Down land on release, not press. A held longer than 0.6 s selects instead of stepping.

**Which layer "clear layer" clears**, in order: the current instrument's layer, plus any pending seed. If the current instrument has no layer yet but a seed is pending, only that seed is dropped. Otherwise, the newest layer. The red mark on screen always shows the target.

## 3. Screen

| Area | Content |
|---|---|
| Head | Instrument (red, with "HOLD A TO SELECT", while the cursor is on another one), BPM, reverb % |
| Loop | 4 bars with beat ticks, red playhead, `BAR n.b / 4` |
| Layers | Oldest first: number, red mark on the clear target, instrument, `FADE IN n %` / `PLAYING notes` / `FADE OUT n %`, fade bar. Audio CPU load. |
| Status | Seed: notes captured, commit countdown with a shrinking red bar. A/B: hold progress; chord bar turns red past 1 s, with a tick at 1 s. |

## 4. Open points

1. The sketch shows two circles above the d-pad that nothing in the README accounts for. The replica omits them. Are they controls?
2. The d-pad and encoder pushes have no function in the beat maker.
