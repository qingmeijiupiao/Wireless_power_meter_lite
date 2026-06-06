from __future__ import annotations

import gzip
import re
from pathlib import Path


WEB_FILES = (
    "index.html",
    "charts.html",
    "control.html",
    "status.html",
    "logs.html",
    "blackbox.html",
    "firmware.html",
    "provision.html",
    "app.css",
)

VERSION_PATTERN = re.compile(
    r'set\(VERSION_(MAJOR|MINOR)\s+(\d+)\s+CACHE\s+STRING'
)


def read_release_version(project_dir: Path) -> str:
    cmake_text = (project_dir / "CMakeLists.txt").read_text(encoding="utf-8")
    values = {name: value for name, value in VERSION_PATTERN.findall(cmake_text)}
    if "MAJOR" not in values or "MINOR" not in values:
        raise RuntimeError("VERSION_MAJOR or VERSION_MINOR missing from CMakeLists.txt")
    return f"{values['MAJOR']}.{values['MINOR']}.0"


def synchronize_launchpad_config(project_dir: Path) -> None:
    config_path = project_dir / ".github" / "workflows" / "config.toml"
    version = read_release_version(project_dir)
    text = config_path.read_text(encoding="utf-8")

    updated = re.sub(
        r'(?m)^(firmware_images_url\s*=\s*".*/download/)v\d+\.\d+\.\d+/?(")$',
        rf"\g<1>v{version}/\g<2>",
        text,
        count=1,
    )
    updated = re.sub(
        r'(?m)^image\.esp32c3\s*=\s*"Wireless_power_meter_lite_merged_v\d+\.\d+\.\d+\.bin"$',
        f'image.esp32c6 = "Wireless_power_meter_lite_merged_v{version}.bin"',
        updated,
        count=1,
    )
    updated = re.sub(
        r'(?m)^image\.esp32c6\s*=\s*"Wireless_power_meter_lite_merged_v\d+\.\d+\.\d+\.bin"$',
        f'image.esp32c6 = "Wireless_power_meter_lite_merged_v{version}.bin"',
        updated,
        count=1,
    )
    updated = re.sub(
        r'(?m)^chipsets\s*=\s*\["esp32c3"\]$',
        'chipsets = ["esp32c6"]',
        updated,
        count=1,
    )

    expected = (
        f"/download/v{version}/" in updated
        and f'image.esp32c6 = "Wireless_power_meter_lite_merged_v{version}.bin"' in updated
        and 'chipsets = ["esp32c6"]' in updated
    )
    if not expected:
        raise RuntimeError("config.toml layout is unsupported or synchronization failed")

    if updated != text:
        config_path.write_text(updated, encoding="utf-8", newline="\n")
        print(f">>> [pre-build] synchronized Launchpad config to v{version}")
    else:
        print(f">>> [pre-build] Launchpad config already matches v{version}")


def generate_web_gzip_files() -> None:
    project_dir = Path(__file__).resolve().parent.parent
    web_dir = project_dir / "components" / "assets" / "web_file"

    raw_size = 0
    gzip_size = 0
    updated_count = 0
    for name in WEB_FILES:
        source = web_dir / name
        output = web_dir / f"{name}.gz"
        data = source.read_bytes()
        compressed = gzip.compress(data, compresslevel=9, mtime=0)
        if not output.exists() or output.read_bytes() != compressed:
            output.write_bytes(compressed)
            updated_count += 1
        raw_size += len(data)
        gzip_size += len(compressed)

    print(
        f">>> [pre-build] synchronized {len(WEB_FILES)} gzip web assets "
        f"({updated_count} updated): {raw_size} -> {gzip_size} bytes"
    )


if __name__ == "__main__":
    synchronize_launchpad_config(Path(__file__).resolve().parent.parent)
    generate_web_gzip_files()
