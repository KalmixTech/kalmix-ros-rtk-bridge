cmake_minimum_required(VERSION 3.16)
project(kalmix_gnss_serial)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(nmea_msgs REQUIRED)
find_package(mavros_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)

add_executable(kalmix_gnss_serial_bridge src/serial_bridge_node.cpp)
target_include_directories(kalmix_gnss_serial_bridge PRIVATE include)
ament_target_dependencies(kalmix_gnss_serial_bridge rclcpp nmea_msgs mavros_msgs sensor_msgs)
install(TARGETS kalmix_gnss_serial_bridge RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
# Local deployment YAML may contain device-specific information. Do not copy
# it into build/install artifacts; the repository-level launch reads it from source.
install(DIRECTORY launch config/ DESTINATION share/${PROJECT_NAME}
  PATTERN "*.local.yaml" EXCLUDE)
ament_package()
