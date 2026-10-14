"""Export vehicle test identifiers from installed episode definitions, without assets."""
import argparse
import csv
from collections import Counter
from pathlib import Path


def model_hash(name):
    value = 0
    for byte in name.lower().encode("ascii"):
        value = (value + byte) & 0xFFFFFFFF
        value = (value + (value << 10)) & 0xFFFFFFFF
        value ^= value >> 6
    value = (value + (value << 3)) & 0xFFFFFFFF
    value ^= value >> 11
    return (value + (value << 15)) & 0xFFFFFFFF


def read_vehicles(path):
    entries = {}
    in_cars = False
    for line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        line = line.partition("#")[0].strip()
        if line.lower() == "cars":
            in_cars = True
            continue
        if line.lower() == "end":
            in_cars = False
        if not in_cars or not line or "," not in line:
            continue
        fields = [part.strip() for part in line.split(",")]
        if len(fields) < 5:
            raise ValueError(f"Malformed vehicle definition: {path}: {line}")
        name, vehicle_type, label = fields[0].lower(), fields[2].lower(), fields[4]
        entries[name] = dict(model=name, hash=f"{model_hash(name):08X}", type=vehicle_type, display_key=label)
    return entries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("docs/vehicle-catalog.csv"))
    args = parser.parse_args()
    rows = []
    base = read_vehicles(args.game_dir / "common/data/vehicles.ide")
    for episode, directory in [("GTAIV", ""), ("TLAD", "TLAD"), ("TBoGT", "TBoGT")]:
        vehicles = dict(base)
        if directory:
            path = args.game_dir / directory / "common/data/vehicles.ide"
            if not path.is_file():
                continue
            vehicles.update(read_vehicles(path))
        for value in sorted(vehicles.values(), key=lambda row: (row["type"], row["model"])):
            rows.append(dict(episode=episode, **value))
        print(episode, len(vehicles), dict(sorted(Counter(v["type"] for v in vehicles.values()).items())))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=["episode", "model", "hash", "type", "display_key"])
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
