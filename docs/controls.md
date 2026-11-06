# Controls

**Status: proposal, awaiting approval.** The default mapping (section 1) is active. The proposed additions (section 2) are in the config file but switched off.

All mappings live in one file: [`core/config/control_map.hpp`](../core/config/control_map.hpp). To approve section 2, set `kUseProposedMapping = true` and rebuild.

## 1. Default mapping (active)

This is the behaviour you specified, plus nothing else.

| Control | Action | Computer keyboard |
|---|---|---|
| Key 1 to 12 | Play C to B, velocity fixed at 100 | `A W S E D F T G Y H U J` |
| Up / Down | Move highlight | Arrow keys |
| Left / Right | Previous / next parameter group | Arrow keys |
| A | Enter page, load preset, or cycle an enum | `X` or `Return` |
| B (tap) | Back out of a page | `Z` or `Backspace` |
| B + Up / Down | Octave up / down, range $\pm 3$ | `Z` + arrow keys |
| Encoder 1 turn | Value, coarse | `K` / `L` |
| Encoder 2 turn | Value, fine | `,` / `.` |
| Encoder 1 or 2 push | Reset value to default | `;` or `/` |

Without section 2, oscillator and preset selection go through the menus:

- **Oscillator:** OSC group, Wave row. Turn either encoder, or press A to cycle.
- **Preset:** GLB group, Preset row, A to open, Up/Down, A to load, B to leave.

Mouse on the replica: click keys and buttons, drag along the keyboard to glide. Drag an encoder vertically or scroll over it to turn; click it to push. **Right-click a button to latch it**, which allows B + Up with one pointer.

## 2. Proposed additions (off until approved)

| Control | Action | Why |
|---|---|---|
| B + Left / Right | Previous / next preset | Changing sound is the most common action while playing. The menu route takes 5 presses or more. |
| B + A | Next oscillator wave | Same reason. B and A sit together, so one hand does it and the other keeps playing. |
| B + Encoder 1 turn | Volume, from any page | Volume should never need navigation. |
| B + Encoder 2 turn | Filter cutoff, from any page | The one parameter worth having as a live performance control. |
| B + Encoder 1 push | All notes off | Panic. A chord, so it cannot happen by accident. |

The pattern: **B is the shift key.** Alone it is Back; held, every other control gets a second, performance-oriented function. Nothing in section 1 changes.

## 3. Where parameters sit

Left/Right walks the five groups, wrapping at the ends. Effects has one level of sub-pages, which is what gives A and B something to do.

| Group | Rows |
|---|---|
| OSC | Wave, Pitch, Fine, Pulse width, Level |
| FLT | Type, Cutoff, Resonance, Env amount, Key track |
| ENV | Attack, Decay, Sustain, Release, then Flt attack, Flt decay, Flt sustain, Flt release |
| FX | Drive / crush, Chorus / phaser, Delay, Reverb (each opens a page) |
| GLB | Preset, Volume, Octave, LED level |

Order follows the signal path, so Left/Right moves along the sound from source to output. Global values survive preset changes.

Step sizes: Encoder 1 moves continuous values $\frac{1}{25}$ of their range per detent, Encoder 2 $\frac{1}{250}$. Cutoff and times step on a logarithmic scale. Pitch moves 12 st or 1 st; Fine moves 10 ct or 1 ct.

## 4. Conflicts and awkward ergonomics

| # | Issue | Handling |
|---|---|---|
| 1 | **B is both Back and a modifier.** | Back fires on release, and only if nothing else was touched while B was held. Back therefore lands on release, not press. A press-and-hold with no chord still counts as Back. |
| 2 | **Octave shift takes both hands.** The d-pad is far left and B is on the right, so playing stops during a shift. | This is the biggest ergonomic cost of the layout. Alternative for your decision: use `B + Encoder 2 turn` for octave rather than cutoff, which is one-handed on the right. |
| 3 | **Order matters in chords.** Up then B navigates once, then B is a plain tap. | Intended: the modifier must go first. |
| 4 | **Encoder push can fire while turning.** Pressing down slightly while turning resets the value, with no undo. | Your spec says push resets. If it bites in practice, change to long press or B + push. Only hardware testing will tell. |
| 5 | **Both pushes do the same thing.** | As specified, but it uses up two controls. Encoder 2 push could become something else later. |
| 6 | **Left/Right inside a sub-page** leaves it for the next group. | Chosen so that Left/Right always means the same thing. B returns to the parent with the cursor kept. |
| 7 | **Encoders on an enum row** stop at the ends; A wraps. | Intended. |
| 8 | **One octave of keys** has no high C. | Inherent to 12 keys. Octave shift covers it. |
| 9 | **Keys are never blocked by B.** | Intended: chords must not interrupt playing. A key held across an octave shift releases the note it started. |
| 10 | **PC keyboards ghost.** Some cannot report 3 or more keys in certain combinations. | Hardware limit of the keyboard. Use the mouse or try another chord. |

## 5. Decisions needed

1. Approve section 2, with or without changes.
2. Conflict 2: keep B + Encoder 2 for cutoff, or use it for octave?
3. The sketch shows two circles above the d-pad that nothing in the README accounts for. The replica omits them. Are they controls?
