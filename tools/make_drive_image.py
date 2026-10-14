"""Build the read-only FAT12 disk image that the mouse shows in drive mode.

The image contains tools/configurator.html and a short README.txt and is written
to firmware/lib/config_drive/drive_image.h as a byte array. PlatformIO runs this
before every build (extra_scripts in platformio.ini); it can also be run directly:

    python tools/make_drive_image.py

The header is only rewritten when its content changes, so unchanged pages do not
trigger a rebuild.
"""

import os
import struct
import time

BLOCK_SIZE = 512
BLOCK_COUNT = 2048        # 1 MiB volume; only the used part is stored in flash
ROOT_ENTRIES = 16         # one sector
NUM_FATS = 2
VOLUME_LABEL = b"CADMOUSE   "
VOLUME_ID = 0xCAD0C0DE

README = (
    "CAD Mouse MK2 - configurator\r\n"
    "\r\n"
    "Open configurator.html in Google Chrome or Microsoft Edge and click\r\n"
    "\"Подключить мышь\". The page talks to the mouse over USB (Web Serial).\r\n"
    "\r\n"
    "This drive appears only when a button is held while plugging the mouse in.\r\n"
    "Unplug and plug the mouse in again without holding a button to hide it.\r\n"
).encode("utf-8")


def project_dir():
    try:
        Import("env")  # noqa: F821 - provided by PlatformIO when run as an extra script
        return env.subst("$PROJECT_DIR")  # noqa: F821
    except NameError:
        return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def fat_datetime(timestamp):
    t = time.localtime(timestamp)
    year = max(t.tm_year, 1980)
    date = ((year - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday
    clock = (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)
    return date, clock


def short_name_checksum(short_name):
    checksum = 0
    for byte in short_name:
        checksum = (((checksum & 1) << 7) + (checksum >> 1) + byte) & 0xFF
    return checksum


def lfn_entries(long_name, short_name):
    """Long file name entries, in the order they are stored (last part first)."""
    units = list(long_name.encode("utf-16-le"))
    chars = [units[i] | (units[i + 1] << 8) for i in range(0, len(units), 2)]
    parts = [chars[i:i + 13] for i in range(0, len(chars), 13)]
    if len(parts[-1]) < 13:
        parts[-1] = parts[-1] + [0x0000] + [0xFFFF] * (12 - len(parts[-1]))
    checksum = short_name_checksum(short_name)

    entries = []
    for index, part in enumerate(parts, start=1):
        sequence = index | (0x40 if index == len(parts) else 0)
        entry = struct.pack("<B5HBBB6HH2H", sequence, *part[0:5], 0x0F, 0, checksum, *part[5:11], 0, *part[11:13])
        entries.append(entry)
    return list(reversed(entries))


def short_entry(name, attributes, nt_flags, cluster, size, date, clock):
    return struct.pack("<11sBBBHHHHHHHI", name, attributes, nt_flags, 0, clock, date, date, 0, clock, date, cluster, size)


def build_image(files, timestamp):
    # FAT12 layout: boot sector, FATs, root directory, data (one sector per cluster)
    clusters = BLOCK_COUNT
    while True:
        fat_sectors = -(-((clusters + 2) * 3 // 2) // BLOCK_SIZE)
        data_start = 1 + NUM_FATS * fat_sectors + ROOT_ENTRIES * 32 // BLOCK_SIZE
        new_clusters = BLOCK_COUNT - data_start
        if new_clusters == clusters:
            break
        clusters = new_clusters
    assert clusters < 4085, "too many clusters for FAT12"

    date, clock = fat_datetime(timestamp)

    boot = bytearray(BLOCK_SIZE)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"MSWIN4.1"
    struct.pack_into("<HBHBHHBHHHII", boot, 11,
                     BLOCK_SIZE, 1, 1, NUM_FATS, ROOT_ENTRIES, BLOCK_COUNT, 0xF8,
                     fat_sectors, 1, 1, 0, 0)
    struct.pack_into("<BBBI11s8s", boot, 36, 0x80, 0, 0x29, VOLUME_ID, VOLUME_LABEL, b"FAT12   ")
    boot[62:64] = b"\xEB\xFE"  # not bootable: spin
    boot[510:512] = b"\x55\xAA"

    fat = [0xFF8, 0xFFF] + [0] * clusters
    root = [short_entry(VOLUME_LABEL, 0x08, 0, 0, 0, date, clock)]
    data = bytearray()
    next_cluster = 2

    for long_name, short_name, nt_flags, content in files:
        count = max(1, -(-len(content) // BLOCK_SIZE))
        first = next_cluster
        for cluster in range(first, first + count):
            fat[cluster] = cluster + 1 if cluster < first + count - 1 else 0xFFF
        next_cluster += count
        if long_name:
            root.extend(lfn_entries(long_name, short_name))
        root.append(short_entry(short_name, 0x21, nt_flags, first, len(content), date, clock))  # read-only, archive
        data += content + bytes(count * BLOCK_SIZE - len(content))

    assert len(root) <= ROOT_ENTRIES, "root directory full"
    assert next_cluster - 2 <= clusters, "disk full"

    fat_bytes = bytearray(fat_sectors * BLOCK_SIZE)
    for i in range(0, len(fat) - 1, 2):
        a, b = fat[i], fat[i + 1]
        offset = i * 3 // 2
        fat_bytes[offset] = a & 0xFF
        fat_bytes[offset + 1] = ((a >> 8) & 0x0F) | ((b & 0x0F) << 4)
        fat_bytes[offset + 2] = (b >> 4) & 0xFF

    root_bytes = b"".join(root)
    root_bytes += bytes(ROOT_ENTRIES * 32 - len(root_bytes))

    image = bytes(boot) + bytes(fat_bytes) * NUM_FATS + root_bytes + bytes(data)
    assert len(image) == (data_start + next_cluster - 2) * BLOCK_SIZE
    return image


def render_header(image):
    lines = [
        "// Generated by tools/make_drive_image.py from tools/configurator.html. Do not edit.",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "static const uint32_t DRIVE_BLOCK_COUNT = %d;" % BLOCK_COUNT,
        "static const uint32_t DRIVE_IMAGE_SIZE = %d;" % len(image),
        "static const uint8_t DRIVE_IMAGE[] = {",
    ]
    for i in range(0, len(image), 24):
        lines.append("  " + ",".join("0x%02x" % b for b in image[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    root = project_dir()
    page_path = os.path.join(root, "tools", "configurator.html")
    header_path = os.path.join(root, "firmware", "lib", "config_drive", "drive_image.h")

    with open(page_path, "rb") as f:
        page = f.read()
    files = [
        ("configurator.html", b"CONFIG~1HTM", 0, page),
        (None, b"README  TXT", 0x10, README),  # 0x10: show the extension in lower case
    ]
    image = build_image(files, os.path.getmtime(page_path))
    header = render_header(image)

    old = None
    if os.path.exists(header_path):
        with open(header_path, "r", encoding="utf-8") as f:
            old = f.read()
    if old != header:
        with open(header_path, "w", encoding="utf-8", newline="\n") as f:
            f.write(header)
        print("make_drive_image: wrote %s (%d bytes of disk image)" % (os.path.relpath(header_path, root), len(image)))


main()
