"""Visualize the unfiltered ESP32 drone telemetry stream."""

import math
import os
import socket
import threading
import time

from vpython import (
    box,
    canvas,
    cone,
    curve,
    cylinder,
    label,
    rate,
    sphere,
    vector,
)


ESP_IP = os.environ.get("SKYLINK_ESP_IP", "192.168.4.1").strip()
ESP_PORT = int(os.environ.get("SKYLINK_ESP_PORT", "8080"))
MAX_LINE_BYTES = 4096
VIEW_RADIUS = 1.5
GRID_SPACING = 0.5
GRID_EXTENT = 12.0
PROPELLER_SPEED = 72.0

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
    title="",
    width=1440,
    height=900,
    background=vector(0.025, 0.045, 0.07),
    align="left",
)
scene.center = vector(0, 0, 0)
scene.forward = vector(-1, -0.65, -1)
scene.range = VIEW_RADIUS
scene.autoscale = False
scene.userzoom = True
scene.userspin = True
scene.userpan = True

cyan = vector(0.2, 0.82, 0.9)
blue = vector(0.14, 0.28, 0.37)
carbon = vector(0.09, 0.12, 0.15)
shell = vector(0.18, 0.23, 0.28)
orange = vector(0.95, 0.36, 0.16)
drone_center = vector(0, 0, 0)

# Open ground reference only; the camera stays centered on the aircraft.
ground_level = -VIEW_RADIUS
for grid_index in range(-int(GRID_EXTENT / GRID_SPACING), int(GRID_EXTENT / GRID_SPACING) + 1):
    grid_position = grid_index * GRID_SPACING
    axis_color = vector(0.25, 0.52, 0.6) if grid_index == 0 else blue
    line_radius = 0.003 if grid_index == 0 else 0.0015
    curve(
        pos=[
            vector(-GRID_EXTENT, ground_level, grid_position),
            vector(GRID_EXTENT, ground_level, grid_position),
        ],
        color=axis_color,
        radius=line_radius,
    )
    curve(
        pos=[
            vector(grid_position, ground_level, -GRID_EXTENT),
            vector(grid_position, ground_level, GRID_EXTENT),
        ],
        color=axis_color,
        radius=line_radius,
    )

drone_body = box(
    pos=drone_center,
    size=vector(0.34, 0.12, 0.28),
    color=shell,
)
drone_deck = box(
    pos=drone_center + vector(0, 0.07, 0),
    size=vector(0.25, 0.035, 0.23),
    color=carbon,
)
drone_battery = box(
    pos=drone_center + vector(-0.02, 0.125, 0),
    size=vector(0.2, 0.075, 0.17),
    color=vector(0.12, 0.16, 0.19),
)
drone_nose = cone(
    pos=drone_center + vector(0.12, -0.005, 0),
    axis=vector(0.18, 0, 0),
    radius=0.075,
    color=orange,
)
camera_mount = cylinder(
    pos=drone_center + vector(0.105, -0.085, 0),
    axis=vector(0, -0.045, 0),
    radius=0.025,
    color=carbon,
)
front_camera = sphere(
    pos=drone_center + vector(0.13, -0.13, 0),
    radius=0.055,
    color=vector(0.08, 0.12, 0.15),
)
camera_lens = sphere(
    pos=drone_center + vector(0.177, -0.13, 0),
    radius=0.027,
    color=cyan,
)

airframe_parts = [
    (drone_body, vector(0, 0, 0), vector(1, 0, 0), vector(0, 1, 0)),
    (drone_deck, vector(0, 0.07, 0), vector(1, 0, 0), vector(0, 1, 0)),
    (drone_battery, vector(-0.02, 0.125, 0), vector(1, 0, 0), vector(0, 1, 0)),
    (
        drone_nose,
        vector(0.12, -0.005, 0),
        vector(0.18, 0, 0),
        vector(0, 1, 0),
    ),
    (
        camera_mount,
        vector(0.105, -0.085, 0),
        vector(0, -0.045, 0),
        vector(1, 0, 0),
    ),
    (front_camera, vector(0.13, -0.13, 0), vector(1, 0, 0), vector(0, 1, 0)),
    (camera_lens, vector(0.177, -0.13, 0), vector(1, 0, 0), vector(0, 1, 0)),
]
for rotor_x, rotor_z in ((0.48, 0.48), (0.48, -0.48), (-0.48, 0.48), (-0.48, -0.48)):
    rotor_position = vector(rotor_x, 0, rotor_z)
    arm = cylinder(
        pos=drone_center,
        axis=rotor_position,
        radius=0.027,
        color=carbon,
    )
    airframe_parts.append(
        (arm, vector(0, 0, 0), rotor_position, vector(0, 1, 0))
    )

rotors = []
for index, (rotor_x, rotor_z) in enumerate(
    ((0.48, 0.48), (0.48, -0.48), (-0.48, 0.48), (-0.48, -0.48))
):
    rotor_position = vector(rotor_x, 0, rotor_z)
    motor = cylinder(
        pos=drone_center + rotor_position + vector(0, 0.025, 0),
        axis=vector(0, 0.09, 0),
        radius=0.075,
        color=shell,
    )
    airframe_parts.append(
        (
            motor,
            rotor_position + vector(0, 0.025, 0),
            vector(0, 0.09, 0),
            vector(1, 0, 0),
        )
    )
    motor_cap = cylinder(
        pos=drone_center + rotor_position + vector(0, 0.115, 0),
        axis=vector(0, 0.025, 0),
        radius=0.052,
        color=carbon,
    )
    airframe_parts.append(
        (
            motor_cap,
            rotor_position + vector(0, 0.115, 0),
            vector(0, 0.025, 0),
            vector(1, 0, 0),
        )
    )
    blades = []
    blade_tips = []
    for direction in (-1, 1):
        blades.append(
            box(
                pos=drone_center + rotor_position + vector(direction * 0.12, 0.15, 0),
                size=vector(0.24, 0.014, 0.042),
                color=vector(0.3, 0.37, 0.42),
            )
        )
        blade_tips.append(
            sphere(
                pos=drone_center + rotor_position + vector(direction * 0.23, 0.15, 0),
                radius=0.018,
                color=vector(0.75, 0.45, 0.2),
            )
        )
    rotors.append(
        {
            "position": rotor_position,
            "phase": index * math.pi / 2,
            "direction": 1 if index % 2 == 0 else -1,
            "motor": motor,
            "cap": motor_cap,
            "blades": blades,
            "blade_tips": blade_tips,
        }
    )

navigation_lights = []
for position_x, position_y, position_z, light_color in (
    (0.48, 0.02, 0.48, vector(0.95, 0.2, 0.16)),
    (0.48, 0.02, -0.48, vector(0.95, 0.2, 0.16)),
    (-0.48, 0.02, 0.48, vector(0.1, 0.82, 0.43)),
    (-0.48, 0.02, -0.48, vector(0.1, 0.82, 0.43)),
):
    light_position = vector(position_x, position_y, position_z)
    navigation_lights.append(
        (
            sphere(
                pos=drone_center + light_position,
                radius=0.035,
                color=light_color,
                emissive=True,
            ),
            light_position,
        )
    )

panel_style = {
    "pixel_pos": True,
    "height": 20,
    "color": vector(0.86, 0.93, 0.97),
    "background": vector(0.035, 0.07, 0.1),
    "box": True,
    "line": False,
    "opacity": 0.93,
    "border": 16,
    "font": "sans",
}
link_panel = label(
    pos=vector(28, scene.height - 24, 0),
    text=(
        "SKYLINK  /  FLIGHT TELEMETRY\n"
        "LINK  ·  CONNECTING\n"
        f"TCP  ·  {normalize_esp_ip(ESP_IP)}:{ESP_PORT}\n"
        "RX AGE  ·  --"
    ),
    align="left",
    **panel_style,
)
telemetry_panel = label(
    pos=vector(scene.width - 28, scene.height - 24, 0),
    text="LIVE FLIGHT DATA\nROLL       0.00°\nPITCH      0.00°\n"
    "YAW RATE   0.00°/s\nALTITUDE   0.00 m",
    align="right",
    **panel_style,
)

scene.title = (
    "<div id='skylink-header' style='display:flex;align-items:center;"
    "justify-content:space-between;"
    "padding:13px 22px;background:#08121d;border-bottom:1px solid #203443;"
    "color:#e5edf3;font:600 16px/1.2 system-ui,sans-serif;letter-spacing:.14em'>"
    "<span id='skylink-brand'>SKYLINK <span style='color:#8293a2;font-size:11px;"
    "letter-spacing:.18em'>&nbsp; FLIGHT SYSTEMS</span></span>"
    "<span id='skylink-controls' style='color:#8293a2;font-size:10px;"
    "letter-spacing:.08em'>"
    "DRAG TO ORBIT &nbsp;·&nbsp; SCROLL TO ZOOM &nbsp;·&nbsp; RIGHT-DRAG TO PAN"
    "</span></div>"
)
scene.caption = (
    "<style>"
    "html,body{height:100%;margin:0!important;overflow:hidden!important;"
    "background:#07111b!important;color:#dce7ee!important}"
    "body{font-family:system-ui,-apple-system,'Segoe UI',sans-serif!important}"
    ".glowscript{width:100vw!important;height:100vh!important;"
    "max-width:none!important;margin:0!important;"
    "padding:0!important;background:#07111b!important}"
    ".glowscript canvas{display:block;width:100vw!important;"
    "height:calc(100vh - 60px)!important}"
    "@media(max-width:700px){#skylink-header{padding:12px 14px!important}"
    "#skylink-controls{font-size:0!important}"
    "#skylink-controls:after{content:'ORBIT · ZOOM · PAN';font-size:9px;"
    "letter-spacing:.04em}}"
    "</style>"
)

print(f"ESP32 telemetry source: {normalize_esp_ip(ESP_IP)}:{ESP_PORT}")
print("Opening the local SkyLink dashboard in your browser.")

receiver = threading.Thread(target=receive_telemetry, daemon=True)
receiver.start()

current_yaw = 0.0
previous_time = time.monotonic()

try:
    while True:
        rate(60)
        now = time.monotonic()
        dt = min(now - previous_time, 0.1)
        previous_time = now

        with data_lock:
            roll_degrees, pitch_degrees, yaw_rate_degrees, altitude = latest_telemetry
            raw_line = latest_raw_line
            received_at = last_telemetry_at
            link_state = connection_state

        sample_age = now - received_at if received_at else None
        if sample_age is not None and sample_age <= 2.0:
            current_yaw = (current_yaw + yaw_rate_degrees * dt) % 360
        roll = math.radians(roll_degrees)
        pitch = math.radians(pitch_degrees)
        yaw = math.radians(current_yaw)

        for part, local_position, local_axis, local_up in airframe_parts:
            part.pos = drone_center + orient_vector(
                local_position, roll, pitch, yaw
            )
            part.axis = orient_vector(local_axis, roll, pitch, yaw)
            part.up = orient_vector(local_up, roll, pitch, yaw)

        for light, local_position in navigation_lights:
            light.pos = drone_center + orient_vector(
                local_position, roll, pitch, yaw
            )

        for rotor in rotors:
            rotor["phase"] += rotor["direction"] * PROPELLER_SPEED * dt
            phase = rotor["phase"]
            blade_axis = vector(math.cos(phase), 0, math.sin(phase))
            oriented_blade_axis = orient_vector(blade_axis, roll, pitch, yaw)
            rotor_center = drone_center + orient_vector(
                rotor["position"], roll, pitch, yaw
            )
            motor_up = orient_vector(vector(0, 1, 0), roll, pitch, yaw)
            for blade, tip, direction in zip(
                rotor["blades"], rotor["blade_tips"], (-1, 1)
            ):
                blade.pos = (
                    rotor_center
                    + motor_up * 0.15
                    + oriented_blade_axis * (direction * 0.12)
                )
                blade.axis = oriented_blade_axis
                blade.up = motor_up
                tip.pos = (
                    rotor_center
                    + motor_up * 0.15
                    + oriented_blade_axis * (direction * 0.23)
                )

        display_state = link_state
        if link_state == "CONNECTED" and (
            sample_age is None or sample_age > 2.0
        ):
            display_state = "WAITING FOR DATA" if sample_age is None else "STALE"
        age_text = f"{sample_age:.1f} s" if sample_age is not None else "--"
        link_panel.text = (
            "SKYLINK  /  FLIGHT TELEMETRY\n"
            f"LINK  ·  {display_state}\n"
            f"TCP  ·  {normalize_esp_ip(ESP_IP)}:{ESP_PORT}\n"
            f"RX AGE  ·  {age_text}"
        )
        telemetry_panel.text = (
            "LIVE FLIGHT DATA\n"
            f"ROLL       {roll_degrees:>8.2f}°\n"
            f"PITCH      {pitch_degrees:>8.2f}°\n"
            f"YAW RATE   {yaw_rate_degrees:>8.2f}°/s\n"
            f"ALTITUDE   {altitude:>8.2f} m"
        )
except KeyboardInterrupt:
    pass
finally:
    stop_event.set()
