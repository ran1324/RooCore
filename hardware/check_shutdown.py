#!/usr/bin/env python3
"""DC transistor checks + documented EN hysteresis model; NOT a hardware test.

Requires ngspice on PATH. The regulator state machine uses nominal thresholds
from https://www.pololu.com/product/3782 (Connections). The transistor is generic;
no manufacturer transient model or measured battery behavior is included.
"""
import itertools
import pathlib
import shutil
import subprocess
import tempfile

if not __debug__:
    raise SystemExit('Run without -O or PYTHONOPTIMIZE; this check uses assertions.')

SPICE = shutil.which('ngspice')
if SPICE is None:
    raise SystemExit('ngspice is required. Install it and add it to PATH, then rerun.')
UPPER = 1 / (1 / 100000 + 1 / 100000)
LOWER = 4700 + 470
SCALE = 1 + UPPER / LOWER
OFF, ON = 1.2 * SCALE, 1.35 * SCALE


def dc(vin, gpio, beta, temp):
    with tempfile.TemporaryDirectory() as tmp:
        p = pathlib.Path(tmp)
        (p / 'check.cir').write_text(f'''Shutdown DC check - generic NPN
Vbat vin 0 {vin}
Vgpio gpio 0 {gpio}
Rinternal vin en 100k
Rtop vin en 100k
Rbottom1 en mid 4.7k
Rbottom2 mid 0 470
Rbase gpio b 10k
Rbe b 0 100k
Qoff en b 0 generic
.model generic NPN(IS=1e-14 BF={beta} VAF=100)
.temp {temp}
.control
op
wrdata result.txt v(en) v(b)
quit
.endc
.end
''')
        subprocess.run([SPICE, '-b', 'check.cir'], cwd=p, check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        values = [float(x) for x in (p / 'result.txt').read_text().split()]
        return values[1], values[3]


count = 0
for vin, gpio, beta, temp in itertools.product(
        (12.0, 13.2, 15.0, 20.0), (0, 3.0), (40, 300), (0, 70)):
    en, base = dc(vin, gpio, beta, temp)
    if gpio:
        assert en < 0.3, (vin, gpio, beta, temp, en)
    else:
        assert abs(en - vin / SCALE) < 0.03, (vin, beta, temp, en)
    count += 1


def state(previous, battery, off_command=False):
    en = 0 if off_command else battery / SCALE
    if en < 1.2:
        return False
    if en > 1.35:
        return True
    return previous


# Software shutdown at low voltage; GPIO then loses power and releases EN.
running = state(False, 15)
assert running
running = state(running, 13.2)
assert running
running = state(running, 13.2, True)
assert not running
running = state(running, 13.2, False)
assert not running
assert not state(running, 14.0)
assert state(running, 14.6)
# Independent undervoltage shutdown without firmware.
assert not state(True, 12.5)
assert not state(False, 13.5)
# Explicit limitation: shutdown at high voltage restarts once GPIO releases.
assert not state(True, 15.0, True)
assert state(False, 15.0, False)

# EXAMPLE sensitivity only: external resistors ±1%, internal pullup ±5%.
# Pololu does not specify that internal tolerance here; this is not a bound.
corners = []
for itol, etol, btol in itertools.product((.95, 1.05), (.99, 1.01), (.99, 1.01)):
    upper = 1 / (1 / (100000 * itol) + 1 / (100000 * etol))
    corners.append(1 + upper / (LOWER * btol))
print(f'PASS: {count} generic-NPN DC cases; EN hysteresis sequence checks')
print(f'Nominal model OFF {OFF:.4f} V; restart {ON:.4f} V')
print('Illustrative resistor sensitivity, excluding EN threshold tolerance:')
print(f'OFF {1.2 * min(corners):.3f}..{1.2 * max(corners):.3f} V')
print(f'ON  {1.35 * min(corners):.3f}..{1.35 * max(corners):.3f} V')
print('Not tested: real battery rebound, startup, GPIO decay, noise, wiring, leakage.')
