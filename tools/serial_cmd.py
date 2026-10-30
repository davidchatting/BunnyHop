#!/usr/bin/env python3
"""Send console commands to the bridge over USB serial and print its output.

usage: serial_cmd.py [-p PORT] [-t SECONDS] [command ...]
With no commands it just listens for -t seconds.
"""
import argparse, sys, time
import serial

ap = argparse.ArgumentParser()
ap.add_argument("-p", "--port", default="/dev/ttyUSB0")
ap.add_argument("-t", "--time", type=float, default=5.0, help="seconds to listen after the last command")
ap.add_argument("commands", nargs="*")
a = ap.parse_args()

s = serial.Serial()
s.port, s.baudrate, s.timeout = a.port, 115200, 0.1
s.dtr = False; s.rts = False  # set before open so the board isn't reset
s.open()
def drain(secs):
    end = time.time() + secs
    while time.time() < end:
        data = s.read(4096)
        if data:
            sys.stdout.write(data.decode("utf-8", "replace")); sys.stdout.flush()
for c in a.commands:
    s.write((c + "\n").encode())
    drain(0.5)
drain(a.time)
