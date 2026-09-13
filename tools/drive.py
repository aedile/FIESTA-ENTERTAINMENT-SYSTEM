#!/usr/bin/env python3
# needs pyserial; on a Mac with Homebrew esptool, the interpreter in
# /opt/homebrew/Cellar/esptool/*/libexec/bin/python already has it
"""Reset the board, type test-pad keys at given times, echo serial output.
Usage: tools/drive.py <seconds> [key@t ...]   e.g. tools/drive.py 20 s@6 j@7 m@12
Keys: w/a/s/d d-pad, j A, k B, q start, e select, m menu."""
import serial, sys, time, glob
secs = float(sys.argv[1])
events = sorted((float(t), k) for k, t in (a.split('@') for a in sys.argv[2:]))
s = serial.Serial(glob.glob('/dev/cu.usbmodem*')[0], 115200, timeout=0.05)
s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False)
t0 = time.time()
while time.time() - t0 < secs:
    while events and time.time() - t0 >= events[0][0]:
        s.write(events.pop(0)[1].encode()); s.flush()
    d = s.read(4096)
    if d: sys.stdout.write(d.decode('utf8', 'replace')); sys.stdout.flush()
