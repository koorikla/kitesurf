#!/usr/bin/env python3
"""Synthesises the game's sound effects as 16-bit WAV files. Standard library only, so it runs
both on its own and inside the editor's Python (see make_sound_assets.py).

    scripts/editor/make_sound_wavs.py <output directory>

Every sound is built from filtered noise and a few tones: there are no recordings. Loops are
made seamless by cross-fading their tail into their head, and whatever modulates them (gusts,
slaps, flutter) repeats a whole number of times over the loop. Stereo sounds are a (left,
right) pair of sample lists. The music is in make_music_wavs.py.
"""
import math
import os
import random
import struct
import sys
import wave

RATE = 44100


# ----------------------------------------------------------------------------------------------
# Building blocks
# ----------------------------------------------------------------------------------------------

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


def swept_bandpass(samples, centre_hz, q=3.0):
    """A resonant band whose centre moves: centre_hz(index) gives it for each sample."""
    out = []
    low = band = 0.0
    damping = 1.0 / q
    for index, sample in enumerate(samples):
        f = 2.0 * math.sin(math.pi * min(centre_hz(index), RATE / 6.5) / RATE)
        low += f * band
        high = sample - low - damping * band
        band += f * high
        out.append(band)
    return out


def normalise(samples, peak=0.85):
    top = max(abs(s) for s in samples) or 1.0
    return [s * peak / top for s in samples]


def normalise_stereo(pair, peak=0.85):
    top = max(max(abs(s) for s in pair[0]), max(abs(s) for s in pair[1])) or 1.0
    return ([s * peak / top for s in pair[0]], [s * peak / top for s in pair[1]])


def mix(*tracks):
    """Sums (samples, gain) pairs of equal length."""
    count = len(tracks[0][0])
    out = [0.0] * count
    for samples, gain in tracks:
        for index in range(count):
            out[index] += samples[index] * gain
    return out


def make_loop(samples, loop_count, fade_count):
    """Folds the extra fade_count samples at the end into the start so the loop has no click."""
    out = samples[:loop_count]
    for index in range(fade_count):
        t = index / fade_count
        # Equal-power cross-fade
        out[index] = samples[loop_count + index] * math.cos(t * math.pi / 2.0) + samples[index] * math.sin(t * math.pi / 2.0)
    return out


def fade_out(samples, seconds=0.02):
    tail = min(int(seconds * RATE), len(samples))
    for index in range(tail):
        samples[len(samples) - 1 - index] *= index / tail
    return samples


def periodic(index, loop_count, cycles, phase=0.0):
    """A sine that goes round a whole number of times over the loop."""
    return math.sin(2.0 * math.pi * (cycles * index / loop_count + phase))


def write_wav(path, samples):
    """samples: one list for mono, or a (left, right) pair for stereo."""
    stereo = isinstance(samples, tuple)
    channels = samples if stereo else (samples,)
    with wave.open(path, "wb") as wav:
        wav.setnchannels(len(channels))
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        frames = bytearray()
        for frame in zip(*channels):
            for sample in frame:
                frames += struct.pack("<h", int(max(-1.0, min(1.0, sample)) * 32767))
        wav.writeframes(bytes(frames))


# ----------------------------------------------------------------------------------------------
# Loops
# ----------------------------------------------------------------------------------------------

def wind_loop(seconds=8.0):
    """Wind past the ears, in stereo: a low rumble, a broad rush that breathes, and a thin
    whistle whose pitch wanders. Each ear gets its own rush, so it sounds wide."""
    loop, fade = int(seconds * RATE), int(0.6 * RATE)
    count = loop + fade
    rumble = lowpass(lowpass(noise(count, 11), 260.0), 260.0)
    whistle_source = noise(count, 14)
    sides = []
    for side, seed in enumerate((12, 13)):
        rush = bandpass(noise(count, seed), 220.0, 1500.0)
        whistle = swept_bandpass(
            whistle_source,
            lambda i, s=side: 2100.0 + 500.0 * periodic(i, loop, 3, 0.25 * s) + 260.0 * periodic(i, loop, 7, 0.1),
            q=6.0)
        out = []
        for index in range(count):
            breath = 0.78 + 0.22 * periodic(index, loop, 2, 0.3 * side) * periodic(index, loop, 5, 0.6)
            gust = 0.6 + 0.4 * max(periodic(index, loop, 1, 0.15 + 0.2 * side), 0.0)
            out.append(rumble[index] * 2.2 + rush[index] * breath + whistle[index] * 0.22 * gust)
        sides.append(make_loop(out, loop, fade))
    return normalise_stereo((sides[0], sides[1]), 0.8)


def water_loop(seconds=6.0):
    """The board cutting through water, in stereo: hiss and a deeper body that gurgle against
    each other, with the odd slap of chop against the hull."""
    loop, fade = int(seconds * RATE), int(0.5 * RATE)
    count = loop + fade
    rng = random.Random(27)
    slaps = [0.0] * count
    for _ in range(11):
        start = rng.randrange(0, loop)
        length = int(rng.uniform(0.05, 0.11) * RATE)
        level = rng.uniform(0.5, 1.0)
        tone = rng.uniform(140.0, 320.0)
        for offset in range(length):
            t = offset / RATE
            envelope = math.exp(-38.0 * t) * min(offset / 60.0, 1.0)
            slaps[(start + offset) % loop] += level * envelope * (math.sin(2.0 * math.pi * tone * t) + 0.5 * rng.uniform(-1.0, 1.0))
    sides = []
    for side, seed in enumerate((21, 24)):
        hiss = bandpass(noise(count, seed), 1800.0, 7500.0)
        body = bandpass(noise(count, seed + 1), 240.0, 1200.0)
        gurgle = lowpass([abs(s) for s in noise(count, seed + 2)], 9.0)
        top = max(gurgle) or 1.0
        out = []
        for index in range(count):
            wobble = 0.55 + 0.45 * gurgle[index] / top
            out.append(hiss[index] * 0.8 * wobble + body[index] * 0.9 * (1.3 - wobble))
        out = make_loop(out, loop, fade)
        for index in range(loop):
            out[index] += slaps[index] * (0.11 if side == 0 else 0.08)
        sides.append(out)
    return normalise_stereo((sides[0], sides[1]), 0.8)


def spray_loop(seconds=3.0):
    """Spray thrown off a hard edge: bright, fizzing, a little uneven."""
    loop, fade = int(seconds * RATE), int(0.3 * RATE)
    count = loop + fade
    fizz = bandpass(noise(count, 51), 3200.0, 9500.0)
    grain = lowpass([abs(s) for s in noise(count, 52)], 22.0)
    top = max(grain) or 1.0
    out = [fizz[i] * (0.5 + 0.5 * grain[i] / top) for i in range(count)]
    return normalise(make_loop(out, loop, fade), 0.75)


def line_loop(seconds=3.0):
    """Loaded lines singing in the wind: narrow bands of noise round a few related pitches."""
    loop, fade = int(seconds * RATE), int(0.3 * RATE)
    count = loop + fade
    out = [0.0] * count
    for pitch, level, seed in ((220.0, 1.0, 31), (331.0, 0.6, 32), (447.0, 0.35, 33), (663.0, 0.18, 34)):
        band = normalise(bandpass(noise(count, seed), pitch * 0.95, pitch * 1.05, order=3), 1.0)
        for index in range(count):
            out[index] += band[index] * level
    return normalise(make_loop(out, loop, fade), 0.7)


def kite_whoosh_loop(seconds=4.0):
    """The kite itself moving through the air: a low, soft roar with the canopy's shudder in it."""
    loop, fade = int(seconds * RATE), int(0.4 * RATE)
    count = loop + fade
    roar = bandpass(noise(count, 61), 110.0, 620.0)
    air = bandpass(noise(count, 62), 600.0, 2400.0)
    out = []
    for index in range(count):
        shudder = 0.8 + 0.2 * periodic(index, loop, 34, 0.0) * (0.6 + 0.4 * periodic(index, loop, 3, 0.2))
        out.append((roar[index] * 1.0 + air[index] * 0.3) * shudder)
    return normalise(make_loop(out, loop, fade), 0.8)


def rotation_whoosh_loop(seconds=1.5):
    """Air rushing past the rider's own head through a rotation: brighter and closer than the
    kite's roar, with a fast flutter riding on top (review batch D, tricks.md 6.9: a rotation
    whoosh whose pitch follows the spin rate). The game drives its volume and pitch; this loop
    just has to read as air right at the ears, with no low rumble to anchor it in place."""
    loop, fade = int(seconds * RATE), int(0.25 * RATE)
    count = loop + fade
    rush = bandpass(noise(count, 101), 500.0, 3400.0)
    hiss = bandpass(noise(count, 102), 2200.0, 7200.0)
    out = []
    for index in range(count):
        flutter = 0.75 + 0.25 * periodic(index, loop, 11, 0.0)
        out.append(rush[index] * 0.9 * flutter + hiss[index] * 0.35)
    return normalise(make_loop(out, loop, fade), 0.8)


def flutter_loop(seconds=2.0):
    """A canopy with no load in it flapping: quick, uneven slaps of cloth."""
    loop = int(seconds * RATE)
    rng = random.Random(71)
    cloth = bandpass(noise(loop, 72), 350.0, 3600.0)
    out = [0.0] * loop
    time = 0.0
    while time < seconds:
        start = int(time * RATE)
        length = int(rng.uniform(0.035, 0.06) * RATE)
        level = rng.uniform(0.55, 1.0)
        for offset in range(length):
            t = offset / RATE
            envelope = min(offset / 40.0, 1.0) * math.exp(-55.0 * t)
            out[(start + offset) % loop] += cloth[(start + offset) % loop] * envelope * level
        time += rng.uniform(0.055, 0.11)
    return normalise(out, 0.8)


# ----------------------------------------------------------------------------------------------
# One-shots
# ----------------------------------------------------------------------------------------------

def splash(seconds, seed, thump_hz, brightness_hz, attack=0.004, bubbles=0):
    """A burst of water: a low thump under spray that darkens as it dies away, and for the big
    ones the bubbles that follow."""
    count = int(seconds * RATE)
    spray = noise(count, seed)
    rng = random.Random(seed + 100)
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
    for _ in range(bubbles):
        start = int(rng.uniform(0.18, 0.75) * count)
        pitch = rng.uniform(380.0, 1100.0)
        length = int(rng.uniform(0.03, 0.07) * RATE)
        level = rng.uniform(0.08, 0.2)
        for offset in range(min(length, count - start)):
            t = offset / RATE
            # A bubble's note rises as it shrinks.
            out[start + offset] += level * math.sin(2.0 * math.pi * pitch * t * (1.0 + 6.0 * t)) * math.exp(-60.0 * t)
    return normalise(fade_out(out), 0.9)


def stomp():
    """A trick stomped clean: a bigger, brighter splash than an ordinary landing, with a quick
    slap on top to sell the impact (review batch D: the feedback for a landed trick)."""
    count = int(0.55 * RATE)
    out = splash(0.55, 91, 85.0, 7000.0, attack=0.003, bubbles=6)
    slap = bandpass(noise(count, 92), 1200.0, 6000.0)
    for index in range(count):
        t = index / RATE
        out[index] += slap[index] * math.exp(-60.0 * t) * min(t / 0.004, 1.0) * 0.5
    return normalise(fade_out(out), 0.95)


def pop():
    """The board letting go of the water: a tight thwack and a short kick of spray."""
    count = int(0.32 * RATE)
    spray = bandpass(noise(count, 41), 900.0, 7000.0)
    out = []
    for index in range(count):
        t = index / RATE
        thwack = math.sin(2.0 * math.pi * (210.0 - 380.0 * t) * t) * math.exp(-34.0 * t)
        out.append(thwack * 0.9 * min(index / 30.0, 1.0) + spray[index] * math.exp(-16.0 * t) * min(t / 0.006, 1.0) * 0.8)
    return normalise(fade_out(out), 0.9)


def kite_crash():
    """The kite hitting the water a line's length away: a flat slap, duller than a splash nearby."""
    count = int(0.9 * RATE)
    slap = lowpass(noise(count, 81), 1500.0)
    out = []
    for index in range(count):
        t = index / RATE
        out.append(slap[index] * math.exp(-7.0 * t) * min(t / 0.003, 1.0) + 0.7 * math.sin(2.0 * math.pi * 95.0 * t) * math.exp(-11.0 * t))
    return normalise(fade_out(out), 0.85)


def relaunch():
    """The kite coming up off the water and filling: a rising rush of air."""
    seconds = 0.9
    count = int(seconds * RATE)
    source = noise(count, 82)
    swept = swept_bandpass(source, lambda i: 260.0 + 2200.0 * (i / count) ** 2, q=2.5)
    out = [swept[i] * math.sin(math.pi * i / count) ** 1.5 for i in range(count)]
    return normalise(fade_out(out), 0.8)


def aground():
    """The board running onto sand: a dull thud and a gritty scrape."""
    count = int(0.8 * RATE)
    grit = bandpass(noise(count, 83), 500.0, 3200.0)
    out = []
    for index in range(count):
        t = index / RATE
        scrape = grit[index] * (0.55 + 0.45 * math.sin(2.0 * math.pi * 47.0 * t)) * math.exp(-5.5 * t) * min(t / 0.01, 1.0)
        thud = math.sin(2.0 * math.pi * (120.0 - 60.0 * t) * t) * math.exp(-13.0 * t)
        out.append(scrape * 0.8 + thud)
    return normalise(fade_out(out), 0.9)


def shark():
    """Two low notes a semitone apart, grinding: something is wrong."""
    seconds = 1.1
    count = int(seconds * RATE)
    out = []
    phases = [0.0, 0.0]
    for index in range(count):
        t = index / RATE
        sample = 0.0
        for voice, pitch in enumerate((92.5, 98.0)):
            phases[voice] = (phases[voice] + pitch / RATE) % 1.0
            sample += 2.0 * phases[voice] - 1.0
        envelope = min(t / 0.02, 1.0) * math.exp(-2.4 * t) * (0.7 + 0.3 * math.sin(2.0 * math.pi * 7.0 * t))
        out.append(sample * envelope)
    return normalise(fade_out(lowpass(out, 1400.0)), 0.85)


def blips(notes, seconds, decay=20.0, level=0.6):
    """Soft sine blips: (start seconds, pitch) pairs. Used for the reset cue and the menu."""
    count = int(seconds * RATE)
    out = [0.0] * count
    for start, pitch in notes:
        first = int(start * RATE)
        for index in range(first, count):
            t = (index - first) / RATE
            out[index] += math.sin(2.0 * math.pi * pitch * t) * math.exp(-decay * t) * min(t / 0.002, 1.0)
    return normalise(fade_out(out), level)


SOUNDS = {
    "SW_WindLoop": wind_loop,
    "SW_WaterLoop": water_loop,
    "SW_SprayLoop": spray_loop,
    "SW_LineLoop": line_loop,
    "SW_KiteLoop": kite_whoosh_loop,
    "SW_FlutterLoop": flutter_loop,
    "SW_RotationWhoosh": rotation_whoosh_loop,
    "SW_Pop": pop,
    "SW_Landing": lambda: splash(0.85, 42, 90.0, 5200.0),
    "SW_Crash": lambda: splash(1.8, 43, 60.0, 4200.0, attack=0.008, bubbles=14),
    "SW_Stomp": stomp,
    "SW_KiteCrash": kite_crash,
    "SW_Relaunch": relaunch,
    "SW_Aground": aground,
    "SW_Shark": shark,
    "SW_ResetCue": lambda: blips(((0.0, 520.0), (0.13, 780.0)), 0.35, decay=17.0),
    "SW_UIMove": lambda: blips(((0.0, 880.0),), 0.09, decay=55.0, level=0.45),
    "SW_UISelect": lambda: blips(((0.0, 660.0), (0.07, 990.0)), 0.26, decay=22.0, level=0.55),
    "SW_UIBack": lambda: blips(((0.0, 660.0), (0.07, 440.0)), 0.26, decay=22.0, level=0.5),
}

# Loops, which the game keeps playing and fades up and down, against one-shots.
LOOPS = ("SW_WindLoop", "SW_WaterLoop", "SW_SprayLoop", "SW_LineLoop", "SW_KiteLoop", "SW_FlutterLoop", "SW_RotationWhoosh")

# A sound quieter than this was not synthesised properly.
MIN_RMS = 0.04


def describe(samples):
    """(seconds, peak, rms) of a mono list or a stereo pair."""
    channels = samples if isinstance(samples, tuple) else (samples,)
    count = len(channels[0])
    peak = max(max(abs(s) for s in channel) for channel in channels)
    rms = math.sqrt(sum(sum(s * s for s in channel) for channel in channels) / (count * len(channels)))
    return count / RATE, peak, rms


def generate_all(out_dir):
    """Writes every sound effect to out_dir and returns {asset name: wav path}."""
    os.makedirs(out_dir, exist_ok=True)
    paths = {}
    for name, make in SOUNDS.items():
        samples = make()
        seconds, peak, rms = describe(samples)
        if rms < MIN_RMS:
            raise RuntimeError(f"{name} came out silent (rms {rms:.4f})")
        path = os.path.join(out_dir, f"{name}.wav")
        write_wav(path, samples)
        paths[name] = path
        print(f"{name}: {seconds:.2f} s, {'stereo' if isinstance(samples, tuple) else 'mono'}, peak {peak:.2f}, rms {rms:.3f}")
    return paths


if __name__ == "__main__":
    generate_all(sys.argv[1] if len(sys.argv) > 1 else ".")
