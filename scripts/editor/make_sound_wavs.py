#!/usr/bin/env python3
"""Synthesises the game's sounds as 16-bit mono WAV files. Standard library only, so it runs
both on its own and inside the editor's Python (see make_sound_assets.py).

    scripts/editor/make_sound_wavs.py <output directory>

Loops are made seamless by cross-fading their tail into their head, and anything that
modulates them (gusts, gurgle) repeats a whole number of times over the loop.
"""
import math
import random
import struct
import sys
import wave

RATE = 44100


def noise(count, seed):
    rng = random.Random(seed)
    return [rng.uniform(-1.0, 1.0) for _ in range(count)]


def lowpass(samples, cutoff_hz):
    alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff_hz / RATE)
    out, state = [], 0.0
    for sample in samples:
        state += alpha * (sample - state)
        out.append(state)
    return out


def highpass(samples, cutoff_hz):
    low = lowpass(samples, cutoff_hz)
    return [sample - l for sample, l in zip(samples, low)]


def bandpass(samples, low_hz, high_hz, order=2):
    out = samples
    for _ in range(order):
        out = lowpass(highpass(out, low_hz), high_hz)
    return out


def normalise(samples, peak=0.85):
    top = max(abs(s) for s in samples) or 1.0
    return [s * peak / top for s in samples]


def make_loop(samples, loop_count, fade_count):
    """Folds the extra fade_count samples at the end into the start so the loop has no click."""
    out = samples[:loop_count]
    for index in range(fade_count):
        t = index / fade_count
        # Equal-power cross-fade
        out[index] = samples[loop_count + index] * math.cos(t * math.pi / 2.0) + samples[index] * math.sin(t * math.pi / 2.0)
    return out


def write_wav(path, samples):
    with wave.open(path, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in samples))


def wind_loop(seconds=6.0):
    """Wind past the ears: broad low rush with a whistling band on top, breathing slowly."""
    loop, fade = int(seconds * RATE), int(0.5 * RATE)
    count = loop + fade
    rush = bandpass(noise(count, 11), 90.0, 900.0)
    whistle = bandpass(noise(count, 12), 1400.0, 3200.0)
    out = []
    for index in range(count):
        t = index / RATE
        breath = 0.8 + 0.2 * math.sin(2.0 * math.pi * t / seconds * 2.0) * math.sin(2.0 * math.pi * t / seconds * 3.0 + 1.0)
        out.append((rush[index] * 1.0 + whistle[index] * 0.35) * breath)
    return normalise(make_loop(out, loop, fade), 0.8)


def water_loop(seconds=4.0):
    """The board cutting through water: bright hiss with a gurgle underneath."""
    loop, fade = int(seconds * RATE), int(0.4 * RATE)
    count = loop + fade
    hiss = bandpass(noise(count, 21), 1800.0, 7000.0)
    body = bandpass(noise(count, 22), 250.0, 1200.0)
    gurgle = lowpass([abs(s) for s in noise(count, 23)], 9.0)
    top = max(gurgle) or 1.0
    out = []
    for index in range(count):
        wobble = 0.55 + 0.45 * gurgle[index] / top
        out.append(hiss[index] * 0.8 * wobble + body[index] * 0.9 * (1.3 - wobble))
    return normalise(make_loop(out, loop, fade), 0.8)


def line_loop(seconds=3.0):
    """Loaded lines singing in the wind: a narrow band of noise around a few related pitches."""
    loop, fade = int(seconds * RATE), int(0.3 * RATE)
    count = loop + fade
    out = [0.0] * count
    for pitch, level, seed in ((220.0, 1.0, 31), (331.0, 0.6, 32), (447.0, 0.35, 33)):
        band = bandpass(noise(count, seed), pitch * 0.94, pitch * 1.06, order=3)
        band = normalise(band, 1.0)
        for index in range(count):
            out[index] += band[index] * level
    return normalise(make_loop(out, loop, fade), 0.7)


def splash(seconds, seed, thump_hz, brightness_hz, attack=0.004):
    """A burst of water: a low thump under spray that darkens as it dies away."""
    count = int(seconds * RATE)
    spray = noise(count, seed)
    out, state = [], 0.0
    for index in range(count):
        t = index / RATE
        fraction = t / seconds
        cutoff = brightness_hz * (1.0 - 0.85 * fraction) + 300.0
        alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
        state += alpha * (spray[index] - state)
        envelope = min(t / attack, 1.0) * math.exp(-4.5 * fraction)
        thump = math.sin(2.0 * math.pi * thump_hz * t * (1.0 - 0.4 * fraction)) * math.exp(-14.0 * fraction)
        out.append(state * envelope + thump * 0.8 * min(t / attack, 1.0))
    tail = int(0.02 * RATE)
    for index in range(tail):
        out[count - 1 - index] *= index / tail
    return normalise(out, 0.9)


def reset_cue(seconds=0.35):
    """Two soft rising blips."""
    count = int(seconds * RATE)
    out = []
    for index in range(count):
        t = index / RATE
        first = math.sin(2.0 * math.pi * 520.0 * t) * math.exp(-18.0 * t)
        late = max(t - 0.13, 0.0)
        second = math.sin(2.0 * math.pi * 780.0 * late) * math.exp(-16.0 * late) if t >= 0.13 else 0.0
        out.append(0.6 * first + 0.6 * second)
    tail = int(0.02 * RATE)
    for index in range(tail):
        out[count - 1 - index] *= index / tail
    return normalise(out, 0.6)


SOUNDS = {
    "SW_WindLoop": wind_loop,
    "SW_WaterLoop": water_loop,
    "SW_LineLoop": line_loop,
    "SW_Pop": lambda: splash(0.35, 41, 150.0, 6000.0),
    "SW_Landing": lambda: splash(0.8, 42, 90.0, 5000.0),
    "SW_Crash": lambda: splash(1.6, 43, 60.0, 4000.0, attack=0.008),
    "SW_ResetCue": reset_cue,
}


# Loops, which the game keeps playing and fades up and down, against one-shots.
LOOPS = ("SW_WindLoop", "SW_WaterLoop", "SW_LineLoop")

# A sound quieter than this was not synthesised properly.
MIN_RMS = 0.05


def generate_all(out_dir):
    """Writes every sound to out_dir and returns {asset name: wav path}."""
    import os
    os.makedirs(out_dir, exist_ok=True)
    paths = {}
    for name, make in SOUNDS.items():
        samples = make()
        rms = math.sqrt(sum(s * s for s in samples) / len(samples))
        if rms < MIN_RMS:
            raise RuntimeError(f"{name} came out silent (rms {rms:.4f})")
        path = os.path.join(out_dir, f"{name}.wav")
        write_wav(path, samples)
        paths[name] = path
        print(f"{name}: {len(samples) / RATE:.2f} s, peak {max(abs(s) for s in samples):.2f}, rms {rms:.3f}")
    return paths


if __name__ == "__main__":
    generate_all(sys.argv[1] if len(sys.argv) > 1 else ".")
