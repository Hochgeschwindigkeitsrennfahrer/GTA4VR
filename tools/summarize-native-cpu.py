"""Map raw sampled instruction addresses through xperf image rundown.

Use matching reports from one ETL; stripped symbols are not module identity.
Ranges use relocated runtime addresses and are independent of module totals.
"""
import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path


def summarize(ip_path, image_path, pid, ranges):
    lines = ip_path.read_text(encoding="utf-8-sig").splitlines()
    header = re.fullmatch(
        r"thread=(\d+) seconds=(\d+) samples=(\d+) uniqueIPs=(\d+) lostEvents=(\d+)",
        lines[0],
    )
    if not header:
        raise ValueError("Unrecognized sampled-IP report header")
    thread, seconds, expected, unique, lost = map(int, header.groups())
    if expected <= 0 or unique <= 0:
        raise ValueError("Report contains no samples")
    if lost:
        raise ValueError(f"Trace has {lost} lost events; exclude it from comparisons")
    samples = {}
    for line in lines[1:]:
        match = re.fullmatch(r"ip=([0-9A-Fa-f]+) hits=(\d+)", line)
        if match:
            address, hits = int(match[1], 16), int(match[2])
            if address in samples or hits <= 0:
                raise ValueError("Duplicate address or invalid hit count")
            samples[address] = hits
        elif not line.startswith("provider="):
            raise ValueError("Unrecognized sampled-IP report row")
    if sum(samples.values()) != expected or len(samples) != unique:
        raise ValueError("Sample totals do not match the report header")

    target_images, kernel_images = set(), set()
    with image_path.open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.reader(stream, skipinitialspace=True):
            if len(row) < 12 or row[2].strip() != "Image":
                continue
            owner = re.search(r"\(\s*(\d+)\)\s*$", row[4])
            if not owner:
                continue
            owner_pid = int(owner[1])
            if owner_pid not in (pid, 0):
                continue
            base, end = int(row[5], 16), int(row[6], 16)
            if end <= base:
                raise ValueError("Invalid image extent")
            name = row[10].replace("\\", "/").rsplit("/", 1)[-1]
            image = (base, end, name)
            if owner_pid == pid:
                target_images.add(image)
            elif base >= 0xFFFF000000000000:
                kernel_images.add(image)
    if not target_images:
        raise ValueError("No image rundown for the requested process")

    counts = Counter()
    for address, hits in samples.items():
        matches = {name for base, end, name in target_images | kernel_images
                   if base <= address < end}
        if len(matches) > 1:
            raise ValueError(f"Ambiguous image ownership at {address:#x}")
        counts[next(iter(matches), "[unmapped]")] += hits
    module_rows = [{"module": name, "samples": hits,
                    "percent": round(100 * hits / expected, 4)}
                   for name, hits in counts.most_common()]
    range_rows = []
    for label, begin_text, end_text in ranges:
        begin, end = int(begin_text, 0), int(end_text, 0)
        if end <= begin:
            raise ValueError("Range end must exceed its start")
        hits = sum(count for address, count in samples.items() if begin <= address < end)
        range_rows.append({"label": label, "runtimeBegin": hex(begin),
                           "runtimeEndExclusive": hex(end), "samples": hits,
                           "percent": round(100 * hits / expected, 4)})
    return {"pid": pid, "thread": thread, "seconds": seconds,
            "rawSamples": expected, "uniqueIPs": unique, "lostEvents": lost,
            "denominator": "raw sampled instruction events, not stack-backed hits or frame time",
            "modules": module_rows, "ranges": range_rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ips", type=Path)
    parser.add_argument("images", type=Path)
    parser.add_argument("pid", type=int)
    parser.add_argument("--range", nargs=3, action="append", default=[],
                        metavar=("LABEL", "RUNTIME_BEGIN", "RUNTIME_END_EXCLUSIVE"))
    args = parser.parse_args()
    try:
        result = summarize(args.ips, args.images, args.pid, args.range)
    except (ValueError, OSError, IndexError) as error:
        parser.exit(1, f"Invalid report: {error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
