"""Actual ROS 2 bridge; no simulated observations or actuator success ACKs."""

import json
import math
import os
import time
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import LaserScan, BatteryState
from nav_msgs.msg import Odometry
from geometry_msgs.msg import Twist, TransformStamped
from std_msgs.msg import String
from std_srvs.srv import Trigger
from tf2_ros import TransformBroadcaster, StaticTransformBroadcaster
from .client import RobotClient, global_frame_valid


class Bridge(Node):
    def __init__(self):
        super().__init__("roocore_bridge")
        self.robot = RobotClient(os.environ["ROOCORE_URL"], os.environ["ROOCORE_TOKEN"])
        self.scan = self.create_publisher(LaserScan, "scan", 10)
        self.odom = self.create_publisher(Odometry, "odom", 10)
        self.battery = self.create_publisher(BatteryState, "battery", 10)
        self.status = self.create_publisher(String, "roocore/status", 10)
        self.tf = TransformBroadcaster(self)
        self.static_tf = StaticTransformBroadcaster(self)
        fixed = TransformStamped()
        fixed.header.frame_id = "base_link"
        fixed.child_frame_id = "laser"
        fixed.transform.rotation.w = 1.0
        self.static_tf.sendTransform(fixed)
        self.create_subscription(Twist, "cmd_vel", self.velocity, 10)
        self.create_service(Trigger, "roocore/remote", self.enable)
        self.create_service(Trigger, "roocore/local", self.disable)
        self.last_scan = None
        self.last_cmd = 0.0
        self.command = (0.0, 0.0)
        self.remote_requested = False
        self.timer = self.create_timer(0.05, self.poll)

    def enable(self, request, response):
        try:
            status = self.robot.poll()
            if not global_frame_valid(status) or status["state"] != 1:
                raise ValueError("start a healthy local cleaning mission first")
            self.command = (0.0, 0.0)
            self.robot.action("remote")
            self.last_cmd = time.monotonic()
            self.remote_requested = True
            response.success = True
            response.message = "requested; MCU status confirms ownership"
        except Exception as exc:
            response.success = False
            response.message = str(exc)
        return response

    def disable(self, request, response):
        self.remote_requested = False
        self.command = (0.0, 0.0)
        try:
            self.robot.action("local")
            response.success = True
        except Exception:
            response.success = False
        return response

    def velocity(self, msg):
        # Only a new explicit enable can resume remote control after a disconnect.
        if not self.remote_requested:
            return
        self.command = (msg.linear.x, msg.angular.z)
        self.last_cmd = time.monotonic()

    def poll(self):
        try:
            s = self.robot.poll()
            stamp = self.get_clock().now().to_msg()
            status = String()
            status.data = json.dumps(s)
            self.status.publish(status)
            global_valid = global_frame_valid(s)
            if not global_valid:
                self.remote_requested = False
                self.command = (0.0, 0.0)
            if global_valid:
                odom = Odometry()
                odom.header.stamp = stamp
                odom.header.frame_id = "odom"
                odom.child_frame_id = "base_link"
                odom.pose.pose.position.x = s["x"]
                odom.pose.pose.position.y = s["y"]
                odom.pose.pose.orientation.z = math.sin(s["yaw"] / 2)
                odom.pose.pose.orientation.w = math.cos(s["yaw"] / 2)
                for index in (0, 7, 35):
                    odom.pose.covariance[index] = 0.05
                transform = TransformStamped()
                transform.header = odom.header
                transform.child_frame_id = "base_link"
                transform.transform.translation.x = s["x"]
                transform.transform.translation.y = s["y"]
                transform.transform.rotation = odom.pose.pose.orientation
                self.tf.sendTransform(transform)
                self.odom.publish(odom)
            if global_valid and s["scan_valid"] and s["scan_ms"] != self.last_scan:
                self.last_scan = s["scan_ms"]
                scan = LaserScan()
                scan.header.stamp = stamp
                scan.header.frame_id = "laser"
                scan.angle_min = 0.0
                scan.angle_max = 179 * math.pi / 90
                scan.angle_increment = math.pi / 90
                scan.range_min = 0.05
                scan.range_max = 12.0
                scan.scan_time = 0.1
                scan.ranges = [
                    float(v) if 0.05 <= v <= 12 else float("nan") for v in s["ranges"]
                ]
                self.scan.publish(scan)
            b = BatteryState()
            b.header.stamp = stamp
            b.percentage = s["battery"] / 100
            self.battery.publish(b)
            if self.remote_requested and s["owner"] == 1:
                age = time.monotonic() - self.last_cmd
                if age > 2:
                    self.robot.action("local")
                    self.remote_requested = False
                else:
                    # Planning silence is a stop, never repetition of an old velocity.
                    v, w = self.command if age < 0.2 else (0.0, 0.0)
                    if s.get("passes", 1) > 1 and abs(v) > 0.12:
                        factor = 0.12 / abs(v)
                        v *= factor
                        w *= factor
                    self.robot.velocity(v, w, 127 if s.get("passes", 1) > 1 else 85)
            elif s["owner"] in (2, 3):
                self.remote_requested = False
        except Exception as exc:
            # Do not retry a motion command or re-take control. MCU lease expires.
            self.remote_requested = False
            self.command = (0.0, 0.0)
            self.get_logger().warning(
                "Transport/control stopped: " + type(exc).__name__,
                throttle_duration_sec=5.0,
            )


def main():
    rclpy.init()
    bridge = Bridge()
    try:
        rclpy.spin(bridge)
    finally:
        try:
            bridge.robot.action("local")
        except Exception:
            pass
        bridge.destroy_node()
        rclpy.shutdown()
