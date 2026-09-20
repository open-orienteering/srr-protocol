#!/usr/bin/env python3
"""Decode SPORTident SRR frame payloads from hex.

Mirrors the field layout in docs/protocol.md and the parsing done by the
reference Arduino receiver. Intended for poking at hex dumps collected with
DUMP_RAW_PACKETS enabled, e.g.

    $ python3 decode_srr.py 73696F6B 00000B76 ... 
    $ python3 decode_srr.py --raw-log capture.txt      # lines like "[RED] RAW 27: 73 69 ..."

Run with --selftest to check the decoder against synthetic frames.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass, field
from typing import Optional

MAGIC = b"siok"
TYPE_PASSIVE = 0xB6  # station-originated punch
TYPE_ACTIVE = 0xB7   # SIAC-originated punch

# Offsets into the payload (first byte after the length byte).
OFF_LINK_ID = 4
OFF_TYPE = 14
B6_CN, B6_CARD, B6_TIME = 19, 20, 24
B7_CARD, B7_MODE, B7_TIME = 15, 19, 22

ACK_MID = bytes.fromhex("7F15000D3F23")
ACK_TAIL = bytes.fromhex("7360")
ACK_LEN = 13

ACTIVE_MODE_NAMES = {
    0x07: "CLEAR",
    0x1A: "CHECK",
    0x0B: "START",
    0x0D: "FINISH",
    0xB2: "CONTROL",
}


def si_time(seconds: int, subsec: int) -> str:
    """16-bit seconds in a 12 h half-day + 1/256 s -> hh:mm:ss.mmm."""
    ms = subsec * 1000 // 256
    return f"{seconds // 3600:02d}:{seconds % 3600 // 60:02d}:{seconds % 60:02d}.{ms:03d}"


@dataclass
class Frame:
    kind: str
    payload: bytes
    fields: dict = field(default_factory=dict)
    note: Optional[str] = None

    def __str__(self) -> str:
        parts = [self.kind]
        parts += [f"{k}={v}" for k, v in self.fields.items()]
        if self.note:
            parts.append(f"({self.note})")
        return " | ".join(parts)


def decode(payload: bytes) -> Frame:
    """Decode one SRR payload (without the CC2500 length byte)."""
    if len(payload) == ACK_LEN and payload[4:10] == ACK_MID and payload[11:13] == ACK_TAIL:
        return Frame("ACK", payload, {
            "link_id": struct.unpack(">I", payload[0:4])[0],
            "seq": payload[10],
        })

    if len(payload) <= OFF_TYPE or payload[:4] != MAGIC:
        return Frame("NON-SRR", payload, {"len": len(payload)})

    ptype = payload[OFF_TYPE]
    link_id = struct.unpack(">I", payload[OFF_LINK_ID:OFF_LINK_ID + 4])[0]
    fields = {"link_id": link_id, "type": f"0x{ptype:02X}", "hdr8_13": payload[8:14].hex(" ")}

    if ptype == TYPE_PASSIVE and len(payload) >= B6_TIME + 3:
        seconds = struct.unpack(">H", payload[B6_TIME:B6_TIME + 2])[0]
        fields.update({
            "station": link_id,
            "unknown15_18": payload[15:19].hex(" "),
            "cn": payload[B6_CN],
            "card": int.from_bytes(payload[B6_CARD:B6_CARD + 3], "big"),
            "byte23": f"0x{payload[23]:02X}",
            "time": si_time(seconds, payload[B6_TIME + 2]),
        })
        if len(payload) > B6_TIME + 3:
            fields["trailing"] = payload[B6_TIME + 3:].hex(" ")
        return Frame("PASSIVE (station)", payload, fields)

    if ptype == TYPE_ACTIVE and len(payload) >= B7_TIME + 3:
        seconds = struct.unpack(">H", payload[B7_TIME:B7_TIME + 2])[0]
        mode = payload[B7_MODE]
        fields.update({
            "card": struct.unpack(">I", payload[B7_CARD:B7_CARD + 4])[0],
            "mode": f"0x{mode:02X} {ACTIVE_MODE_NAMES.get(mode, '?')}",
            "bytes20_21": payload[20:22].hex(" "),
            "time": si_time(seconds, payload[B7_TIME + 2]),
        })
        if len(payload) > B7_TIME + 3:
            fields["trailing"] = payload[B7_TIME + 3:].hex(" ")
        note = None if fields["card"] == link_id else "card != link_id"
        return Frame("ACTIVE (SIAC)", payload, fields, note)

    return Frame("UNKNOWN/SHORT", payload, fields)


def build_ack(link_id: int, seq: int) -> bytes:
    """Build the 13-byte ACK payload for a given link ID (for reference/testing)."""
    return struct.pack(">I", link_id) + ACK_MID + bytes([seq & 0xFF]) + ACK_TAIL


_HEX_RE = re.compile(r"(?:[0-9A-Fa-f]{2}\s*)+")


def parse_hex(text: str) -> bytes:
    return bytes.fromhex(re.sub(r"[^0-9A-Fa-f]", "", text))


def iter_raw_log(lines) -> "list[bytes]":
    """Extract payloads from receiver output lines of the form '[RED] RAW 27: 73 69 ...'."""
    out = []
    for line in lines:
        m = re.search(r"RAW\s+(\d+):\s*((?:[0-9A-Fa-f]{2}\s*)+)", line)
        if m:
            out.append(parse_hex(m.group(2)))
    return out


# ---------------------------------------------------------------------------
# Self-test with synthetic frames (there is no real capture data in the repo)
# ---------------------------------------------------------------------------

def _synthetic_b6(station: int, cn: int, card: int, seconds: int, subsec: int) -> bytes:
    p = bytearray(27)
    p[0:4] = MAGIC
    p[4:8] = struct.pack(">I", station)
    p[OFF_TYPE] = TYPE_PASSIVE
    p[B6_CN] = cn
    p[B6_CARD:B6_CARD + 3] = card.to_bytes(3, "big")
    p[B6_TIME:B6_TIME + 2] = struct.pack(">H", seconds)
    p[B6_TIME + 2] = subsec
    return bytes(p)


def _synthetic_b7(card: int, mode: int, seconds: int, subsec: int) -> bytes:
    p = bytearray(25)
    p[0:4] = MAGIC
    p[4:8] = struct.pack(">I", card)
    p[OFF_TYPE] = TYPE_ACTIVE
    p[B7_CARD:B7_CARD + 4] = struct.pack(">I", card)
    p[B7_MODE] = mode
    p[B7_TIME:B7_TIME + 2] = struct.pack(">H", seconds)
    p[B7_TIME + 2] = subsec
    return bytes(p)


def selftest() -> int:
    f = decode(_synthetic_b6(42, 42, 2448200, 10 * 3600 + 50 * 60 + 16, 58))
    assert f.kind.startswith("PASSIVE"), f
    assert f.fields["station"] == 42 and f.fields["cn"] == 42, f
    assert f.fields["card"] == 2448200, f
    assert f.fields["time"] == "10:50:16.226", f

    f = decode(_synthetic_b7(8506707, 0xB2, 3661, 128))
    assert f.kind.startswith("ACTIVE"), f
    assert f.fields["card"] == 8506707 and f.fields["link_id"] == 8506707, f
    assert f.fields["mode"].startswith("0xB2 CONTROL"), f
    assert f.fields["time"] == "01:01:01.500", f
    assert f.note is None

    ack = build_ack(8506707, 7)
    assert len(ack) == ACK_LEN
    f = decode(ack)
    assert f.kind == "ACK" and f.fields["link_id"] == 8506707 and f.fields["seq"] == 7, f

    assert decode(b"hello world").kind == "NON-SRR"
    assert decode(MAGIC + bytes(11) + bytes([0xB6])).kind == "UNKNOWN/SHORT"

    print("selftest OK")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("hex", nargs="*", help="payload bytes as hex (spaces optional)")
    ap.add_argument("--raw-log", type=argparse.FileType("r"), help="receiver serial log with RAW lines")
    ap.add_argument("--ack", type=int, metavar="LINK_ID", help="print the ACK payload for LINK_ID and exit")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if args.ack is not None:
        print(build_ack(args.ack, 1).hex(" ").upper())
        return 0

    payloads = []
    if args.hex:
        payloads.append(parse_hex(" ".join(args.hex)))
    if args.raw_log:
        payloads += iter_raw_log(args.raw_log)
    if not payloads:
        ap.print_help()
        return 1

    for p in payloads:
        print(f"{p.hex(' ').upper()}\n  -> {decode(p)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
