# Shutdown circuit simulation

[check_shutdown.py](check_shutdown.py) checks the proposed D10/EN shutdown circuit with ngspice and a small Python model of the regulator's enable thresholds. It runs on a computer and does not connect to the robot or change its firmware.

## Run it

Install Python 3 and ngspice, with `ngspice` available on `PATH`. From the repository root:

```sh
python3 hardware/check_shutdown.py
```

Run Python without `-O` or `PYTHONOPTIMIZE`, since the checks use assertions. The script creates temporary circuit files and removes them when each simulation finishes. It requires no Python packages beyond the standard library.

The script passed all 32 DC cases and the enable-state checks with ngspice 47 on 2026-11-10. It is an optional check, separate from `tests/run.sh` and the firmware build.

## What it models

The divider uses an external 100kΩ resistor in parallel with the regulator's internal 100kΩ pull-up, plus 4.7kΩ and 470Ω to ground. The Python model uses the 1.2V disable and 1.35V enable thresholds published in the [Pololu D36V28F5 Connections section](https://www.pololu.com/product/3782). These are nominal calculation inputs, not measured thresholds for the installed board.

The DC sweep uses a generic NPN model with beta values of 40 and 300, temperatures of 0°C and 70°C, input voltages of 12.0V, 13.2V, 15.0V and 20.0V, and GPIO levels of 0V and 3.0V. The circuit includes a 10kΩ base resistor and a 100kΩ base-to-emitter resistor. These are model assumptions; the generic transistor does not establish the purchased transistor's pinout or operating limits.

The state checks cover low-voltage shutdown, restart and a limitation of EN control: in the model, switching off at a high battery voltage allows an immediate restart once the GPIO releases EN. The GPIO source in the DC sweep is held at a fixed voltage. The script does not simulate how an unpowered GPIO behaves.

The final sensitivity calculation assumes ±1% for external resistors and an illustrative ±5% for the internal pull-up. Pololu does not give that internal tolerance on the product page. The result excludes EN threshold tolerance and is not a guaranteed range.

## How to use the result

`PASS` means the model met its assertions. It does not approve wiring or provide battery calibration. Startup, real battery rebound, GPIO power decay, noise, wiring and leakage still need physical checks.

See the shutdown documentation in [English](../docs/POWER_SHUTDOWN.md) or [Hebrew](../docs/POWER_SHUTDOWN-he.md), and the [electrical review](../docs/assembly/en/ELECTRICAL_REVIEW.md), before using these calculations in the assembly.
