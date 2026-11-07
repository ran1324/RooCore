"""Nav2 + SLAM Toolbox baseline. Launching never arms robot motion."""
import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node


def generate_launch_description():
    share = get_package_share_directory('roocore_bridge')
    parameters = os.path.join(share, 'config', 'nav2.yaml')
    slam = get_package_share_directory('slam_toolbox')
    nodes = [Node(package='roocore_bridge', executable='bridge', output='screen')]
    nodes.append(IncludeLaunchDescription(PythonLaunchDescriptionSource(os.path.join(slam, 'launch', 'online_async_launch.py')),
        launch_arguments={'use_sim_time': 'false', 'slam_params_file': os.path.join(share, 'config', 'slam.yaml')}.items()))
    for package, executable in [('nav2_planner', 'planner_server'), ('nav2_controller', 'controller_server'), ('nav2_bt_navigator', 'bt_navigator')]:
        extras = {'use_sim_time': False}
        if executable == 'bt_navigator': extras['default_nav_to_pose_bt_xml'] = os.path.join(share, 'config', 'navigate.xml')
        nodes.append(Node(package=package, executable=executable, name=executable, output='screen', parameters=[parameters, extras]))
    nodes.append(Node(package='nav2_lifecycle_manager', executable='lifecycle_manager', name='lifecycle_manager_navigation',
        parameters=[{'autostart': True, 'node_names': ['planner_server', 'controller_server', 'bt_navigator'], 'bond_timeout': 4.0}]))
    return LaunchDescription(nodes)
