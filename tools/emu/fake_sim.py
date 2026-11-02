"""Fake simulator for the end-to-end test (T17): publishes sirv_msgs like SIRV.

Runs in the Jazzy container on the host network. The state comes from a JSON
file the test rewrites; the publisher rereads it when its mtime changes:
    {"speed": km/h, "gear": int8, "lever": ASCII int8, "handbrake": bool,
     "telemetry": bool}
`telemetry: false` stops the telemetry stream (the source goes stale).
Telemetry at 66 Hz on /<ns>/telemetry, the CAT793F lever at 20 Hz on
/<ns>/vehicle_control and the control at 20 Hz on /apply_control_to_vehicle.
"""

import json
import os
import sys
import time

import rclpy
from sirv_msgs.msg import (
    CAT793FVehicleControl,
    SirvVehicleControl,
    SirvVehicleTelemetry,
)


def main() -> None:
    ns, state_path = sys.argv[1], sys.argv[2]
    rclpy.init()
    node = rclpy.create_node("vipee_fake_sim")
    tel_pub = node.create_publisher(SirvVehicleTelemetry, f"/{ns}/telemetry", 5)
    lever_pub = node.create_publisher(
        CAT793FVehicleControl, f"/{ns}/vehicle_control", 1
    )
    ctl_pub = node.create_publisher(SirvVehicleControl, "/apply_control_to_vehicle", 2)
    state = {}
    mtime = 0.0
    tick = 0
    last = [time.monotonic()]

    def step() -> None:
        nonlocal state, mtime, tick
        m = os.stat(state_path).st_mtime
        if m != mtime:
            mtime = m
            with open(state_path) as f:
                state = json.load(f)
            print(f"STATE {state}", flush=True)
        if state.get("telemetry", True):
            tel = SirvVehicleTelemetry()
            tel.speed = float(state.get("speed", 0.0))
            tel.gear = int(state.get("gear", 0))
            tel_pub.publish(tel)
        tick += 1
        if tick % 330 == 0:  # ~5 s: real loop rate, for the rate checks
            now = time.monotonic()
            print(f"RATE {330 / (now - last[0]):.1f} Hz", flush=True)
            last[0] = now
        if tick % 3 == 0:  # ~22 Hz
            lever = CAT793FVehicleControl()
            lever.transmission = int(state.get("lever", 80))
            lever_pub.publish(lever)
            ctl = SirvVehicleControl()
            ctl.id = 1
            ctl.handbrake = bool(state.get("handbrake", False))
            ctl_pub.publish(ctl)

    node.create_timer(1.0 / 66.0, step)
    rclpy.spin(node)


if __name__ == "__main__":
    main()
