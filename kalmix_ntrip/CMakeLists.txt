cmake_minimum_required(VERSION 3.16)
project(kalmix_ntrip)

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
find_package(diagnostic_msgs REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

add_library(kalmix_rtcm3_parser src/rtcm3_parser.cpp)
target_include_directories(kalmix_rtcm3_parser PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(kalmix_rtcm3_parser PUBLIC Boost::system)

add_executable(kalmix_ntrip_client src/ntrip_client_node.cpp)
target_include_directories(kalmix_ntrip_client PRIVATE include)
# ament_target_dependencies() uses CMake's plain target_link_libraries form on
# ROS 2 Jazzy. Keep this target in that form as CMake forbids mixing forms.
target_link_libraries(kalmix_ntrip_client kalmix_rtcm3_parser Boost::system OpenSSL::SSL OpenSSL::Crypto)
ament_target_dependencies(kalmix_ntrip_client rclcpp nmea_msgs mavros_msgs diagnostic_msgs)

install(TARGETS kalmix_rtcm3_parser kalmix_ntrip_client
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
# Local deployment YAML may contain NTRIP credentials. Do not copy it into
# build/install artifacts; the repository-level launch reads it from source.
install(DIRECTORY launch config/ DESTINATION share/${PROJECT_NAME}
  PATTERN "*.local.yaml" EXCLUDE)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_rtcm3_parser test/test_rtcm3_parser.cpp)
  target_link_libraries(test_rtcm3_parser kalmix_rtcm3_parser)
endif()

ament_export_include_directories(include)
ament_export_libraries(kalmix_rtcm3_parser)
ament_package()
