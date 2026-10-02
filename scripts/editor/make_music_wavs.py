#!/usr/bin/env python3
"""Composes and synthesises the game's music as 16-bit stereo WAV loops. Standard library only.

    scripts/editor/make_music_wavs.py <output directory>

Three loops, all written out here as notes and played on a few simple synth voices:

    MU_Menu       a calm pad and a slow arpeggio for the menus
    MU_RideBase   the ride: pad, bass, a light beat and an arpeggio
    MU_RideAir    the same bars again as a lead line and a busier top end; the game plays it in
                  step with MU_RideBase and fades it in while the rider is in the air

The two ride loops are exactly the same length and tempo, so they stay in step. Every note is
added into the loop with its tail wrapped round to the start, and the echo runs round the loop
too, so the loops are seamless without a cross-fade.
"""
import math
import os
import random
import struct
import sys
import wave

RATE = 44100

NOTE_NUMBERS = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def hz(name):
    """'A4' -> 440.0; sharps as 'F#3'."""
    letter, rest = name[0], name[1:]
    sharp = rest.startswith('#')
    octave = int(rest[1:] if sharp else rest)
    midi = 12 * (octave + 1) + NOTE_NUMBERS[letter] + (1 if sharp else 0)
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


class Loop:
    """A stereo loop that notes are added into; anything past the end wraps to the start."""

    def __init__(self, seconds):
        self.count = int(round(seconds * RATE))
        self.left = [0.0] * self.count
        self.right = [0.0] * self.count

    def add(self, start_seconds, samples, pan=0.0, gain=1.0):
        """pan: -1 left .. 1 right, equal power."""
        angle = (pan + 1.0) * math.pi / 4.0
        left_gain, right_gain = math.cos(angle) * gain, math.sin(angle) * gain
        index = int(round(start_seconds * RATE)) % self.count
        left, right, count = self.left, self.right, self.count
        for sample in samples:
            left[index] += sample * left_gain
            right[index] += sample * right_gain
            index += 1
            if index == count:
                index = 0

    def echo(self, delay_seconds, feedback, wet):
        """A ping-pong echo that runs round the loop, so its tail is already at the start."""
        delay = int(round(delay_seconds * RATE))
        left, right, count = self.left, self.right, self.count
        echo_left = [0.0] * count
        echo_right = [0.0] * count
        # Twice round: the second pass picks up the tail the first left at the start.
        for _ in range(2):
            for index in range(count):
                source = index - delay
                echo_left[index] = right[source] * wet + echo_right[source] * feedback
                echo_right[index] = left[source] * wet + echo_left[source] * feedback
        for index in range(count):
            left[index] += echo_left[index]
            right[index] += echo_right[index]

    def finish(self, peak=0.89):
        """Soft-limits and scales to the peak level."""
        top = max(max(abs(s) for s in self.left), max(abs(s) for s in self.right)) or 1.0
        drive = 1.6 / top
        self.left = [math.tanh(s * drive) for s in self.left]
        self.right = [math.tanh(s * drive) for s in self.right]
        top = max(max(abs(s) for s in self.left), max(abs(s) for s in self.right)) or 1.0
        self.left = [s * peak / top for s in self.left]
        self.right = [s * peak / top for s in self.right]
        return (self.left, self.right)


# ----------------------------------------------------------------------------------------------
# Voices: each returns a list of samples for one note
# ----------------------------------------------------------------------------------------------

def pad(pitch, seconds, attack=0.5, release=0.9, cutoff=1100.0, detune=0.006):
    """Two saws a little out of tune through a low-pass that opens and closes slowly: a soft wash."""
    count = int((seconds + release) * RATE)
    step_a, step_b = pitch * (1.0 - detune) / RATE, pitch * (1.0 + detune) / RATE
    phase_a, phase_b, state_1, state_2 = 0.0, 0.37, 0.0, 0.0
    hold = int(seconds * RATE)
    out = []
    for index in range(count):
        phase_a += step_a
        if phase_a >= 1.0:
            phase_a -= 1.0
        phase_b += step_b
        if phase_b >= 1.0:
            phase_b -= 1.0
        t = index / RATE
        envelope = min(t / attack, 1.0) if index < hold else max(1.0 - (index - hold) / (release * RATE), 0.0)
        alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff * (0.55 + 0.45 * envelope) / RATE)
        state_1 += alpha * ((phase_a + phase_b - 1.0) - state_1)
        state_2 += alpha * (state_1 - state_2)
        out.append(state_2 * envelope)
    return out


def pluck(pitch, seconds, cutoff=2600.0, decay=7.0):
    """A plucked note: bright at the start, darkening as it fades."""
    count = int(seconds * RATE)
    step = pitch / RATE
    phase, state = 0.0, 0.0
    out = []
    for index in range(count):
        phase += step
        if phase >= 1.0:
            phase -= 1.0
        t = index / RATE
        envelope = min(t / 0.004, 1.0) * math.exp(-decay * t)
        # A triangle with a little saw in it.
        wave_sample = (4.0 * abs(phase - 0.5) - 1.0) * 0.75 + (2.0 * phase - 1.0) * 0.25
        alpha = 1.0 - math.exp(-2.0 * math.pi * (300.0 + cutoff * envelope) / RATE)
        state += alpha * (wave_sample - state)
        out.append(state * envelope)
    return out


def bass(pitch, seconds):
    """A round bass note: a saw closed right down, with a sine underneath."""
    count = int(seconds * RATE)
    step = pitch / RATE
    phase, state = 0.0, 0.0
    out = []
    for index in range(count):
        phase += step
        if phase >= 1.0:
            phase -= 1.0
        t = index / RATE
        envelope = min(t / 0.006, 1.0) * math.exp(-3.2 * t) * min((count - index) / 300.0, 1.0)
        alpha = 1.0 - math.exp(-2.0 * math.pi * (140.0 + 520.0 * math.exp(-9.0 * t)) / RATE)
        state += alpha * ((2.0 * phase - 1.0) - state)
        out.append((state * 0.8 + math.sin(2.0 * math.pi * phase) * 0.6) * envelope)
    return out


def lead(pitch, seconds, vibrato_hz=5.5, release=0.25):
    """The tune: a saw and a square together, opening up as the note starts, with vibrato once it is held."""
    count = int((seconds + release) * RATE)
    hold = int(seconds * RATE)
    phase, state = 0.0, 0.0
    out = []
    for index in range(count):
        t = index / RATE
        vibrato = 1.0 + 0.006 * math.sin(2.0 * math.pi * vibrato_hz * t) * min(t / 0.35, 1.0)
        phase += pitch * vibrato / RATE
        if phase >= 1.0:
            phase -= 1.0
        envelope = min(t / 0.02, 1.0) * (0.85 + 0.15 * math.exp(-5.0 * t))
        if index >= hold:
            envelope *= max(1.0 - (index - hold) / (release * RATE), 0.0)
        wave_sample = (2.0 * phase - 1.0) * 0.6 + (1.0 if phase < 0.5 else -1.0) * 0.4
        alpha = 1.0 - math.exp(-2.0 * math.pi * (700.0 + 2400.0 * min(t / 0.08, 1.0)) / RATE)
        state += alpha * (wave_sample - state)
        out.append(state * envelope)
    return out


def kick():
    count = int(0.32 * RATE)
    out, phase = [], 0.0
    for index in range(count):
        t = index / RATE
        phase += (46.0 + 110.0 * math.exp(-28.0 * t)) / RATE
        out.append(math.sin(2.0 * math.pi * phase) * math.exp(-9.0 * t) * min(index / 20.0, 1.0))
    return out


def clap(rng):
    count = int(0.22 * RATE)
    out, low, high_state = [], 0.0, 0.0
    for index in range(count):
        t = index / RATE
        sample = rng.uniform(-1.0, 1.0)
        # Band-pass round 1.5 kHz: a low-pass and the remainder of a slower one.
        low += 0.32 * (sample - low)
        high_state += 0.08 * (low - high_state)
        # Three quick bursts, then the tail.
        burst = max(math.exp(-90.0 * (t % 0.011)), 0.0) if t < 0.033 else 1.0
        out.append((low - high_state) * math.exp(-17.0 * t) * burst)
    return out


def hat(rng, seconds=0.05, decay=70.0):
    count = int(seconds * RATE)
    out, low = [], 0.0
    for index in range(count):
        t = index / RATE
        sample = rng.uniform(-1.0, 1.0)
        low += 0.45 * (sample - low)
        out.append((sample - low) * math.exp(-decay * t))
    return out


# ----------------------------------------------------------------------------------------------
# The tunes
# ----------------------------------------------------------------------------------------------

# One chord to the bar: (bass note, pad notes). A minor, bright rather than sad.
CHORDS = {
    'Am': ('A1', ('A2', 'E3', 'A3', 'C4', 'E4')),
    'F': ('F1', ('F2', 'C3', 'F3', 'A3', 'C4')),
    'C': ('C2', ('C3', 'G3', 'C4', 'E4', 'G4')),
    'G': ('G1', ('G2', 'D3', 'G3', 'B3', 'D4')),
    'Em': ('E1', ('E2', 'B2', 'E3', 'G3', 'B3')),
    'Dm': ('D2', ('D3', 'A3', 'D4', 'F4', 'A4')),
}

RIDE_BARS = ['Am', 'F', 'C', 'G', 'Am', 'F', 'C', 'G', 'F', 'G', 'Am', 'Em', 'F', 'G', 'Am', 'Am']
RIDE_BPM = 104.0

# The tune over the ride bars: for each bar, (start beat, length in beats, note). Four beats to the bar.
RIDE_TUNE = [
    [(0, 1.5, 'E5'), (1.5, 0.5, 'D5'), (2, 1, 'C5'), (3, 1, 'A4')],
    [(0, 1.5, 'C5'), (1.5, 0.5, 'D5'), (2, 2, 'F5')],
    [(0, 1.5, 'G5'), (1.5, 0.5, 'E5'), (2, 1, 'D5'), (3, 1, 'C5')],
    [(0, 3, 'D5')],
    [(0, 1.5, 'E5'), (1.5, 0.5, 'G5'), (2, 1, 'A5'), (3, 1, 'G5')],
    [(0, 1, 'F5'), (1, 1, 'E5'), (2, 1, 'D5'), (3, 1, 'C5')],
    [(0, 1.5, 'E5'), (1.5, 0.5, 'D5'), (2, 1, 'C5'), (3, 1, 'E5')],
    [(0, 3.5, 'D5')],
    [(0, 1, 'A5'), (1, 1, 'G5'), (2, 1, 'F5'), (3, 1, 'E5')],
    [(0, 1.5, 'D5'), (1.5, 0.5, 'E5'), (2, 2, 'G5')],
    [(0, 1, 'C6'), (1, 1, 'B5'), (2, 1, 'A5'), (3, 1, 'E5')],
    [(0, 3, 'G5')],
    [(0, 1, 'A5'), (1, 1, 'C6'), (2, 1, 'A5'), (3, 1, 'F5')],
    [(0, 1.5, 'G5'), (1.5, 0.5, 'A5'), (2, 2, 'B5')],
    [(0, 1.5, 'C6'), (1.5, 0.5, 'B5'), (2, 1, 'A5'), (3, 1, 'E5')],
    [(0, 3.5, 'A5')],
]

MENU_BARS = ['Am', 'F', 'C', 'G', 'Dm', 'F', 'C', 'Em']
MENU_BPM = 84.0


def ride_base():
    beat = 60.0 / RIDE_BPM
    bar = 4.0 * beat
    loop = Loop(bar * len(RIDE_BARS))
    plucks = Loop(bar * len(RIDE_BARS))
    rng = random.Random(5)
    kick_samples = kick()
    for bar_index, chord in enumerate(RIDE_BARS):
        start = bar_index * bar
        bass_note, pad_notes = CHORDS[chord]
        for voice, note in enumerate(pad_notes):
            loop.add(start, pad(hz(note), bar - 0.3, cutoff=1700.0), pan=-0.6 + 0.3 * voice, gain=0.12)
        # Bass on the quavers, leaving the second and sixth out so that it bounces.
        for quaver in (0, 2, 3, 4, 6, 7):
            pitch = hz(bass_note) * (2.0 if quaver in (3, 7) else 1.0)
            loop.add(start + quaver * beat / 2.0, bass(pitch, beat * 0.45), gain=0.32)
        # Arpeggio in semiquavers, up and back down through the chord an octave up.
        order = (1, 2, 3, 4, 3, 2)
        for step in range(16):
            note = pad_notes[order[step % len(order)]]
            plucks.add(start + step * beat / 4.0, pluck(hz(note) * 2.0, beat * 0.6, cutoff=5200.0), pan=0.5 if step % 2 else -0.5, gain=0.24)
        # A light beat: kick on one and three, with a pick-up into every fourth bar; clap on two and four; hats on the quavers.
        kicks = [0.0, 2.0] + ([3.5] if bar_index % 4 == 3 else [])
        for at in kicks:
            loop.add(start + at * beat, kick_samples, gain=0.75)
        for at in (1.0, 3.0):
            loop.add(start + at * beat, clap(rng), pan=0.1, gain=0.45)
        for quaver in range(8):
            loop.add(start + quaver * beat / 2.0, hat(rng), pan=0.3, gain=0.22 if quaver % 2 == 0 else 0.34)
    plucks.echo(beat * 0.75, 0.38, 0.45)
    loop.add(0.0, plucks.left, pan=-1.0)
    loop.add(0.0, plucks.right, pan=1.0)
    return loop.finish()


def ride_air():
    beat = 60.0 / RIDE_BPM
    bar = 4.0 * beat
    loop = Loop(bar * len(RIDE_BARS))
    tune = Loop(bar * len(RIDE_BARS))
    rng = random.Random(6)
    for bar_index, chord in enumerate(RIDE_BARS):
        start = bar_index * bar
        _, pad_notes = CHORDS[chord]
        for at, length, note in RIDE_TUNE[bar_index]:
            tune.add(start + at * beat, lead(hz(note), length * beat * 0.92), gain=0.30)
        # A high shimmer of the chord, and semiquaver hats with an open one before each bar.
        for voice, note in enumerate(pad_notes[2:]):
            loop.add(start, pad(hz(note) * 2.0, bar - 0.3, attack=0.9, cutoff=2600.0), pan=0.7 - 0.7 * voice, gain=0.045)
        for step in range(16):
            loop.add(start + step * beat / 4.0, hat(rng, 0.035, 95.0), pan=-0.35, gain=0.09 if step % 4 else 0.05)
        loop.add(start + 3.5 * beat, hat(rng, 0.22, 16.0), pan=-0.2, gain=0.11)
    tune.echo(beat * 0.75, 0.42, 0.5)
    loop.add(0.0, tune.left, pan=-1.0)
    loop.add(0.0, tune.right, pan=1.0)
    return loop.finish(0.8)


def menu():
    beat = 60.0 / MENU_BPM
    bar = 4.0 * beat
    loop = Loop(bar * len(MENU_BARS))
    plucks = Loop(bar * len(MENU_BARS))
    for bar_index, chord in enumerate(MENU_BARS):
        start = bar_index * bar
        bass_note, pad_notes = CHORDS[chord]
        for voice, note in enumerate(pad_notes):
            loop.add(start, pad(hz(note), bar - 0.4, attack=1.1, release=1.6, cutoff=1500.0), pan=-0.7 + 0.35 * voice, gain=0.13)
        loop.add(start, pad(hz(bass_note) * 2.0, bar - 0.4, attack=0.6, release=1.2, cutoff=300.0, detune=0.002), gain=0.20)
        # A slow arpeggio in quavers that climbs and falls.
        order = (1, 2, 3, 4, 3, 2, 3, 4)
        for step in range(8):
            note = pad_notes[order[step]]
            plucks.add(start + step * beat / 2.0, pluck(hz(note) * 2.0, beat * 1.2, cutoff=4200.0, decay=4.5), pan=0.4 if step % 2 else -0.4, gain=0.26)
    plucks.echo(beat * 1.5, 0.45, 0.55)
    loop.add(0.0, plucks.left, pan=-1.0)
    loop.add(0.0, plucks.right, pan=1.0)
    return loop.finish(0.8)


TRACKS = {
    "MU_Menu": menu,
    "MU_RideBase": ride_base,
    "MU_RideAir": ride_air,
}

# Loops that must be exactly as long as each other, because the game plays them in step.
SYNCED = ("MU_RideBase", "MU_RideAir")


def write_stereo(path, pair):
    with wave.open(path, "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        frames = bytearray()
        for left, right in zip(*pair):
            frames += struct.pack("<hh", int(max(-1.0, min(1.0, left)) * 32767), int(max(-1.0, min(1.0, right)) * 32767))
        wav.writeframes(bytes(frames))


def generate_all(out_dir):
    """Writes every track to out_dir and returns {asset name: wav path}."""
    os.makedirs(out_dir, exist_ok=True)
    paths, lengths = {}, {}
    for name, make in TRACKS.items():
        pair = make()
        count = len(pair[0])
        rms = math.sqrt(sum(l * l + r * r for l, r in zip(*pair)) / (2 * count))
        if rms < 0.05:
            raise RuntimeError(f"{name} came out silent (rms {rms:.4f})")
        seam = abs(pair[0][0] - pair[0][-1])
        lengths[name] = count
        path = os.path.join(out_dir, f"{name}.wav")
        write_stereo(path, pair)
        paths[name] = path
        print(f"{name}: {count / RATE:.2f} s, peak {max(max(abs(s) for s in pair[0]), max(abs(s) for s in pair[1])):.2f}, rms {rms:.3f}, step across the loop point {seam:.3f}")
    if len({lengths[name] for name in SYNCED}) != 1:
        raise RuntimeError(f"The synced loops differ in length: {[lengths[name] for name in SYNCED]}")
    return paths


if __name__ == "__main__":
    generate_all(sys.argv[1] if len(sys.argv) > 1 else ".")
