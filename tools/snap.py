#!/usr/bin/env python3
"""Grab a frame from the USB camera watching the rabbit.
usage: snap.py OUT.jpg [exposure]   (exposure: V4L2 absolute units, lower = darker)"""
import sys, time, cv2
out = sys.argv[1]
exp = float(sys.argv[2]) if len(sys.argv) > 2 else 60
cap = cv2.VideoCapture(0, cv2.CAP_V4L2)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280); cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
cap.set(cv2.CAP_PROP_AUTO_EXPOSURE, 1)  # manual
cap.set(cv2.CAP_PROP_EXPOSURE, exp)
for _ in range(15):  # let exposure settle
    cap.read(); time.sleep(0.03)
ok, f = cap.read()
cap.release()
if not ok: sys.exit("no frame")
cv2.imwrite(out, f)
