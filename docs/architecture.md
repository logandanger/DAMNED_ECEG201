# Architecture

## Layout

| Path | Role | May depend on |
|---|---|---|
| `core/` | Engine, menu, view, mapping, mirror protocol | `hal/hal.hpp`, LVGL |
| `hal/hal.hpp`, `hal/controls.hpp` | Interface | nothing |
| `hal/esp32/` | I2S, display, LEDs, buttons, I2C | ESP-IDF |
| `hal/sim/` | SDL audio, framebuffer, input injection | SDL2 |
| `firmware/` | `app_main`, tasks, mirror sender | all of the above |
| `sim/` | Replica, keyboard, serial client | all of the above |

`core/` contains no hardware `#ifdef`s. The HAL is resolved at link time.

## Tasks

| Task | Core | Priority | Rate | Job |
|---|---|---|---|---|
| audio | 1 | MAX-2 | 2.9 ms | `Engine::render`, then a blocking I2S write |
| input | 0 | 10 | 1 ms | Buttons, keys, encoders |
| ui | 0 | 5 | 5 ms | LVGL, menu, LEDs |
| mirror | 0 | 2 | 50 ms | Optional framebuffer stream |

All are pinned, which ESP-IDF requires for tasks that use the FPU.

Data crosses tasks without locks:

- **Notes:** input task to audio, through a single-producer single-consumer ring.
- **Parameters:** UI to audio, through `std::atomic<float>` plus a generation counter. The audio task recomputes coefficients only when the counter moves.
- **Actions:** input task to UI, through a second ring.
- **Scope, peak, voice mask:** audio to UI, through relaxed atomics. Tearing in the scope is harmless.

`static_assert`s in `engine.hpp` prove these atomics are lock-free on both targets. A test counts heap allocations across every preset and requires none during `render`.

## Signal path

```
4 x [ oscillator -> SVF -> amp envelope ]  mono sum
  -> drive / bitcrush  -> chorus / phaser (mono to stereo)  -> delay  -> reverb  -> volume  -> soft clip  -> int16
```

- **Oscillators:** sine from a 1024-entry table; saw and square with polyBLEP; naive triangle, whose harmonics fall at 12 dB/oct; white from xorshift32; pink from Paul Kellet's filter.
- **Filter:** trapezoidal state-variable filter, per voice, $g = \tan(\pi f_c / f_s)$, $Q$ from 0.5 to 25.
- **Stealing:** same note, then idle, then quietest releasing, then oldest held. The envelope continues from its current level, so a steal does not click.
- **Soft clip:** $y = x - \frac{4}{27}x^3$ for $|x| \le 1.5$: unity slope at 0, reaching $\pm 1$ with zero slope.

## Fixed sizes

| Quantity | Value | Time |
|---|---|---|
| Sample rate | 44,100 Hz | |
| Audio block | 128 frames | 2.90 ms |
| Control block | 32 frames | 0.73 ms |
| I2S DMA | 3 x 128 frames | 8.7 ms |

## CPU budget

One core gives $240 \times 10^6 / 44{,}100 = 5{,}442$ cycles per stereo sample. Target: under 60 %, or 3,265 cycles.

**None of the figures below were measured on an ESP32-S3.** No hardware was available. They are rough estimates, scaled from the size of the compiled Xtensa functions rather than counted per loop, with these assumptions:

- 2 cycles per instruction on average, allowing for FPU latency and branches.
- Hot code stays in the instruction cache, and no flash writes happen during play.
- Worst case: 4 square voices with the filter envelope, plus drive, phaser, delay and reverb all on.

| Block | Instructions per sample | Cycles per sample |
|---|---|---|
| 4 voices (oscillator, filter, envelope) | 380 | 760 |
| Coefficients: `tanf` and `exp2f` per voice per control block | | 190 |
| Drive | 10 | 20 |
| Phaser, 2 x 4 allpasses | 60 | 120 |
| Delay | 50 | 100 |
| Reverb | 100 | 200 |
| Output, scope | 30 | 60 |
| **Total** | **630** | **1,450, about 27 %** |

On the host the same worst case takes 7.5 µs per block, 0.26 % of real time (`danger_bench`). That shows the relative cost of each stage, not device speed.

The status line shows real load on the device (`CPU n %`, from the cycle counter), so this table can be replaced after first boot. If the load is high, the first steps are to run the reverb at half rate, move hot functions to IRAM, or update coefficients every 64 frames.

## Memory

From the linker, `danger` build:

| Item | Bytes |
|---|---|
| Static RAM total | 171,904 of 327,680 |
| of which `App` (engine, delay 45,056, reverb about 26,000) | 80,484 |
| of which LVGL heap | 32,768 |
| of which LVGL draw buffer | 25,600 |
| Task stacks, from the heap | 36,864 |
| Mirror shadow, from the heap, 15 x 10,240 | 153,600 |
| Flash | 519,367 of 3,145,728 |

**RAM is the tightest resource.** The total comes to about 362 kB, which only fits if the heap regions beyond the linker's segment are available as expected. That has not been confirmed on hardware. If it does not fit, the mirror turns itself off and logs the reason; audio, UI and input keep running. Lowering `kDrawLines` or `LV_MEM_SIZE`, or a module with PSRAM, would recover it.

## Not done yet

| Item | State |
|---|---|
| TLV320DAC3100 driver | Out of scope. TODOs in `hal/esp32/hal_audio.cpp`. **The box is silent until it exists.** |
| Hardware run | Firmware compiles and links, but has never run. Panel orientation, encoder direction and key pins are marked `[verify]`. |
| Key pull-ups | The AW9523 has none. Each key line needs an external resistor. |
| Patch saving | Presets are read-only. |
| 32-LED strip | Core renders 32 cells; the board drives the first 20. |
