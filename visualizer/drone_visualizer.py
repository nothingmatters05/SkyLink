"""Visualize the unfiltered ESP32 drone telemetry stream."""

import math
import socket
import threading
import time

from vpython import (
    box,
    canvas,
    compound,
    cone,
    curve,
    cylinder,
    label,
    rate,
    sphere,
    vector,
)


ESP_IP = "192.168.4.1"  # ESP32 SoftAP default IP; update if your board prints another.
ESP_PORT = 8080
MAX_ALTITUDE = 2.0
MAX_LINE_BYTES = 4096
ROOM_WIDTH = 4.0
ROOM_DEPTH = 4.0
ROOM_HEIGHT = MAX_ALTITUDE
DRONE_CLEARANCE = 0.22
PROPELLER_SPEED = 58.0
ROOM_HALF_WIDTH = ROOM_WIDTH / 2
ROOM_HALF_DEPTH = ROOM_DEPTH / 2

data_lock = threading.Lock()
latest_telemetry = (0.0, 0.0, 0.0, 0.0)
latest_raw_line = ""
last_telemetry_at = 0.0
connection_state = "CONNECTING"
stop_event = threading.Event()


def normalize_esp_ip(value):
    """Accept either a bare IP or a URL and return a socket host value."""
    host = value.strip()
    if "://" in host:
        host = host.split("://", 1)[1]
    return host.rstrip("/")


def parse_telemetry(line):
    """Return (roll, pitch, yaw_rate, altitude), or None for invalid input."""
    expected_keys = {"R", "P", "Y", "Alt"}
    values = {}

    try:
        for field in line.strip().split(","):
            key, raw_value = field.split(":", 1)
            key = key.strip()
            if key not in expected_keys or key in values:
                return None
            value = float(raw_value.strip())
            if not math.isfinite(value):
                return None
            values[key] = value
    except (ValueError, TypeError):
        return None

    if set(values) != expected_keys:
        return None

    return values["R"], values["P"], values["Y"], values["Alt"]


def set_connection_state(state):
    global connection_state
    with data_lock:
        connection_state = state


def receive_telemetry():
    global latest_telemetry, latest_raw_line, last_telemetry_at
    pending = bytearray()
    esp_host = normalize_esp_ip(ESP_IP)

    while not stop_event.is_set():
        try:
            set_connection_state("CONNECTING")
            print(f"Connecting to {esp_host}:{ESP_PORT}...")
            with socket.create_connection((esp_host, ESP_PORT), timeout=5) as sock:
                sock.settimeout(1.0)
                set_connection_state("CONNECTED")
                print("Connected to ESP32 telemetry server")

                while not stop_event.is_set():
                    try:
                        chunk = sock.recv(1024)
                    except socket.timeout:
                        continue
                    if not chunk:
                        raise ConnectionError("ESP32 closed the TCP connection")

                    pending.extend(chunk)
                    while b"\n" in pending:
                        raw_line, _, remainder = pending.partition(b"\n")
                        pending = bytearray(remainder)
                        if len(raw_line) > MAX_LINE_BYTES:
                            print("Ignored oversized telemetry line")
                            continue

                        line = raw_line.decode("utf-8", errors="replace")
                        frame = line.rstrip("\r")
                        parsed = parse_telemetry(frame)
                        if parsed is None:
                            print(f"Ignored malformed telemetry: {line!r}")
                            continue

                        received_at = time.monotonic()
                        with data_lock:
                            latest_telemetry = parsed
                            latest_raw_line = frame
                            last_telemetry_at = received_at

                        roll, pitch, yaw_rate, altitude = parsed
                        print(
                            f"RX LINE / UNFILTERED: {frame} | "
                            f"parsed: roll={roll:.2f}°, pitch={pitch:.2f}°, "
                            f"yaw_rate={yaw_rate:.2f}°/s, altitude={altitude:.3f} m"
                        )

                    if len(pending) > MAX_LINE_BYTES:
                        print("Discarded oversized incomplete telemetry line")
                        pending.clear()

        except (OSError, ConnectionError) as exc:
            if not stop_event.is_set():
                set_connection_state("RETRYING")
                print(f"Telemetry connection error: {exc}; retrying in 3 seconds")
                stop_event.wait(3)


def orient_vector(v, roll, pitch, yaw):
    """Apply roll about X, pitch about Z, then yaw about world Y."""
    x, y, z = v.x, v.y, v.z

    cos_roll, sin_roll = math.cos(roll), math.sin(roll)
    y, z = y * cos_roll - z * sin_roll, y * sin_roll + z * cos_roll

    cos_pitch, sin_pitch = math.cos(pitch), math.sin(pitch)
    x, y = x * cos_pitch - y * sin_pitch, x * sin_pitch + y * cos_pitch

    cos_yaw, sin_yaw = math.cos(yaw), math.sin(yaw)
    x, z = x * cos_yaw + z * sin_yaw, -x * sin_yaw + z * cos_yaw
    return vector(x, y, z)


scene = canvas(
    title="SKYFRAME  |  FLIGHT TELEMETRY",
    width=1240,
    height=780,
    background=vector(0.025, 0.055, 0.09),
)
scene.center = vector(0, ROOM_HEIGHT / 2, 0)
scene.forward = vector(-1, -0.5, -1)
scene.range = 3.65
scene.autoscale = False

cyan = vector(0.12, 0.86, 0.91)
blue = vector(0.1, 0.36, 0.54)
wall = vector(0.1, 0.3, 0.42)
graphite = vector(0.08, 0.12, 0.17)

box(
    pos=vector(0, -0.06, 0),
    size=vector(ROOM_WIDTH, 0.12, ROOM_DEPTH),
    color=vector(0.07, 0.12, 0.17),
)
box(
    pos=vector(0, ROOM_HEIGHT + 0.05, 0),
    size=vector(ROOM_WIDTH, 0.1, ROOM_DEPTH),
    color=wall,
    opacity=0.16,
)
for wall_x in (-ROOM_HALF_WIDTH, ROOM_HALF_WIDTH):
    box(
        pos=vector(wall_x, ROOM_HEIGHT / 2, 0),
        size=vector(0.06, ROOM_HEIGHT, ROOM_DEPTH),
        color=wall,
        opacity=0.16,
    )
for wall_z in (-ROOM_HALF_DEPTH, ROOM_HALF_DEPTH):
    box(
        pos=vector(0, ROOM_HEIGHT / 2, wall_z),
        size=vector(ROOM_WIDTH, ROOM_HEIGHT, 0.06),
        color=wall,
        opacity=0.16,
    )

for grid_index in range(-4, 5):
    grid_position = grid_index * 0.5
    curve(
        pos=[
            vector(-ROOM_HALF_WIDTH, 0.006, grid_position),
            vector(ROOM_HALF_WIDTH, 0.006, grid_position),
        ],
        color=blue,
        radius=0.0025,
    )
    curve(
        pos=[
            vector(grid_position, 0.006, -ROOM_HALF_DEPTH),
            vector(grid_position, 0.006, ROOM_HALF_DEPTH),
        ],
        color=blue,
        radius=0.0025,
    )

room_edges = []
for x_edge in (-ROOM_HALF_WIDTH, ROOM_HALF_WIDTH):
    for z_edge in (-ROOM_HALF_DEPTH, ROOM_HALF_DEPTH):
        room_edges.extend(
            [
                [
                    vector(x_edge, 0, z_edge),
                    vector(x_edge, ROOM_HEIGHT, z_edge),
                ],
                [
                    vector(x_edge, 0, z_edge),
                    vector(x_edge, 0, -z_edge),
                ],
                [
                    vector(x_edge, ROOM_HEIGHT, z_edge),
                    vector(-x_edge, ROOM_HEIGHT, z_edge),
                ],
            ]
        )
for edge in room_edges:
    curve(pos=edge, color=cyan, radius=0.006)

for height_mark in (0.0, 1.0, MAX_ALTITUDE):
    label(
        pos=vector(ROOM_HALF_WIDTH + 0.04, height_mark, 0),
        text=f"{height_mark:.1f} m",
        height=12,
        color=cyan,
        box=False,
        line=False,
        opacity=0,
    )

drone_parts = [
    box(
        pos=vector(0, 0, 0),
        size=vector(0.36, 0.12, 0.3),
        color=graphite,
    ),
    box(
        pos=vector(0, 0.075, 0),
        size=vector(0.22, 0.035, 0.2),
        color=vector(0.13, 0.22, 0.29),
    ),
    cone(
        pos=vector(0.15, 0.015, 0),
        axis=vector(0.22, 0, 0),
        radius=0.09,
        color=vector(0.93, 0.35, 0.16),
    ),
    sphere(
        pos=vector(0.32, 0.015, 0),
        radius=0.045,
        color=cyan,
    ),
]
rotor_positions = []
for arm_x, arm_z in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
    rotor_position = vector(arm_x * 0.48, 0, arm_z * 0.48)
    rotor_positions.append(rotor_position)
    drone_parts.extend(
        [
            cylinder(
                pos=vector(0, 0, 0),
                axis=rotor_position,
                radius=0.027,
                color=vector(0.23, 0.31, 0.37),
            ),
            cylinder(
                pos=rotor_position + vector(0, 0.025, 0),
                axis=vector(0, 0.105, 0),
                radius=0.07,
                color=vector(0.15, 0.2, 0.25),
            ),
        ]
    )

drone = compound(drone_parts)
drone.pos = vector(0, DRONE_CLEARANCE, 0)

propellers = []
for index, rotor_position in enumerate(rotor_positions):
    propeller_position = rotor_position + vector(0, 0.16, 0)
    propellers.append(
        {
            "blade": box(
                pos=drone.pos + propeller_position,
                size=vector(0.48, 0.025, 0.065),
                axis=vector(1, 0, 0),
                up=vector(0, 1, 0),
                color=cyan if index % 2 == 0 else vector(0.95, 0.48, 0.2),
            ),
            "disk": cylinder(
                pos=drone.pos + propeller_position,
                axis=vector(0, 0.008, 0),
                radius=0.27,
                color=cyan,
                opacity=0.13,
            ),
            "local_position": propeller_position,
            "phase": index * math.pi / 2,
            "direction": 1 if index % 2 == 0 else -1,
        }
    )

panel_style = {
    "pixel_pos": True,
    "height": 16,
    "color": vector(0.82, 0.92, 0.97),
    "background": vector(0.035, 0.075, 0.11),
    "box": True,
    "line": False,
    "opacity": 0.94,
    "border": 14,
    "font": "monospace",
}
link_panel = label(
    pos=vector(24, scene.height - 24, 0),
    text="  SKYFRAME / FLIGHT DECK\n  LINK       CONNECTING\n  SOURCE     STM32 UART -> ESP32 TCP\n"
    "  PIPELINE   RAW / NO FILTERING\n  RX AGE     --",
    align="left",
    **panel_style,
)
telemetry_panel = label(
    pos=vector(scene.width - 24, scene.height - 24, 0),
    text="FLIGHT TELEMETRY\nROLL       0.00 deg\nPITCH      0.00 deg\n"
    "YAW        0.00 deg\nYAW RATE   0.00 deg/s\n"
    "ALTITUDE   0.00 / 2.00 m\nMODEL Y    0.22 m",
    align="right",
    **panel_style,
)

scene.caption = (
    "<div style='font-family:monospace;color:#9bb7c8;padding:10px 4px'>"
    "<b style='color:#20dce8'>FLIGHT ENVELOPE</b> &nbsp; "
    "4 m x 4 m x 2 m &nbsp; | &nbsp; "
    "<b style='color:#20dce8'>ALTITUDE LIMIT</b> &nbsp; 2.00 m &nbsp; | &nbsp; "
    "Attitude is displayed directly; altitude is clamped to the flight box."
    "</div>"
)

receiver = threading.Thread(target=receive_telemetry, daemon=True)
receiver.start()

current_yaw = 0.0
previous_time = time.monotonic()

try:
    while True:
        rate(60)
        now = time.monotonic()
        dt = now - previous_time
        previous_time = now

        with data_lock:
            roll_degrees, pitch_degrees, yaw_rate_degrees, altitude = latest_telemetry
            raw_line = latest_raw_line
            received_at = last_telemetry_at
            link_state = connection_state

        current_yaw += yaw_rate_degrees * dt
        roll = math.radians(roll_degrees)
        pitch = math.radians(pitch_degrees)
        yaw = math.radians(current_yaw)

        drone.axis = orient_vector(vector(1, 0, 0), roll, pitch, yaw)
        drone.up = orient_vector(vector(0, 1, 0), roll, pitch, yaw)

        limited_altitude = max(0.0, min(altitude, MAX_ALTITUDE))
        usable_height = ROOM_HEIGHT - 2 * DRONE_CLEARANCE
        model_height = DRONE_CLEARANCE + (
            limited_altitude / MAX_ALTITUDE
        ) * usable_height
        drone.pos = vector(0, model_height, 0)

        for propeller in propellers:
            propeller["phase"] += (
                propeller["direction"] * PROPELLER_SPEED * dt
            )
            phase = propeller["phase"]
            local_blade_axis = vector(math.cos(phase), 0, math.sin(phase))
            blade = propeller["blade"]
            blade.pos = drone.pos + orient_vector(
                propeller["local_position"], roll, pitch, yaw
            )
            blade.axis = orient_vector(local_blade_axis, roll, pitch, yaw)
            blade.up = orient_vector(vector(0, 1, 0), roll, pitch, yaw)

            disk = propeller["disk"]
            disk.pos = blade.pos
            disk.axis = orient_vector(vector(0, 0.008, 0), roll, pitch, yaw)

        sample_age = now - received_at if received_at else None
        display_state = link_state
        if link_state == "CONNECTED" and (
            sample_age is None or sample_age > 2.0
        ):
            display_state = "WAITING FOR DATA" if sample_age is None else "STALE"
        age_text = f"{sample_age:.1f} s" if sample_age is not None else "--"
        raw_preview = raw_line
        if len(raw_preview) > 54:
            raw_preview = raw_preview[:51] + "..."
        link_panel.text = (
            "  SKYFRAME / FLIGHT DECK\n"
            f"  LINK       {display_state}\n"
            "  SOURCE     STM32 UART -> ESP32 TCP\n"
            "  PIPELINE   RAW / NO FILTERING\n"
            f"  RX AGE     {age_text}\n"
            f"  RX FRAME   {raw_preview or 'Waiting for first sample'}"
        )
        telemetry_panel.text = (
            "FLIGHT TELEMETRY\n"
            f"ROLL       {roll_degrees:>8.2f} deg\n"
            f"PITCH      {pitch_degrees:>8.2f} deg\n"
            f"YAW        {current_yaw % 360:>8.2f} deg\n"
            f"YAW RATE   {yaw_rate_degrees:>8.2f} deg/s\n"
            f"ALTITUDE   {altitude:>5.2f} / {MAX_ALTITUDE:.2f} m\n"
            f"MODEL Y    {model_height:>5.2f} m"
            f"{'  LIMIT' if altitude != limited_altitude else ''}"
        )
except KeyboardInterrupt:
    pass
finally:
    stop_event.set()
