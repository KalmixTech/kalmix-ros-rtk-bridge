"""Start the KALMIX GNSS serial bridge and NTRIP client together.

This repository-level launch file is intentionally not part of either ROS 2
package. Run it by its absolute path so it can use the editable YAML files
stored alongside the two packages in this source checkout.
"""

from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    repository_root = Path(__file__).resolve().parent.parent
    serial_config_dir = repository_root / "kalmix_gnss_serial" / "config"
    ntrip_config_dir = repository_root / "kalmix_ntrip" / "config"
    # *.local.yaml is the only runtime configuration. It is ignored by Git and
    # must be created/configured before launch; examples are reference only.
    serial_local = serial_config_dir / "serial.local.yaml"
    ntrip_local = ntrip_config_dir / "ntrip.local.yaml"
    missing_configs = [str(path) for path in (serial_local, ntrip_local) if not path.is_file()]
    if missing_configs:
        raise FileNotFoundError(
            "KALMIX RTK requires deployment-local configuration. Copy each "
            "*.example.yaml file to the matching *.local.yaml file and fill it "
            "before launch. Missing: " + ", ".join(missing_configs))

    serial_launch = Path(get_package_share_directory("kalmix_gnss_serial")) / "launch" / "serial_bridge.launch.py"
    ntrip_launch = Path(get_package_share_directory("kalmix_ntrip")) / "launch" / "ntrip.launch.py"

    return LaunchDescription([
        DeclareLaunchArgument("serial_log_level", default_value="info"),
        DeclareLaunchArgument("ntrip_log_level", default_value="info"),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(str(serial_launch)),
            launch_arguments={
                "params_file": str(serial_local),
                "log_level": LaunchConfiguration("serial_log_level"),
            }.items(),
        ),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(str(ntrip_launch)),
            launch_arguments={
                "params_file": str(ntrip_local),
                "log_level": LaunchConfiguration("ntrip_log_level"),
            }.items(),
        ),
    ])
