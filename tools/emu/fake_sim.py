"""Fake simulator for the end-to-end test (T17): publishes sirv_msgs like SIRV.

Runs in the Jazzy container on the host network. The state comes from a JSON
file the test rewrites; the publisher rereads it when its mtime changes:
    {"speed": km/h, "gear": int8, "lever": ASCII int8, "handbrake": bool,
     "telemetry": bool, "publish": bool}
     "telemetry": bool, "publish": bool,
     "cameras": {"front"|"rear"|"left"|"right": "ok"|"small"|"png"|"trunc"|"off"}}
`telemetry: false` stops the telemetry stream; `publish: false` stops all
streams (the simulator goes silent).
Telemetry at 66 Hz on /<ns>/telemetry, the CAT793F lever at 20 Hz on
/<ns>/vehicle_control and the control at 20 Hz on /apply_control_to_vehicle.

Cameras (cam-evs-slice), only with a frames dir (third argument): 10 Hz
CompressedImage on /sirv/sensor/fisheye/<pos>/image/compressed, per mode:
ok = <frames>/<pos>.jpg (from extract_frames.py, 1280x720), small =
<frames>/small.jpg (a 480x480 JPEG), png = <frames>/frame.png with format
'png', trunc = the first half of <pos>.jpg, off = nothing. A camera missing
from "cameras" is off.
"""

import json
import os
import sys
import time

import rclpy
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import CompressedImage
from sirv_msgs.msg import (
    CAT793FVehicleControl,
    SirvVehicleControl,
    SirvVehicleTelemetry,
)


def main() -> None:
    ns, state_path = sys.argv[1], sys.argv[2]
    frames = sys.argv[3] if len(sys.argv) > 3 else None
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
        if not state.get("publish", True):
            return
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
    if frames:
        cams = {
            p: node.create_publisher(
                CompressedImage,
                f"/sirv/sensor/fisheye/{p}/image/compressed",
                qos_profile_sensor_data,
            )
            for p in ("front", "rear", "left", "right")
        }

        def read(name: str) -> bytes:
            with open(os.path.join(frames, name), "rb") as f:
                return f.read()

        def frame(pos: str, mode: str) -> tuple[str, bytes]:
            if mode == "ok":
                return "jpeg", read(f"{pos}.jpg")
            if mode == "small":
                return "jpeg", read("small.jpg")
            if mode == "png":
                return "png", read("frame.png")
            if mode == "trunc":
                jpg = read(f"{pos}.jpg")
                return "jpeg", jpg[: len(jpg) // 2]
            raise ValueError(f"camera {pos}: unknown mode {mode}")

        def cameras() -> None:
            if not state.get("publish", True):
                return
            for pos, mode in state.get("cameras", {}).items():
                if mode == "off":
                    continue
                msg = CompressedImage()
                msg.header.stamp = node.get_clock().now().to_msg()
                msg.format, data = frame(pos, mode)
                msg.data = data
                cams[pos].publish(msg)

        node.create_timer(0.1, cameras)
    rclpy.spin(node)


if __name__ == "__main__":
    main()
