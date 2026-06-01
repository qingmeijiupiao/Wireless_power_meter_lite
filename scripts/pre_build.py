from __future__ import annotations

import gzip
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
    generate_web_gzip_files()
