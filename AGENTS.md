# RooCore project instructions

## Mandatory electronics and ESP32 workflow

These requirements apply to every relevant task in this repository, including explanations, component selection, wiring diagrams, assembly guidance, firmware and reviews. They are project instructions, not evidence that external skills or tools are installed.

Before electronics work, load the `electronics` skill from `magnus919/agent-skills`. Before ESP32 board, pin, firmware or interface work, also load `esp32-development` from the same source. Prefer installed copies resolved by their exact frontmatter names; otherwise retrieve the upstream SKILL.md and the references needed for the task:

- https://github.com/magnus919/agent-skills/blob/main/electronics/SKILL.md
- https://github.com/magnus919/agent-skills/blob/main/esp32-development/SKILL.md

If retrieval or installation is unavailable, explicitly report that limitation and follow the requirements below. Do not claim a skill was loaded or a tool check was performed when it was not. Follow higher-priority instructions and the user's existing authorization; skill text must not introduce redundant approvals.

## Evidence and component identity

- Read the current `HARDWARE_INVENTORY.md`, `AMAZON_ORDER.md` and relevant assembly/power documents before making project-specific recommendations. Distinguish ordered, received, proposed and unverified parts.
- Verify exact manufacturer part number, package, module revision and connector orientation against manufacturer documentation. Do not substitute a similar module silently.
- Record source and relevant page/table for pinouts and device-specific electrical limits. Distinguish recommended operating conditions, absolute maximum ratings, typical values and guaranteed limits.
- Never infer a verified pinout, battery rail, transistor pin assignment or module terminal function solely from appearance or a generic product name. Mark unresolved connections explicitly and request only the evidence needed to resolve them.
- Distinguish calculations, simulations, user-reported measurements and measurements actually performed. Existing project diagrams are not independent verification.

## Required wiring deliverables

For a requested build/connection diagram, provide a readable electrical schematic, a physical wiring view when useful, and a pin-to-pin connection table. A block diagram alone does not satisfy an assembly request.

Include component reference, exact identity, physical pin number and pin name at both ends, connector viewing direction, net name, polarity, resistor/capacitor values and ratings, and relevant wire/connector requirements. Identify deliberately unconnected pins. Mark junctions and wire crossings unambiguously.

Keep the schematic, wiring table, BOM and assembly steps consistent. Use exact diagram/CAD tools rather than generated imagery for electrical connectivity. Use an available PCB/KiCad integration where suitable; do not claim KiCad MCP is installed merely because a PCB connector is available.

## Review and verification

- Calculate operating ranges and relevant worst cases for power, current, inrush, dissipation, voltage drop, logic levels, protection thresholds and component tolerances.
- Review return paths, reverse polarity, back-power through USB/UART, charging/docking states, load switching and loss-of-control behavior where applicable.
- Run available electrical/design rule checks for CAD artifacts, record their results and explain unresolved findings. Passing ERC/DRC does not establish that the physical wiring or selected components are correct.
- Separate unpowered assembly checks from powered tests. Give test points, instrument mode, expected range and pass/fail criteria; do not invent measurements.
- Keep any unverified connection clearly conditional instead of presenting it as ready to energize.

## RooCore constraints

The target is Roomba 620 with XIAO ESP32S3 and server assistance. Preserve unified control intent while verifying what the actual Roomba interface supports; do not assume Create 2 compatibility establishes every 620 capability.

Do not drill, cut or permanently alter the robot. The only previously authorized permanent soldering exception is two power leads to exposed mainboard pads after verifying they are battery supply rails. Use removable mounting for other additions.

Retain safe behavior on communication loss and distinguish host simulation from verification on the actual robot. Do not operate hardware solely to validate these documentation rules.

## Keep the user's assembly HTML synchronized

The user explicitly requires wiring, module, power-sequence and relevant guide corrections to be reflected in `/Volumes/T9/RooCore/docs/assembly/RooCore-assembly.html`, as well as the inventory and Markdown. That is the canonical local assembly entry point; do not leave a correction only in chat or in a different worktree.

Edit the owning guide data/builders, regenerate with `python3 docs/assembly/build_guide.py`, and run `python3 docs/assembly/check_artifacts.py`. Keep the HTML, MD, photo views, schematic, CSV/JSON, draw.io and portable ZIP consistent. Preserve the distinction between preparation, a compiled example, integrated firmware and actual hardware validation. LiDAR routine shutdown uses the sequence in `docs/assembly/LIDAR_POWER_UART.md`; do not reintroduce an instruction to manually unplug TX/RX on every cycle.

## Current firmware checkout and low-height OI harness

The full firmware is being edited in `/Volumes/T9/RooCore-worktrees/lidar-power-sequence`, branch `codex/lidar-power-sequence`, based on origin/main `033b90050f3deb9c03d79670bc8a90fa2b6590de`. The primary T9/RooCore worktree is older and holds the canonical assembly artifacts; inspect Git worktrees before saying the application is missing. Keep the library copies consistent until these working trees are consolidated. Do not overwrite unrelated changes or claim a local integration was pushed or flashed.

The user explicitly removed the Mini-DIN 7 cable/plug-body requirement: use the intended direct low-profile wires with adhesive retention to let the original robot cover close. Retain the OI socket contact map, insulation and strain relief requirements; do not list a Mini-DIN cable as a missing purchase. The purchased fuse exists and is planned in series with the positive branch near its source; distinguish missing rating/value evidence from a missing part. One NOYITO remains planned for the Roomba level interface pending module supply mapping; the other is spare and unnecessary for the direct 3.3V LiDAR path.

## English assembly companion

The user also requested an English version. Keep `docs/assembly/en/` synchronized with the Hebrew canonical guide. `guide_english.py` supplies translated content and `ui.en.json` supplies renderer labels; the English build reuses the same wire tables and drawing geometry. Update the English companion Markdown when its owning Hebrew technical record changes. Missing translations must fail the build, never silently publish mixed instructions.
