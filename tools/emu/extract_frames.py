"""Writes one JPEG per fisheye camera from a bag, for fake_sim.py (cam-evs-slice).

Runs in the Jazzy container (rosbag2_py, mcap plugin):
    python3 extract_frames.py <bag_dir> <out_dir>
Writes <out_dir>/<pos>.jpg (pos = front, rear, left, right) with the bytes of
the first CompressedImage on /sirv/sensor/fisheye/<pos>/image/compressed.
Fails if a camera has no message or its format does not name jpeg.
"""

import os
import sys

import rosbag2_py
from rclpy.serialization import deserialize_message
from sensor_msgs.msg import CompressedImage

POSITIONS = ("front", "rear", "left", "right")


def main() -> None:
    bag, out = sys.argv[1], sys.argv[2]
    topics = {f"/sirv/sensor/fisheye/{p}/image/compressed": p for p in POSITIONS}
    reader = rosbag2_py.SequentialReader()
    reader.open(
        rosbag2_py.StorageOptions(uri=bag, storage_id="mcap"),
        rosbag2_py.ConverterOptions("cdr", "cdr"),
    )
    reader.set_filter(rosbag2_py.StorageFilter(topics=list(topics)))
    os.makedirs(out, exist_ok=True)
    left = dict(topics)
    while reader.has_next() and left:
        topic, data, _ = reader.read_next()
        pos = left.pop(topic, None)
        if pos is None:
            continue
        msg = deserialize_message(data, CompressedImage)
        if "jpeg" not in msg.format:
            sys.exit(f"{topic}: format '{msg.format}' is not jpeg")
        with open(os.path.join(out, f"{pos}.jpg"), "wb") as f:
            f.write(bytes(msg.data))
        print(f"FRAME {pos} {len(msg.data)} bytes", flush=True)
    if left:
        sys.exit(f"no frame on {sorted(left)}")


if __name__ == "__main__":
    main()
