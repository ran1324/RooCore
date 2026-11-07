"""Bounded Nav2 action client for MCU coverage goals. Never re-arms after loss."""
import json
import math
import time
import rclpy
from rclpy.action import ActionClient
from rclpy.node import Node
from nav2_msgs.action import NavigateToPose
from action_msgs.msg import GoalStatus
from std_msgs.msg import String
from std_srvs.srv import Trigger


def main():
    rclpy.init(); node = Node('roocore_cleaner'); latest = {}; received = [0.]
    def status(msg):
        latest.update(json.loads(msg.data)); received[0] = time.monotonic()
    node.create_subscription(String, '/roocore/status', status, 10)
    remote = node.create_client(Trigger, '/roocore/remote')
    local = node.create_client(Trigger, '/roocore/local')
    client = ActionClient(node, NavigateToPose, 'navigate_to_pose')
    handle = None; result = None; goal = None; pending = None; started = 0.; mission_start = time.monotonic()
    try:
        if not client.wait_for_server(timeout_sec=10): raise RuntimeError('Nav2 unavailable')
        while not (latest.get('localized') and latest.get('state') == 1):
            if time.monotonic()-mission_start>30: raise RuntimeError('no active local cleaning mission')
            rclpy.spin_once(node, timeout_sec=.1)
        if not remote.wait_for_service(timeout_sec=5): raise RuntimeError('bridge unavailable')
        response = remote.call_async(Trigger.Request()); rclpy.spin_until_future_complete(node, response, timeout_sec=3)
        if not response.done() or not response.result().success: raise RuntimeError('takeover not accepted')
        while rclpy.ok():
            rclpy.spin_once(node, timeout_sec=.05); now = time.monotonic()
            if now-mission_start>7200 or now-received[0]>.5: raise RuntimeError('mission or telemetry deadline')
            if latest.get('state') != 1 or latest.get('owner') in (2,3): break
            if pending:
                if pending.done():
                    handle = pending.result(); pending = None
                    if not handle.accepted: raise RuntimeError('Nav2 rejected goal')
                    result = handle.get_result_async()
                elif now-started>3: raise RuntimeError('goal acknowledgement timeout')
            if goal and now-started>90: raise RuntimeError('goal deadline')
            if result and result.done():
                if result.result().status != GoalStatus.STATUS_SUCCEEDED: raise RuntimeError('Nav2 goal failed')
                result = None; handle = None
            target = latest.get('target')
            if target and target != goal and not pending:
                if handle:
                    cancelled = handle.cancel_goal_async(); rclpy.spin_until_future_complete(node, cancelled, timeout_sec=2)
                    if not cancelled.done(): raise RuntimeError('cancellation timeout')
                    handle = None; result = None
                message = NavigateToPose.Goal(); pose = message.pose
                pose.header.frame_id = 'odom'; pose.header.stamp = node.get_clock().now().to_msg()
                pose.pose.position.x, pose.pose.position.y = target
                yaw = math.atan2(target[1]-latest['y'],target[0]-latest['x'])
                pose.pose.orientation.z = math.sin(yaw/2); pose.pose.orientation.w = math.cos(yaw/2)
                pending = client.send_goal_async(message); goal = target; started = now
    finally:
        if handle: handle.cancel_goal_async()
        if local.wait_for_service(timeout_sec=1):
            request = local.call_async(Trigger.Request()); rclpy.spin_until_future_complete(node, request, timeout_sec=2)
        client.destroy(); node.destroy_node(); rclpy.shutdown()
