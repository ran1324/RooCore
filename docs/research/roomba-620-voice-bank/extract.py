"""Extract the documented Roomba 620 voice-bank image to a new directory."""
import argparse
import csv
import hashlib
import sys
import tempfile
import wave
from array import array
from pathlib import Path

IMAGE_SHA256 = "334b74d0fb4790f45095d53297d774947b44f72662aefdf071f7e35b95457a67"
LANGUAGES = "en fr de es it nl da sv no fi pl ru pt ja ko zh".split()
ENGLISH_LABELS = ("language", "move_and_restart", "clean_brushes", "please_charge")
STEP_SIZES = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
    41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173,
    190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
    16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
)
INDEX_DELTA = (-1, -1, -1, -1, 2, 4, 6, 8)
RATE = 8000


def decode_clip(data):
    predictor = 0
    step_index = 0
    samples = array("h")
    for byte in data:
        for code in (byte >> 4, byte & 15):
            step = STEP_SIZES[step_index]
            change = step >> 3
            if code & 4:
                change += step
            if code & 2:
                change += step >> 1
            if code & 1:
                change += step >> 2
            predictor += -change if code & 8 else change
            predictor = max(-32768, min(32767, predictor))
            samples.append(predictor)
            step_index = max(0, min(88, step_index + INDEX_DELTA[code & 7]))
    if sys.byteorder == "big":
        samples.byteswap()
    return samples.tobytes()


def extract(image, directory):
    count = int.from_bytes(image[1:3], "big")
    with (directory / "index.csv").open("w", newline="", encoding="utf-8") as index:
        rows = csv.writer(index)
        rows.writerow(("slot_zero_based", "group", "start_hex", "end_hex_exclusive",
                       "source_bytes", "type", "wav_file", "sample_frames", "export_rate_hz"))
        for slot in range(count):
            offset = 3 + 6 * slot
            start = int.from_bytes(image[offset:offset + 3], "little")
            end = int.from_bytes(image[offset + 3:offset + 6], "little")
            group = ""
            filename = ""
            kind = "four-byte record, purpose undetermined"
            if slot:
                group = "standalone" if slot == 1 else LANGUAGES[(slot - 2) // 22]
                kind = "audio" if end > start else "empty"
            if kind == "audio":
                label = ""
                if group == "en" and slot - 2 < len(ENGLISH_LABELS):
                    label = "_" + ENGLISH_LABELS[slot - 2]
                filename = f"{group}/{slot:03d}{label}.wav"
                path = directory / filename
                path.parent.mkdir(exist_ok=True)
                with wave.open(str(path), "wb") as wav:
                    wav.setparams((1, 2, RATE, 0, "NONE", ""))
                    wav.writeframes(decode_clip(image[start:end]))
            rows.writerow((slot, group, f"0x{start:06X}", f"0x{end:06X}", end - start,
                           kind, filename, 2 * (end - start) if filename else "",
                           RATE if filename else ""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("destination", type=Path, help="new output directory")
    args = parser.parse_args()
    image = args.image.read_bytes()
    if hashlib.sha256(image).hexdigest() != IMAGE_SHA256:
        parser.error("Image differs from the documented capture; its slot map is unverified.")
    if args.destination.exists():
        parser.error("Choose a new destination directory.")
    args.destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.destination.parent) as tmp:
        staged = Path(tmp) / "clips"
        staged.mkdir()
        extract(image, staged)
        staged.rename(args.destination)
    print(f"Extracted 341 WAV files and index.csv into {args.destination}")


if __name__ == "__main__":
    main()
