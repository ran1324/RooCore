# First cleaning recording

Connect the module using the [wiring guide](wiring-he.md): one 3.3V buck converter, a 2N3906 transistor, normal TX and GPIO2 for BRC.
Our server records sensor data while the Roomba runs its original cleaning program.

## Setup

On the computer holding the project files, run:

```sh
python3 tools/configure.py --ssid YOUR_WIFI --server 192.168.1.10
```

Replace the network name and address with your Wi-Fi name and server address. The tool prompts for the Wi-Fi password and saves it locally.
Reserve the server's address in the router. The module and server need access to the same network.
Keep `network_config.h` and `.roocore-token` private; do not upload them to GitHub.

Build and flash the ESP while it is disconnected from the Roomba, following the [build instructions](../README.md#configure-and-build).
This does not flash the Roomba's firmware. Remove the programmer before connecting the ESP to the robot circuit.

## Recording

1. Start the server:

   ```sh
   python3 server/recorder.py --token-file .roocore-token
   ```

2. Power on the added circuit and wake the Roomba. The module connects to Wi-Fi and the server automatically.
3. Check that the sample count increases. Start with a 30-second test using `--profile basic --seconds 30` if needed.
4. For a full run, start the server without a time limit and press CLEAN on the Roomba.
5. Leave the server running throughout cleaning. Note or film wall contacts, turns and stalls so you can compare them with the data.
6. Once cleaning finishes, press Ctrl-C to stop recording. The added circuit remains powered.

Stopping the recording does not stop cleaning. The server does not automatically detect the end of a cleaning cycle.
After a disconnect, recording resumes in the same run folder. The missing interval is marked as lost data; the module has no local storage to recover it later. BRC pulses continue while the server is disconnected.

## Files and analysis

Each run folder contains `sensors.csv`, `transport.jsonl`, `events.jsonl` and `session.json`. Keep them together.
For an initial summary, run:

```sh
python3 tools/analyze.py recordings/RUN_FOLDER/sensors.csv
```

The report summarizes bumper contacts, samples with active cliff sensors and raw wheel counts. Those counts are not calibrated distances or a room map.
Next, compare the recording with the behavior you observed, then use it to develop our cleaning logic.
The [LiDAR and mapping plan](lidar-plan-he.md) covers the later mapping work.
