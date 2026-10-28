#!/usr/bin/env python3
"""Decode GUI5560 files and perform exact, disk-sorted two-channel coincidences.

Only the Python standard library is required. Input files are never modified.
The coincidence time convention is dt = (B timestamp * 8 + offset_ns) - A * 8.
Every pair satisfying abs(dt) <= window_ns is exported (not nearest-only).
"""
import argparse
from contextlib import closing
import csv
import json
from pathlib import Path
import sqlite3
import struct
import sys
import tempfile

WORDS = struct.Struct("<8I")
FIELDS = ("timestamp_ticks", "previous_ticks", "channel", "baseline_a", "baseline_b", "sl1", "sg", "sl2")


def header(stream):
    if stream.read(8) != b"R5560D1\n":
        raise ValueError("Not a GUI5560 list-mode file")
    size_bytes = stream.read(4)
    if len(size_bytes) != 4:
        raise ValueError("Truncated file header")
    size = struct.unpack("<I", size_bytes)[0]
    if not 0 < size <= 1024 * 1024:
        raise ValueError("Invalid header length")
    data = stream.read(size)
    if len(data) != size:
        raise ValueError("Truncated JSON header")
    meta = json.loads(data)
    if (meta.get("schema") != "r5560-list-v1" or meta.get("event_bytes") != 32
            or meta.get("byte_order") != "little" or meta.get("event_format") != 2
            or meta.get("clock_hz") != 125000000):
        raise ValueError("Unsupported event format / clock / endianness")
    return meta


def events(stream):
    while True:
        block = stream.read(32 * 8192)
        if not block:
            return
        if len(block) % 32:
            raise ValueError("Truncated final event; do not treat this run as complete")
        for w in WORDS.iter_unpack(block):
            if w[2] & 0x07000000 or (w[3] | w[4] | w[5] | w[6]) & 0xff000000:
                raise ValueError("Event reserved bits are nonzero; alignment/firmware mismatch")
            yield ((w[1] & 0xFFFF) << 32 | w[0], (w[1] >> 16) << 32 | w[7],
                   w[2] >> 27, w[2] & 0xFFFFFF, w[3], w[4], w[5], w[6])


def export_csv(source, destination):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if source == destination:
        raise ValueError("Output must differ from input")
    # Exclusive creation protects prior analysis files too.
    with source.open("rb") as src, destination.open("x", newline="", encoding="utf-8") as dst:
        meta = header(src)
        writer = csv.writer(dst)
        writer.writerow(("daq_id", "run_id") + FIELDS)
        count = 0
        for event in events(src):
            writer.writerow((meta["daq_id"], meta["run_id"]) + event)
            count += 1
    return count


def require_complete(source):
    manifest_path = Path(source).resolve().parent / "run.json"
    with manifest_path.open(encoding="utf-8") as f:
        manifest = json.load(f)
    if manifest.get("status") != "complete":
        raise ValueError("Coincidence input must belong to a complete run.json")
    return manifest


def coincidences(source_a, source_b, ch_a, ch_b, window_ns, offset_ns, destination):
    if not 0 <= ch_a < 32 or not 0 <= ch_b < 32 or window_ns < 0:
        raise ValueError("Invalid channel / coincidence window")
    ma, mb = require_complete(source_a), require_complete(source_b)
    if ma.get("run_id") != mb.get("run_id") or ma.get("synchronization") != "shared-external-RUN" or mb.get("synchronization") != "shared-external-RUN":
        raise ValueError("Inputs must share an external-RUN epoch")
    if Path(source_a).resolve().parent != Path(source_b).resolve().parent:
        raise ValueError("Select both module files from the same run directory")
    if Path(destination).resolve() in (Path(source_a).resolve(), Path(source_b).resolve()):
        raise ValueError("Output must differ from input")
    # Sorting on disk handles arbitrary inter-channel / inter-module readout order.
    # A detected 48-bit rollover is refused rather than silently inventing an epoch.
    with tempfile.TemporaryDirectory(prefix="r5560-coinc-") as temp:
        with closing(sqlite3.connect(str(Path(temp) / "events.db"))) as db:
            db.execute("PRAGMA journal_mode=OFF")
            db.execute("PRAGMA temp_store=FILE")
            headers = []
            for name, source, channel in (("a", source_a, ch_a), ("b", source_b, ch_b)):
                db.execute(f"CREATE TABLE {name} (seq INTEGER PRIMARY KEY, ts INTEGER)")
                with open(source, "rb") as src:
                    meta = header(src)
                    if meta["run_id"] != ma["run_id"] or not meta["settings"]["sync_config"] & 1:
                        raise ValueError("File metadata disagrees with synchronized run")
                    headers.append(meta)
                    minimum, maximum = (1 << 48), 0
                    batch = []
                    for seq, e in enumerate(events(src)):
                        minimum, maximum = min(minimum, e[0]), max(maximum, e[0])
                        if maximum - minimum >= 1 << 47:
                            raise ValueError("Ambiguous 48-bit timestamp rollover / run longer than half-wrap; split acquisition into shorter runs")
                        if e[2] == channel:
                            batch.append((seq, e[0]))
                        if len(batch) >= 8192:
                            db.executemany(f"INSERT INTO {name} VALUES (?,?)", batch)
                            batch.clear()
                    db.executemany(f"INSERT INTO {name} VALUES (?,?)", batch)
                db.execute(f"CREATE INDEX {name}_time ON {name}(ts)")
            if headers[0]["daq_id"] == headers[1]["daq_id"]:
                raise ValueError("Select two distinct DAQ IDs for cross-module coincidences")
            if headers[0].get("simulated") != headers[1].get("simulated"):
                raise ValueError("Cannot mix simulated and hardware events")
            # UTC/run-directory equality is bookkeeping, not proof of physical sync.
            count = 0
            with open(destination, "x", newline="", encoding="utf-8") as dst:
                writer = csv.writer(dst)
                writer.writerow(("daq_a", "ch_a", "event_a", "ticks_a", "daq_b", "ch_b", "event_b", "ticks_b", "delta_ns"))
                for seq_a, ts_a in db.execute("SELECT seq,ts FROM a ORDER BY ts,seq"):
                    low = -(-(ts_a * 8 - offset_ns - window_ns) // 8)
                    high = (ts_a * 8 - offset_ns + window_ns) // 8
                    for seq_b, ts_b in db.execute("SELECT seq,ts FROM b WHERE ts BETWEEN ? AND ? ORDER BY ts,seq", (low, high)):
                        writer.writerow((headers[0]["daq_id"], ch_a, seq_a, ts_a, headers[1]["daq_id"], ch_b, seq_b, ts_b, ts_b*8 + offset_ns - ts_a*8))
                        count += 1
            return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    decode = sub.add_parser("decode", help="Export raw event fields to CSV")
    decode.add_argument("source"); decode.add_argument("destination")
    coinc = sub.add_parser("coincidence", help="Exact offline all-pairs coincidence analysis")
    coinc.add_argument("source_a"); coinc.add_argument("source_b"); coinc.add_argument("destination")
    coinc.add_argument("--channel-a", type=int, default=0); coinc.add_argument("--channel-b", type=int, default=0)
    coinc.add_argument("--window-ns", type=int, default=40); coinc.add_argument("--offset-ns", type=int, default=0)
    args = parser.parse_args()
    try:
        if args.command == "decode":
            n = export_csv(args.source, args.destination)
        else:
            n = coincidences(args.source_a, args.source_b, args.channel_a, args.channel_b,
                             args.window_ns, args.offset_ns, args.destination)
        print(f"Complete: {n} rows -> {args.destination}", flush=True)
        return 0
    except (ValueError, OSError, KeyError, sqlite3.Error) as exc:
        print(f"Analysis failed: {exc}. Any newly created CSV may be partial.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
