#!/usr/bin/env python3
"""Render the placeholder sample set into samples/ (offline only; the device never synthesizes).

Writes 22,050 Hz mono 16-bit WAVs and samples/manifest.csv. Replace any WAV with a real
recording, keep or edit its manifest row, then run tools/pack_samples.py.

    python tools/make_samples.py

Needs numpy.
"""
import csv
import wave
from pathlib import Path

import numpy as np

RATE = 22050
REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "samples"
rng = np.random.default_rng(201)


def t_axis(seconds):
    return np.arange(int(seconds * RATE)) / RATE


def hz(note):
    return 440.0 * 2 ** ((note - 69) / 12)


def lowpass(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / RATE)
    y = np.empty_like(x)
    s = 0.0
    for i, v in enumerate(x):
        s = (1 - a) * v + a * s
        y[i] = s
    return y


def finish(x, peak=0.89, tail=0.01):
    n = int(tail * RATE)
    x = x.copy()
    x[-n:] *= np.linspace(1, 0, n)  # every one-shot ends at exactly zero
    x[:16] *= np.linspace(0, 1, 16)
    return x / np.max(np.abs(x)) * peak


# ---- Drums ----------------------------------------------------------------------------------
def kick():
    t = t_axis(0.5)
    f = 48 + 110 * np.exp(-t / 0.035)
    phase = 2 * np.pi * np.cumsum(f) / RATE
    body = np.sin(phase) * np.exp(-t / 0.16)
    click = lowpass(rng.standard_normal(len(t)), 3000) * np.exp(-t / 0.004) * 0.6
    return finish(np.tanh(1.6 * (body + click)))


def snare():
    t = t_axis(0.35)
    tone = (np.sin(2 * np.pi * 185 * t) + 0.5 * np.sin(2 * np.pi * 330 * t)) * np.exp(-t / 0.05)
    noise = rng.standard_normal(len(t))
    noise = noise - lowpass(noise, 1200)
    noise = lowpass(noise, 9000) * np.exp(-t / 0.09)
    return finish(0.55 * tone + noise)


def metal(seconds, decay, cutoff):
    t = t_axis(seconds)
    ratios = [2.0, 3.0, 4.16, 5.43, 6.79, 8.21]
    sq = sum(np.sign(np.sin(2 * np.pi * 300 * r * t + rng.uniform(0, 6.28))) for r in ratios)
    sq = sq + 0.6 * rng.standard_normal(len(t))
    hp = sq - lowpass(sq, cutoff)
    return hp * np.exp(-t / decay)


def hat():
    return finish(metal(0.12, 0.025, 7000), peak=0.6)


def crash():
    t = t_axis(1.6)
    x = metal(1.6, 0.55, 5000) + 0.5 * metal(1.6, 0.9, 3000)
    return finish(x * (1 - np.exp(-t / 0.002)), peak=0.6)


# ---- Instruments, one root note each -------------------------------------------------------
def additive(note, seconds, partials, attack=0.003):
    t = t_axis(seconds)
    f0 = hz(note)
    x = np.zeros_like(t)
    for ratio, amp, decay in partials:
        if f0 * ratio < RATE / 2 * 0.9:
            x += amp * np.sin(2 * np.pi * f0 * ratio * t) * np.exp(-t / decay)
    return x * np.minimum(1, t / attack)


def keys():
    return finish(additive(60, 1.6, [(1, 1, 0.9), (2, 0.5, 0.5), (3, 0.25, 0.3), (4, 0.15, 0.2), (7, 0.05, 0.08)]))


def pluck():  # Karplus-Strong
    n = int(1.2 * RATE)
    period = int(round(RATE / hz(60)))
    buf = rng.uniform(-1, 1, period)
    out = np.empty(n)
    for i in range(n):
        out[i] = buf[i % period]
        buf[i % period] = 0.996 * 0.5 * (buf[i % period] + buf[(i + 1) % period])
    return finish(lowpass(out, 5000))


def bass():
    t = t_axis(1.2)
    f0 = hz(36)
    x = sum((1 / k) * np.sin(2 * np.pi * f0 * k * t) for k in range(1, 12))
    x = lowpass(x, 600) * np.exp(-t / 0.6)
    return finish(np.tanh(2 * x) * np.minimum(1, t / 0.004))


def bell():
    return finish(additive(72, 2.0, [(1, 1, 1.2), (2.76, 0.6, 0.6), (5.4, 0.4, 0.3), (8.93, 0.25, 0.15)], 0.001))


def marimba():
    return finish(additive(60, 0.8, [(1, 1, 0.35), (4, 0.35, 0.05), (10, 0.1, 0.02)], 0.001))


def pad():
    """Sustained: returns the samples and a loop region with a crossfade baked in."""
    seconds = 2.5
    t = t_axis(seconds)
    f0 = hz(60)
    x = np.zeros_like(t)
    for det in (-0.08, 0.0, 0.07):
        for k in range(1, 8):
            x += (0.6 ** k) * np.sin(2 * np.pi * f0 * k * (1 + det / 100) * t + rng.uniform(0, 6.28))
    x *= np.minimum(1, t / 0.25)
    start, end = int(0.8 * RATE), int(2.3 * RATE)
    fade = int(0.3 * RATE)
    w = np.linspace(0, 1, fade)
    # The end of the loop blends into the material just before its start, so the jump is seamless.
    x[end - fade:end] = x[end - fade:end] * (1 - w) + x[start - fade:start] * w
    x = x[:end]
    x = x / np.max(np.abs(x)) * 0.8
    return x, start, end


def write(name, x):
    data = (np.clip(x, -1, 1) * 32767).astype("<i2").tobytes()
    with wave.open(str(OUT / f"{name}.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data)


def main():
    OUT.mkdir(exist_ok=True)
    rows = []
    # kind, name, root note, release in s
    for name, fn in (("kick", kick), ("snare", snare), ("hat", hat), ("crash", crash)):
        write(name, fn())
        rows.append({"name": name, "kind": "drum", "file": f"{name}.wav", "root": 60, "loop_start": 0, "loop_end": 0,
                     "release_s": 0.02})
    for name, fn, root, rel in (("keys", keys, 60, 0.25), ("pluck", pluck, 60, 0.15), ("bass", bass, 36, 0.12),
                                ("bell", bell, 72, 0.6), ("marimba", marimba, 60, 0.1)):
        write(name, fn())
        rows.append({"name": name, "kind": "inst", "file": f"{name}.wav", "root": root, "loop_start": 0, "loop_end": 0,
                     "release_s": rel})
    x, ls, le = pad()
    write("pad", x)
    rows.append({"name": "pad", "kind": "inst", "file": "pad.wav", "root": 60, "loop_start": ls, "loop_end": le,
                 "release_s": 0.6})
    with open(OUT / "manifest.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"wrote {len(rows)} samples to {OUT}")


if __name__ == "__main__":
    main()
