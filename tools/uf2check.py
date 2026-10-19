#!/usr/bin/env python3
"""Check that a UF2 file is safe to copy onto the T1000-E bootloader drive.

Accepts the file only if every block:
  - is a valid UF2 block for the nRF52840 family 0xADA52840,
  - writes inside the application area 0x27000-0xEA000
    (below: MBR + SoftDevice S140 7.3.0, above: app data incl. InternalFS, then the bootloader),
and the first block starts exactly at 0x27000.

Usage: uf2check.py FILE.uf2      (exit code 0 = OK, 1 = rejected)
"""
import hashlib
import struct
import sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
FLAG_NOT_MAIN_FLASH = 0x00000001
FLAG_FAMILY_ID = 0x00002000

FAMILY = 0xADA52840
APP_START = 0x27000  # first byte after S140 7.3.0
# USER_FLASH_END of the Adafruit nRF52 bootloader: 0xF4000 - DFU_APP_DATA_RESERVED (10*4096 for
# nRF52840, Makefile). The bootloader rejects UF2 blocks from here on; it would accept blocks down
# to 0x1000, i.e. over the SoftDevice, so APP_START above is what protects the SoftDevice.
APP_END = 0xEA000
PAYLOAD = 256


def check(path):
    try:
        data = open(path, "rb").read()
    except OSError as e:
        return b"", None, [f"cannot read file: {e.strerror}"]
    errors = []
    if not data or len(data) % 512:
        return data, None, [f"file size {len(data)} is not a non-zero multiple of 512"]

    count = len(data) // 512
    blocks = []
    for i in range(count):
        blk = data[i * 512 : (i + 1) * 512]
        m0, m1, flags, addr, size, block_no, num_blocks, family = struct.unpack("<8I", blk[:32])
        (m_end,) = struct.unpack("<I", blk[508:512])
        where = f"block {i}"
        if (m0, m1, m_end) != (UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_MAGIC_END):
            errors.append(f"{where}: bad UF2 magic")
            continue
        if flags & FLAG_NOT_MAIN_FLASH:
            errors.append(f"{where}: 'not main flash' flag set")
        if not flags & FLAG_FAMILY_ID:
            errors.append(f"{where}: no family ID")
        elif family != FAMILY:
            errors.append(f"{where}: family 0x{family:08X}, expected 0x{FAMILY:08X}")
        if size != PAYLOAD:
            errors.append(f"{where}: payload {size} B, expected {PAYLOAD}")
        if block_no != i or num_blocks != count:
            errors.append(f"{where}: numbering {block_no}/{num_blocks}, expected {i}/{count}")
        if addr < APP_START or addr + size > APP_END:
            errors.append(f"{where}: address 0x{addr:05X} outside 0x{APP_START:05X}-0x{APP_END:05X}")
        blocks.append((addr, size, family))

    if blocks and blocks[0][0] != APP_START:
        errors.append(f"first block at 0x{blocks[0][0]:05X}, expected 0x{APP_START:05X}")
    return data, blocks, errors


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    data, blocks, errors = check(path)

    print(f"uf2check: {path}")
    if blocks:
        lo = min(a for a, _, _ in blocks)
        hi = max(a + s for a, s, _ in blocks)
        families = sorted({f for _, _, f in blocks})
        print(f"  blocks:       {len(blocks)}  ({len(data)} B file, sha256 {hashlib.sha256(data).hexdigest()[:16]})")
        print(f"  family:       {', '.join(f'0x{f:08X}' for f in families)}  (expected 0x{FAMILY:08X})")
        print(f"  first block:  0x{blocks[0][0]:05X}  (expected 0x{APP_START:05X})")
        print(f"  range:        0x{lo:05X} - 0x{hi:05X}  (allowed 0x{APP_START:05X} - 0x{APP_END:05X})")
        print(f"  app size:     {(hi - lo) / 1024:.1f} KiB of {(APP_END - APP_START) / 1024:.0f} KiB")
    if errors:
        shown = errors[:10]
        for e in shown:
            print(f"  ERROR: {e}")
        if len(errors) > len(shown):
            print(f"  ... and {len(errors) - len(shown)} more errors")
        print("  RESULT: REJECTED - do not copy this file to the card")
        return 1
    print("  RESULT: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
