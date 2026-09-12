#!/opt/homebrew/Cellar/esptool/5.4.0/libexec/bin/python
"""Reset the board and print serial output for N seconds (default 8). Usage: tools/monitor.py [seconds] [port]"""
import serial, sys, time, glob
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 8
port = sys.argv[2] if len(sys.argv) > 2 else glob.glob('/dev/cu.usbmodem*')[0]
s = serial.Serial(port, 115200, timeout=0.2)
s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False)
t = time.time()
while time.time() - t < secs:
    d = s.read(4096)
    if d: sys.stdout.write(d.decode('utf8', 'replace')); sys.stdout.flush()
