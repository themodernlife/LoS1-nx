#!/usr/bin/env python3
"""Summarize LOS_FRAME_TRACE CPU timings; intervals are not display/GPU timings."""
import argparse
import collections
import csv
import json
import math


def stats(values):
    values = sorted(values)
    if not values:
        return {"count": 0}
    def percentile(p):
        return round(values[max(0, math.ceil(len(values) * p) - 1)] / 1000, 3)
    return {"count": len(values), "mean_ms": round(sum(values) / len(values) / 1000, 3),
            "p50_ms": percentile(.5), "p95_ms": percentile(.95), "p99_ms": percentile(.99),
            "max_ms": percentile(1), "over_33ms": sum(v > 33333 for v in values),
            "over_50ms": sum(v > 50000 for v in values)}


def summarize(path, start=0, end=float("inf"), top=10):
    if not math.isfinite(start) or start < 0 or math.isnan(end) or end <= start or top < 0:
        raise ValueError("require 0 <= start < end and top >= 0")
    with open(path) as source:
        rows = list(csv.DictReader(source))
    if not rows:
        raise ValueError("trace contains no events")
    for row in rows:
        row["start_us"] = int(row["start_us"])
        row["duration_us"] = int(row["duration_us"])
        row["end_us"] = row["start_us"] + row["duration_us"]
    origin = min(r["start_us"] for r in rows)
    lower, upper = origin + start * 1e6, origin + end * 1e6
    selected = [r for r in rows if lower <= r["start_us"] and r["end_us"] <= upper]
    groups = collections.defaultdict(list)
    for r in selected:
        groups[r["event"]].append(r["duration_us"])
    frames = sorted((r for r in selected if r["event"] == "frame"), key=lambda r: r["end_us"])
    guest = sorted(r["end_us"] for r in selected if r["event"] == "guest_pace")
    intervals = [(b["end_us"] - a["end_us"], a["end_us"], b["end_us"], b["thread"])
                 for a, b in zip(frames, frames[1:])]
    spikes = []
    for duration, begin, finish, thread in sorted(intervals, reverse=True)[:top]:
        # Attribution candidates only: nested spans overlap, and a slow worker
        # matters only if the rendering thread waits on it. Keep threads explicit.
        nearby = [r for r in rows if r["start_us"] < finish and r["end_us"] > begin
                  and r["event"] not in ("frame", "guest_pace") and r["duration_us"] >= 1000]
        nearby.sort(key=lambda r: r["duration_us"], reverse=True)
        spikes.append({"at_s": round((finish - origin) / 1e6, 3),
                       "interval_ms": round(duration / 1000, 3),
                       "overlapping_spans": [{"event": r["event"],
                           "duration_ms": round(r["duration_us"] / 1000, 3),
                           "render_thread": r["thread"] == thread, "detail": r["detail"]}
                           for r in nearby[:8]]})
    return {"source": path, "window_start_s": start,
            "window_end_s": end if math.isfinite(end) else round((max(r["end_us"] for r in rows) - origin) / 1e6, 3),
            "note": "CPU wall times; nested spans overlap. Frame intervals measure host present submissions, not display refreshes.",
            "frame_intervals": stats([x[0] for x in intervals]),
            "guest_swap_intervals": stats([b - a for a, b in zip(guest, guest[1:])]),
            "events": {key: stats(value) for key, value in sorted(groups.items())},
            "longest_intervals": spikes}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace")
    parser.add_argument("--start", type=float, default=0, help="seconds after first trace event")
    parser.add_argument("--end", type=float, default=float("inf"))
    parser.add_argument("--top", type=int, default=10)
    args = parser.parse_args()
    try:
        result = summarize(args.trace, args.start, args.end, args.top)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))
