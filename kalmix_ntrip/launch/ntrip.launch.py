from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
import os


def generate_launch_description():
    default_params = os.path.join(
        get_package_share_directory("kalmix_ntrip"), "config", "ntrip.example.yaml"
    )
    return LaunchDescription([
        DeclareLaunchArgument("params_file", default_value=default_params),
        DeclareLaunchArgument("log_level", default_value="info"),
        Node(
            package="kalmix_ntrip",
            executable="kalmix_ntrip_client",
            name="kalmix_ntrip_client",
            output="screen",
            parameters=[LaunchConfiguration("params_file")],
            arguments=["--ros-args", "--log-level", LaunchConfiguration("log_level")],
        ),
    ])
