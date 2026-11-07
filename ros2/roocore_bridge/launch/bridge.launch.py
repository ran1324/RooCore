from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    # Launch SLAM Toolbox + Nav2 separately with measured robot parameters.
    # This launches the real bridge and never automatically arms movement.
    return LaunchDescription([Node(package='roocore_bridge', executable='bridge', output='screen')])
