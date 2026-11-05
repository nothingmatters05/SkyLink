from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from html.parser import HTMLParser
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WEB_DIR = ROOT / "web"
DIST_DIR = WEB_DIR / "dist"
DATA_DIR = ROOT / "firmware" / "esp32_websocket_bridge" / "data"
IMAGE_DIR = ROOT / "firmware" / "esp32_websocket_bridge"
IMAGE_PATH = IMAGE_DIR / "spiffs.bin"
PIO_IMAGE = ROOT / ".pio" / "build" / "skylink-esp32" / "spiffs.bin"
PARTITION_SIZE = 0x1E0000
MAX_PATH_BYTES = 30


class AssetReferences(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.references: set[str] = set()

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        for key, value in attrs:
            if key in {"src", "href"} and value and value.startswith("/"):
                self.references.add(value)


def run(command: list[str], cwd: Path, label: str) -> str:
    environment = os.environ.copy()
    environment["PYTHONIOENCODING"] = "utf-8"
    environment["PYTHONUTF8"] = "1"
    result = subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    print(result.stdout, end="")
    if result.returncode:
        raise RuntimeError(f"{label} failed with exit code {result.returncode}.")
    return result.stdout


def pio_executable() -> Path:
    candidate = shutil.which("pio") or shutil.which("platformio")
    if candidate:
        return Path(candidate)
    if os.name == "nt":
        candidates = [
            Path.home() / ".platformio" / "penv" / "Scripts" / "pio.exe",
            Path.home() / ".platformio" / "penv" / "Scripts" / "platformio.exe",
        ]
    else:
        candidates = [Path.home() / ".platformio" / "penv" / "bin" / "pio"]
    for path in candidates:
        if path.is_file():
            return path
    raise FileNotFoundError(
        "PlatformIO Core was not found. Install the VS Code PlatformIO IDE "
        "extension or add the pio executable to PATH."
    )


def files_below(directory: Path) -> dict[str, Path]:
    return {
        path.relative_to(directory).as_posix(): path
        for path in directory.rglob("*")
        if path.is_file()
    }


def validate_site(directory: Path) -> set[str]:
    files = files_below(directory)
    if "index.html" not in files:
        raise RuntimeError(f"Missing index.html in {directory}.")

    total_bytes = sum(path.stat().st_size for path in files.values())
    if total_bytes > PARTITION_SIZE:
        raise RuntimeError(
            f"Website content is {total_bytes} bytes, above the "
            f"{PARTITION_SIZE}-byte SPIFFS partition."
        )

    for relative in files:
        spiffs_path = f"/{relative}"
        path_bytes = len(spiffs_path.encode("utf-8"))
        if path_bytes > MAX_PATH_BYTES:
            raise RuntimeError(
                f"SPIFFS path is {path_bytes} bytes (limit {MAX_PATH_BYTES}): "
                f"{spiffs_path}"
            )

    parser = AssetReferences()
    parser.feed(files["index.html"].read_text(encoding="utf-8"))
    references = parser.references
    if not references:
        raise RuntimeError("index.html does not reference any local assets.")
    for reference in references:
        relative = reference.lstrip("/")
        if relative not in files:
            raise RuntimeError(f"index.html references missing file {reference}.")
    return set(files)


def clear_generated_data() -> None:
    if (
        (ROOT / "firmware").is_symlink()
        or IMAGE_DIR.is_symlink()
        or DATA_DIR.is_symlink()
    ):
        raise RuntimeError("Refusing to prepare SPIFFS through a symbolic link.")
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    resolved_data = DATA_DIR.resolve()
    expected_data = (ROOT / "firmware" / "esp32_websocket_bridge" / "data").resolve()
    if resolved_data != expected_data:
        raise RuntimeError(f"Refusing to clear unexpected data directory: {resolved_data}")
    for child in DATA_DIR.iterdir():
        if child.is_dir() and not child.is_symlink():
            shutil.rmtree(child)
        else:
            child.unlink()


def find_mkspiffs() -> Path:
    platformio_home = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    packages = platformio_home / "packages"
    executable_names = (
        ("mkspiffs_espressif32_arduino.exe", "mkspiffs.exe")
        if os.name == "nt"
        else ("mkspiffs_espressif32_arduino", "mkspiffs")
    )
    for executable_name in executable_names:
        candidates = sorted(packages.glob(f"tool-mkspiffs*/**/{executable_name}"))
        if candidates:
            return candidates[-1]
    raise FileNotFoundError(
        f"PlatformIO's ESP32 Arduino mkspiffs executable was not found under {packages}."
    )


def list_image(mkspiffs: Path, image: Path) -> set[str]:
    output = run([str(mkspiffs), "-l", str(image)], ROOT, "SPIFFS image listing")
    if re.search(r"SPIFFS_write error|error adding file", output, re.IGNORECASE):
        raise RuntimeError("mkspiffs reported a filesystem write error.")
    paths = {
        match.group(1).replace("\\", "/")
        for line in output.splitlines()
        if (match := re.match(r"^\s*\d+\s+([/\\][^\s]+)\s*$", line))
    }
    return paths


def main() -> None:
    if not DIST_DIR.is_dir():
        raise RuntimeError(f"Production output does not exist: {DIST_DIR}")
    if IMAGE_PATH.is_symlink():
        raise RuntimeError(f"Refusing to replace a symbolic-link filesystem image: {IMAGE_PATH}")

    expected_files = validate_site(DIST_DIR)
    print(
        f"Validated {len(expected_files)} production files "
        f"({sum(path.stat().st_size for path in files_below(DIST_DIR).values())} bytes)."
    )

    clear_generated_data()
    shutil.copytree(DIST_DIR, DATA_DIR, dirs_exist_ok=True)
    copied_files = validate_site(DATA_DIR)
    if copied_files != expected_files:
        raise RuntimeError("The SPIFFS data directory does not match web/dist.")
    print(f"Prepared {len(copied_files)} files in {DATA_DIR}.")

    pio = pio_executable()
    build_output = run(
        [str(pio), "run", "-e", "skylink-esp32", "-t", "buildfs"],
        ROOT,
        "PlatformIO SPIFFS build",
    )
    if re.search(r"SPIFFS_write error|error adding file", build_output, re.IGNORECASE):
        raise RuntimeError("PlatformIO emitted an SPIFFS write error.")
    if not PIO_IMAGE.is_file():
        raise FileNotFoundError(f"PlatformIO did not create {PIO_IMAGE}.")
    if PIO_IMAGE.stat().st_size != PARTITION_SIZE:
        raise RuntimeError(
            f"PlatformIO image size is {PIO_IMAGE.stat().st_size}, "
            f"expected {PARTITION_SIZE} bytes."
        )

    mkspiffs = find_mkspiffs()
    print(f"Listing image with {mkspiffs}.")
    with tempfile.NamedTemporaryFile(
        prefix="spiffs.candidate-",
        suffix=".bin",
        dir=IMAGE_DIR,
        delete=False,
    ) as candidate_file:
        candidate = Path(candidate_file.name)
    backup: Path | None = None
    try:
        shutil.copy2(PIO_IMAGE, candidate)
        if candidate.stat().st_size != PARTITION_SIZE:
            raise RuntimeError("Candidate filesystem image has an unexpected size.")
        listed_files = list_image(mkspiffs, candidate)
        expected_paths = {f"/{relative}" for relative in expected_files}
        if listed_files != expected_paths:
            missing = sorted(expected_paths - listed_files)
            extra = sorted(listed_files - expected_paths)
            raise RuntimeError(
                f"SPIFFS listing mismatch. Missing: {missing}; unexpected: {extra}."
            )

        if IMAGE_PATH.exists():
            with tempfile.NamedTemporaryFile(
                prefix="spiffs.previous-",
                suffix=".bin",
                dir=IMAGE_DIR,
                delete=False,
            ) as backup_file:
                backup = Path(backup_file.name)
            shutil.copy2(IMAGE_PATH, backup)

        os.replace(candidate, IMAGE_PATH)
        if IMAGE_PATH.stat().st_size != PARTITION_SIZE:
            raise RuntimeError("Installed SPIFFS image has an unexpected size.")
        if list_image(mkspiffs, IMAGE_PATH) != expected_paths:
            raise RuntimeError("Installed SPIFFS image failed its final listing check.")
        print(
            f"Verified {len(expected_paths)} files; "
            f"{IMAGE_PATH.stat().st_size} byte image installed at {IMAGE_PATH}."
        )
    except Exception:
        if backup and backup.exists():
            os.replace(backup, IMAGE_PATH)
            backup = None
        raise
    finally:
        candidate.unlink(missing_ok=True)
        if backup and backup.exists():
            backup.unlink()


if __name__ == "__main__":
    try:
        main()
    except (FileNotFoundError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1) from error