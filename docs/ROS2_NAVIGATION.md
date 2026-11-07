# Optional Nav2 computer

Use an existing Linux computer with **Ubuntu 24.04 / ROS 2 Jazzy**. Nothing here requires buying another controller. XIAO safety, local coverage and link-loss fallback remain on the robot. This integration has not been exercised against physical hardware.

```sh
sudo apt install ros-jazzy-navigation2 ros-jazzy-nav2-bringup ros-jazzy-slam-toolbox python3-colcon-common-extensions
source /opt/ros/jazzy/setup.bash
colcon build --base-paths ros2 --packages-select roocore_bridge
source install/setup.bash
export ROOCORE_URL=http://ROBOT_IP
read -s ROOCORE_TOKEN
export ROOCORE_TOKEN
ros2 launch roocore_bridge bridge.launch.py
```

The token is local configuration; do not put it in commits, launch files, screenshots or command-line URLs. The bridge uses standard-library HTTP with a 150 ms request timeout. Its topics are `/scan` (180 body-centered rays), `/odom`, `/battery`, `/roocore/status`; TF is `odom -> base_link -> laser`. The firmware compensates the measured LiDAR mounting offset before sending ranges. Unknown range bins are NaN, not invented free space. Timestamps currently use host receipt time; network delay and scan deskew are commissioning limitations.

A conservative baseline is included in `config/nav2.yaml`, `config/slam.yaml` and `config/navigate.xml`: 18 cm radius with 5 cm padding, forward speed 0.18 m/s, no automatic reversing, 8-second progress limit and no repeating recovery tree. These are initial engineering values, **not measured acceptance values**. Confirm them during commissioning. The XIAO independently checks its own movement envelope and may reject a Nav2 command.

```sh
# Use this INSTEAD of bridge.launch.py, not alongside a second bridge.
ros2 launch roocore_bridge navigation.launch.py
```

The launch starts SLAM Toolbox, NavFn A* planning, regulated pure pursuit, the BT navigator and their lifecycle manager. It does not start a cleaning mission or acquire motor control. The coverage action client has a separate 90-second goal deadline. Physical footprint, wheel calibration, transform timing, localization and real docking remain to be validated.

After a local mission is active and Nav2 is available, `ros2 run roocore_bridge cleaner` requests remote ownership and sends the MCU's coverage goals to Nav2's `NavigateToPose` action. Goal submission, cancellation and execution have finite deadlines. Failure requests local control. The bridge converts fresh `/cmd_vel` into bounded wheel commands and includes the MCU epoch, sequence and expiry. It sends zero during brief controller silence, and requests local fallback after 2 seconds without a new velocity. Loss of the bridge itself is handled by the MCU lease.

Alternatively use `/roocore/remote` (`std_srvs/Trigger`) with your own Nav2 goal client. `/roocore/local` relinquishes server control. A reconnect does not automatically call remote again. Only one bridge process should own the token/session.

The local fallback map and SLAM Toolbox's map are distinct: goals are expressed in the MCU's `odom` frame and transformed by Nav2. The local map remains available if the server disappears. This does not transfer a saved SLAM pose graph to the XIAO or promise global relocalization after a power cycle. If the local pose fails validation, the correct fallback is stopped, not guessed motion.
