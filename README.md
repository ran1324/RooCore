# RooCore

A hybrid local/server robot controller for the Roomba 620.

RooCore is intended to keep wheel control and safety decisions on the robot while allowing a server to provide higher-level navigation instructions. The first development target is a Python simulator using simulated Roomba 620 sensor data, without LiDAR. Hardware integration is a later step.

## Planned components

- **Local controller:** interprets server instructions, controls motion, and handles immediate sensor responses.
- **Server interface:** supplies navigation instructions with explicit expiry so stale commands are rejected.
- **Simulator:** exercises robot behavior and safety cases before connecting to hardware.

## Current status

This repository contains the project outline only. A no-LiDAR Python simulator was created in an earlier chat, but its source archive is not available in this workspace and has not been imported. The earlier simulator is therefore **not** represented here as committed or hardware-validated code.

The intended simulator scenarios include normal cleaning, obstacle and dirt responses, a communications outage, cliff detection, and stale-command rejection. A previous simulated obstacle-avoidance change was experimental and should be validated before use on a real robot.

## Safety boundary

Server instructions must never override local cliff, wheel-drop, bumper, or proximity safety responses. Motion should stop when navigation instructions expire. Do not connect this project to a physical Roomba until the controller and Open Interface integration have been implemented and tested on hardware.
