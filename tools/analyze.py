"""Summarize a sensor CSV without claiming to reconstruct the native planner."""
import argparse
import csv
import json
from collections import Counter
from oi import delta16


def summarize(rows):
    counts = Counter()
    previous = None
    for row in rows:
        counts['frames'] += 1
        bump = int(row['bumps_wheeldrops'] or 0) & 3
        if bump and (previous is None or not (int(previous['bumps_wheeldrops'] or 0)&3)):
            counts['bumper_activations_observed'] += 1
        if any(int(row[k] or 0) for k in ('cliff_left','cliff_front_left','cliff_front_right','cliff_right')):
            counts['cliff_active_frames'] += 1
        if row['encoder_left'] and row['encoder_right'] and previous and previous['encoder_left'] and previous['encoder_right']:
            dt = (int(row['device_ms'])-int(previous['device_ms'])) & 0xffffffff
            continuous = row['segment'] == previous['segment'] and 0 < dt <= 250
            if continuous:
                dl = delta16(int(row['encoder_left']),int(previous['encoder_left']))
                dr = delta16(int(row['encoder_right']),int(previous['encoder_right']))
                # Count raw travel only: calibration, slip and firmware are not verified.
                counts['left_absolute_encoder_counts'] += abs(dl)
                counts['right_absolute_encoder_counts'] += abs(dr)
                counts['stationary_intervals' if dl == dr == 0 else 'moving_intervals'] += 1
            else:
                counts['wheel_intervals_skipped'] += 1
        previous = row
    return dict(counts)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv')
    args = parser.parse_args()
    with open(args.csv, newline='') as file:
        print(json.dumps(summarize(csv.DictReader(file)), indent=2))
