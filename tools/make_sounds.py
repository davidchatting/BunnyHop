#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the sample sounds in firmware/data/audio (needs numpy, ffmpeg, espeak-ng).

big-ben.mp3 - the Westminster Quarters (1793, public domain) and one hour
              strike, synthesised with bell-like partials an octave up
hello.mp3   - a greeting spoken by eSpeak NG
"""
import os, subprocess, tempfile, wave
import numpy as np

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware", "data", "audio")
RATE = 22050

# Bell partials (ratio to strike note, relative amplitude, decay time in s):
# hum, prime, tierce (minor third), quint, nominal, and a few upper partials
PARTIALS = [(0.5, 0.6, 4.0), (1.0, 1.0, 2.6), (1.19, 0.5, 2.0), (1.5, 0.35, 1.6),
            (2.0, 0.7, 1.4), (2.5, 0.25, 0.9), (3.0, 0.2, 0.7), (4.2, 0.12, 0.4)]


def bell(freq, dur, decay_scale=1.0):
    t = np.arange(int(RATE * dur)) / RATE
    s = np.zeros_like(t)
    for ratio, amp, decay in PARTIALS:
        s += amp * np.sin(2 * np.pi * freq * ratio * t) * np.exp(-t / (decay * decay_scale))
    s *= np.minimum(1, t / 0.004)  # soft strike, no click
    return s


def westminster():
    # One octave above the real bells: a small speaker can't do Big Ben's low E
    G4s, F4s, E4, B3, E3 = 830.61, 739.99, 659.26, 493.88, 329.63
    changes = [[E4, G4s, F4s, B3], [E4, F4s, G4s, E4], [G4s, E4, F4s, B3], [B3, F4s, G4s, E4]]  # full hour
    beat, total = 0.75, 4 * (4 * 0.75 + 0.75) + 6
    out = np.zeros(int(RATE * (total + 6)))
    t = 0.3
    for ch in changes:
        for i, f in enumerate(ch):
            note = bell(f, 4.0) * 0.5
            if i == 3:  # last note of each change is held longer
                note = bell(f, 5.0, 1.3) * 0.5
            n = int(t * RATE)
            out[n:n + len(note)] += note[:len(out) - n]
            t += beat * (2 if i == 3 else 1)
        t += beat * 0.5
    hour = bell(E3, 6.0, 2.0) * 0.8  # one hour strike
    n = int((t + 0.8) * RATE)
    out[n:n + len(hour)] += hour[:len(out) - n]
    end = n + len(hour)
    out = out[:end]
    return out / np.abs(out).max() * 0.9


def save_mp3(samples, name):
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        path = f.name
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes((samples * 32767).astype(np.int16).tobytes())
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", path, "-af", "loudnorm=I=-16",
                    "-ac", "1", "-ar", str(RATE), "-b:a", "48k", os.path.join(OUT, name)], check=True)
    os.unlink(path)


def hello():
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        path = f.name
    text = "Hello, I'm Nabaztag."
    subprocess.run(["espeak-ng", "-v", "en-gb", "-s", "140", "-w", path, text], check=True)
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", path, "-af", "loudnorm=I=-16",
                    "-ac", "1", "-ar", str(RATE), "-b:a", "48k", os.path.join(OUT, "hello.mp3")], check=True)
    os.unlink(path)


if __name__ == "__main__":
    save_mp3(westminster(), "big-ben.mp3")
    hello()
    for n in sorted(os.listdir(OUT)):
        print(n, os.path.getsize(os.path.join(OUT, n)), "bytes")
