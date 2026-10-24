"""Take a nonexclusive snapshot of a growing PresentMon v1 CSV.

This is a one-shot reader: run again to refresh the report while capture runs.
The input CSV is never changed. Cadence includes menus and loading screens.
"""

from __future__ import annotations

import argparse
import csv
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import io
import json
import math
import os
from pathlib import Path
from collections import Counter


def shared_snapshot(path: Path) -> bytes:
    """Read up to the size at open time, allowing the writer to keep using it."""
    if os.name != "nt":
        with path.open("rb") as stream:
            return stream.read(os.fstat(stream.fileno()).st_size)

    import msvcrt

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateFileW.argtypes = [
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
    ]
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.GetFileSizeEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_longlong)]
    kernel32.GetFileSizeEx.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL
    # GENERIC_READ; FILE_SHARE_READ | WRITE | DELETE; OPEN_EXISTING.
    handle = kernel32.CreateFileW(str(path), 0x80000000, 7, None, 3, 0, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    size = ctypes.c_longlong()
    if not kernel32.GetFileSizeEx(handle, ctypes.byref(size)):
        error = ctypes.get_last_error()
        kernel32.CloseHandle(handle)
        raise ctypes.WinError(error)
    try:
        descriptor = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except BaseException:
        kernel32.CloseHandle(handle)
        raise
    # The file descriptor now owns the handle, including its sharing flags.
    with os.fdopen(descriptor, "rb", buffering=0) as stream:
        remaining = size.value
        parts = []
        while remaining > 0:
            part = stream.read(min(remaining, 1024 * 1024))
            if not part:
                break
            parts.append(part)
            remaining -= len(part)
        return b"".join(parts)


def percentile_nearest_rank(values: list[float], fraction: float) -> float:
    return values[max(0, math.ceil(len(values) * fraction) - 1)]


def statistics(frames: list[dict]) -> dict:
    if not frames:
        return {"sample_count": 0}
    intervals = sorted(frame["frametime_ms"] for frame in frames)
    count = len(intervals)
    mean = math.fsum(intervals) / count
    worst_count = max(1, math.ceil(count * 0.01))
    worst_mean = math.fsum(intervals[-worst_count:]) / worst_count
    return {
        "sample_count": count,
        "capture_start_seconds": frames[0]["time_seconds"],
        "capture_end_seconds": frames[-1]["time_seconds"],
        "covered_seconds": frames[-1]["time_seconds"] - frames[0]["time_seconds"],
        "mean_fps": 1000.0 / mean,
        "one_percent_low_fps": 1000.0 / worst_mean,
        "one_percent_worst_frame_count": worst_count,
        "mean_frametime_ms": mean,
        "median_frametime_ms": percentile_nearest_rank(intervals, 0.5),
        "p99_frametime_ms": percentile_nearest_rank(intervals, 0.99),
        "p99_9_frametime_ms": percentile_nearest_rank(intervals, 0.999),
        "worst_frametime_ms": intervals[-1],
        "frames_over_50_ms": sum(value > 50.0 for value in intervals),
        "frames_over_100_ms": sum(value > 100.0 for value in intervals),
        "dropped_present_count": sum(frame["dropped"] for frame in frames),
    }


def analyze(path: Path, pid: int) -> dict:
    report = {
        "source_csv": str(path.resolve()),
        "process_id": pid,
        "snapshot_utc": datetime.now(timezone.utc).isoformat(),
        "status": "waiting",
        "definitions": {
            "cadence": "PresentMon v1 msBetweenPresents, not game simulation frames.",
            "mean_fps": "1000 / arithmetic mean of positive frame intervals.",
            "one_percent_low": "1000 / mean of the slowest ceil(N * 0.01) frame intervals.",
            "percentiles": "Nearest rank; milliseconds, with higher values meaning slower frames.",
            "first_interval": "First valid interval of the dominant swap chain is discarded.",
            "last_60_seconds": "Relative to the latest selected frame, not snapshot wall time.",
            "context": "Menus, loading, focus changes and capture overhead remain included.",
            "runtime": "ETW runtime label is reported as recorded; it does not identify the configured RPCS3 renderer.",
        },
    }
    try:
        raw = shared_snapshot(path)
    except FileNotFoundError:
        report["reason"] = "CSV has not been created yet."
        return report
    report["snapshot_bytes"] = len(raw)
    # PowerShell 5 redirects stdout as UTF-16; the direct collector uses UTF-8.
    encoding = "utf-16" if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
    report["input_encoding"] = encoding
    decoded = raw.decode(encoding, errors="replace")
    # A writer can be part way through its final record. Analyze complete lines.
    if decoded and not decoded.endswith("\n"):
        decoded = decoded[:decoded.rfind("\n") + 1]
    rows = csv.DictReader(io.StringIO(decoded, newline=""))
    required = {"ProcessID", "SwapChainAddress", "TimeInSeconds", "msBetweenPresents"}
    if not rows.fieldnames or not required.issubset(rows.fieldnames):
        report["reason"] = "Waiting for a complete PresentMon v1 CSV header."
        return report
    frames = []
    rejected = 0
    target_rows = 0
    for row in rows:
        try:
            if int(row["ProcessID"]) != pid:
                continue
            target_rows += 1
            time_seconds = float(row["TimeInSeconds"])
            frametime = float(row["msBetweenPresents"])
            if not (math.isfinite(time_seconds) and time_seconds >= 0
                    and math.isfinite(frametime) and frametime > 0):
                rejected += 1
                continue
            chain = row["SwapChainAddress"]
            if not chain or None in row.values():
                rejected += 1
                continue
            frames.append({
                "swap_chain": chain,
                "time_seconds": time_seconds,
                "frametime_ms": frametime,
                "runtime": row.get("Runtime", ""),
                "dropped": row.get("Dropped", "0") == "1",
            })
        except (ValueError, TypeError, KeyError):
            rejected += 1
    chain_counts = Counter(frame["swap_chain"] for frame in frames)
    report["target_row_count"] = target_rows
    report["invalid_or_nonpositive_row_count"] = rejected
    report["swap_chains"] = dict(chain_counts.most_common())
    if not chain_counts:
        report["reason"] = "No complete positive frame intervals for the requested PID yet."
        return report
    selected_chain = chain_counts.most_common(1)[0][0]
    selected = sorted((frame for frame in frames if frame["swap_chain"] == selected_chain),
                      key=lambda frame: frame["time_seconds"])
    report["dominant_swap_chain"] = selected_chain
    report["runtime_labels"] = dict(Counter(frame["runtime"] for frame in selected))
    report["dominant_share_of_valid_target_rows"] = len(selected) / len(frames)
    selected = selected[1:]
    report["discarded_first_interval_count"] = 1
    report["global"] = statistics(selected)
    recent = ([frame for frame in selected
               if frame["time_seconds"] >= selected[-1]["time_seconds"] - 60.0]
              if selected else [])
    report["last_60_seconds"] = statistics(recent)
    report["status"] = "ok" if selected else "waiting"
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--pid", required=True, type=int)
    args = parser.parse_args()
    try:
        report = analyze(args.csv, args.pid)
    except (OSError, csv.Error) as error:
        report = {"status": "error", "source_csv": str(args.csv),
                  "process_id": args.pid, "error": str(error)}
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
    temporary = args.output_json.with_suffix(args.output_json.suffix + ".tmp")
    temporary.write_text(payload, encoding="utf-8")
    temporary.replace(args.output_json)
    print(payload, end="")


if __name__ == "__main__":
    main()
