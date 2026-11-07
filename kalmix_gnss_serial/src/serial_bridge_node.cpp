// Copyright 2026 KALMIX
// SPDX-License-Identifier: Apache-2.0
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <mavros_msgs/msg/rtcm.hpp>
#include <nmea_msgs/msg/sentence.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>

#include "kalmix_gnss_serial/rotating_file_logger.hpp"

namespace kalmix_gnss_serial {
using namespace std::chrono_literals;

namespace {
speed_t ToSpeed(int baud_rate) {
  switch (baud_rate) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default: throw std::invalid_argument("unsupported baud_rate");
  }
}

bool IsValidNmea(const std::string& sentence, bool require_checksum) {
  if (sentence.empty() || sentence.front() != '$') return false;
  const auto star = sentence.find('*');
  if (star == std::string::npos) return !require_checksum;
  if (star + 3 != sentence.size()) return false;
  unsigned expected = 0;
  try { expected = static_cast<unsigned>(std::stoul(sentence.substr(star + 1), nullptr, 16)); }
  catch (const std::exception&) { return false; }
  unsigned actual = 0;
  for (std::size_t i = 1; i < star; ++i) actual ^= static_cast<unsigned char>(sentence[i]);
  return actual == expected;
}

double ToDegrees(const std::string& value, const std::string& hemisphere) {
  const double raw = std::stod(value);
  const int degrees = static_cast<int>(raw / 100.0);
  const double result = static_cast<double>(degrees) + (raw - degrees * 100.0) / 60.0;
  if (hemisphere == "S" || hemisphere == "W") return -result;
  if (hemisphere == "N" || hemisphere == "E") return result;
  throw std::invalid_argument("invalid NMEA hemisphere");
}
}  // namespace

class SerialBridge final : public rclcpp::Node {
 public:
  SerialBridge() : Node("kalmix_gnss_serial_bridge") {
    declare_parameter("serial_port", "/dev/ttyUSB0");
    declare_parameter("baud_rate", 115200);
    declare_parameter("nmea_topic", "/nmea");
    declare_parameter("fix_topic", "/fix");
    declare_parameter("rtcm_topic", "/rtcm");
    declare_parameter("read_period_ms", 20);
    declare_parameter("write_timeout_ms", 1000);
    declare_parameter("reconnect_interval_ms", 5000);
    declare_parameter("require_nmea_checksum", true);
    declare_parameter("publish_nav_sat_fix", true);
    declare_parameter("enable_file_logging", true);
    declare_parameter("log_directory", "/root/kalmix-rtk-logs");
    declare_parameter("log_max_file_size_mb", 50);
    declare_parameter("log_max_files", 7);
    port_ = get_parameter("serial_port").as_string();
    baud_rate_ = static_cast<int>(get_parameter("baud_rate").as_int());
    write_timeout_ms_ = static_cast<int>(get_parameter("write_timeout_ms").as_int());
    reconnect_interval_ms_ = static_cast<int>(get_parameter("reconnect_interval_ms").as_int());
    require_nmea_checksum_ = get_parameter("require_nmea_checksum").as_bool();
    publish_nav_sat_fix_ = get_parameter("publish_nav_sat_fix").as_bool();
    const int read_period_ms = static_cast<int>(get_parameter("read_period_ms").as_int());
    if (port_.empty() || write_timeout_ms_ < 1 || reconnect_interval_ms_ < 100 || read_period_ms < 1) {
      throw std::invalid_argument("invalid serial bridge parameters");
    }
    SetupFileLogging();
    nmea_publisher_ = create_publisher<nmea_msgs::msg::Sentence>(
        get_parameter("nmea_topic").as_string(), rclcpp::QoS(30));
    if (publish_nav_sat_fix_) {
      fix_publisher_ = create_publisher<sensor_msgs::msg::NavSatFix>(
          get_parameter("fix_topic").as_string(), rclcpp::QoS(10));
    }
    rtcm_subscription_ = create_subscription<mavros_msgs::msg::RTCM>(
        get_parameter("rtcm_topic").as_string(), rclcpp::QoS(30),
        [this](const mavros_msgs::msg::RTCM::SharedPtr message) { WriteRtcm(*message); });
    read_timer_ = create_wall_timer(std::chrono::milliseconds(read_period_ms), [this] { ReadNmea(); });
    Open();
  }

  ~SerialBridge() override { Close(); }

 private:
  void SetupFileLogging() {
    if (!get_parameter("enable_file_logging").as_bool()) return;
    const std::string directory = get_parameter("log_directory").as_string();
    const int max_size_mb = static_cast<int>(get_parameter("log_max_file_size_mb").as_int());
    const int max_files = static_cast<int>(get_parameter("log_max_files").as_int());
    if (directory.empty() || max_size_mb < 1 || max_size_mb > 1024 || max_files < 1 || max_files > 100) {
      throw std::invalid_argument("invalid file logging parameters");
    }
    const auto max_bytes = static_cast<std::uintmax_t>(max_size_mb) * 1024U * 1024U;
    operation_log_ = std::make_unique<RotatingFileLogger>(
        true, directory, "kalmix_serial_operation.log", max_bytes, max_files);
    nmea_log_ = std::make_unique<RotatingFileLogger>(
        true, directory, "kalmix_nmea.log", max_bytes, max_files);
    if (!operation_log_->enabled() || !nmea_log_->enabled()) {
      RCLCPP_ERROR(get_logger(), "file logging disabled: %s %s", operation_log_->failure().c_str(),
          nmea_log_->failure().c_str());
    } else {
      operation_log_->WriteOperation("serial bridge starting; port=" + port_ +
          "; baud_rate=" + std::to_string(baud_rate_));
    }
  }

  void LogOperation(const std::string& message) {
    if (operation_log_) operation_log_->WriteOperation(message);
  }

  bool Open() {
    std::lock_guard<std::mutex> lock(serial_mutex_);
    if (fd_ >= 0) return true;
    const int candidate = open(port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (candidate < 0) { ReportOpenError(); return false; }
    termios options{};
    if (tcgetattr(candidate, &options) != 0) {
      RCLCPP_ERROR(get_logger(), "tcgetattr(%s): %s", port_.c_str(), std::strerror(errno));
      LogOperation(std::string{"tcgetattr failed: "} + std::strerror(errno));
      close(candidate);
      return false;
    }
    try {
      cfmakeraw(&options);
      const speed_t speed = ToSpeed(baud_rate_);
      cfsetispeed(&options, speed);
      cfsetospeed(&options, speed);
      options.c_cflag |= (CLOCAL | CREAD);
      options.c_cc[VMIN] = 0;
      options.c_cc[VTIME] = 0;
      if (tcsetattr(candidate, TCSANOW, &options) != 0) {
        RCLCPP_ERROR(get_logger(), "tcsetattr(%s): %s", port_.c_str(), std::strerror(errno));
        LogOperation(std::string{"tcsetattr failed: "} + std::strerror(errno));
        close(candidate);
        return false;
      }
    } catch (const std::exception& error) {
      RCLCPP_ERROR(get_logger(), "%s", error.what());
      LogOperation(std::string{"serial configuration failed: "} + error.what());
      close(candidate);
      return false;
    }
    fd_ = candidate;
    RCLCPP_INFO(get_logger(), "opened GNSS serial port %s at %d baud", port_.c_str(), baud_rate_);
    LogOperation("opened serial port " + port_ + " at " + std::to_string(baud_rate_) + " baud");
    return true;
  }

  void Close() {
    std::lock_guard<std::mutex> lock(serial_mutex_);
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
      LogOperation("closed serial port");
    }
  }

  void ReportOpenError() {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_open_error_ >= std::chrono::milliseconds(reconnect_interval_ms_)) {
      RCLCPP_WARN(get_logger(), "cannot open %s: %s", port_.c_str(), std::strerror(errno));
      LogOperation(std::string{"cannot open serial port: "} + std::strerror(errno));
      last_open_error_ = now;
    }
  }

  void ReadNmea() {
    if (!Open()) return;
    char data[1024];
    ssize_t count = 0;
    {
      std::lock_guard<std::mutex> lock(serial_mutex_);
      count = read(fd_, data, sizeof(data));
      if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        RCLCPP_WARN(get_logger(), "serial read error: %s", std::strerror(errno));
        LogOperation(std::string{"serial read error: "} + std::strerror(errno));
        close(fd_); fd_ = -1;
        return;
      }
    }
    if (count <= 0) return;
    input_.append(data, static_cast<std::size_t>(count));
    if (input_.size() > 8192U) input_.erase(0, input_.size() - 2048U);
    while (true) {
      const auto newline = input_.find('\n');
      if (newline == std::string::npos) break;
      std::string sentence = input_.substr(0, newline);
      input_.erase(0, newline + 1);
      if (!sentence.empty() && sentence.back() == '\r') sentence.pop_back();
      if (!IsValidNmea(sentence, require_nmea_checksum_)) continue;
      if (nmea_log_) nmea_log_->WriteLine(sentence);
      nmea_msgs::msg::Sentence message;
      message.header.stamp = now();
      message.header.frame_id = "gnss";
      message.sentence = sentence;
      nmea_publisher_->publish(message);
      PublishFix(sentence);
    }
  }

  void PublishFix(const std::string& sentence) {
    if (!fix_publisher_ ||
        (sentence.rfind("$GPGGA,", 0) != 0 && sentence.rfind("$GNGGA,", 0) != 0)) return;
    std::vector<std::string> fields;
    std::istringstream input{sentence};
    std::string field;
    while (std::getline(input, field, ',')) fields.push_back(field);
    if (fields.size() < 10 || fields[2].empty() || fields[3].empty() || fields[4].empty() ||
        fields[5].empty() || fields[6].empty()) return;
    try {
      sensor_msgs::msg::NavSatFix fix;
      fix.header.stamp = now();
      fix.header.frame_id = "gnss";
      const int quality = std::stoi(fields[6]);
      fix.status.status = quality > 0 ? sensor_msgs::msg::NavSatStatus::STATUS_FIX
                                      : sensor_msgs::msg::NavSatStatus::STATUS_NO_FIX;
      fix.status.service = sensor_msgs::msg::NavSatStatus::SERVICE_GPS;
      fix.latitude = ToDegrees(fields[2], fields[3]);
      fix.longitude = ToDegrees(fields[4], fields[5]);
      fix.altitude = fields[9].empty() ? 0.0 : std::stod(fields[9]);
      fix.position_covariance_type = sensor_msgs::msg::NavSatFix::COVARIANCE_TYPE_UNKNOWN;
      fix_publisher_->publish(fix);
    } catch (const std::exception&) {
      // A malformed but checksum-valid GGA sentence must not stop the serial bridge.
    }
  }

  void WriteRtcm(const mavros_msgs::msg::RTCM& message) {
    if (message.data.empty() || !Open()) return;
    std::lock_guard<std::mutex> lock(serial_mutex_);
    std::size_t offset = 0;
    while (offset < message.data.size()) {
      pollfd poll_fd{fd_, POLLOUT, 0};
      const int ready = poll(&poll_fd, 1, write_timeout_ms_);
      if (ready <= 0 || (poll_fd.revents & POLLOUT) == 0) {
        RCLCPP_WARN(get_logger(), "serial write timed out");
        LogOperation("serial RTCM write timed out");
        return;
      }
      const auto written = write(fd_, message.data.data() + offset, message.data.size() - offset);
      if (written <= 0) {
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        RCLCPP_WARN(get_logger(), "serial write error: %s", std::strerror(errno));
        LogOperation(std::string{"serial RTCM write error: "} + std::strerror(errno));
        close(fd_); fd_ = -1;
        return;
      }
      offset += static_cast<std::size_t>(written);
    }
    LogOperation("forwarded RTCM frame; bytes=" + std::to_string(message.data.size()));
  }

  std::string port_;
  int baud_rate_{};
  int write_timeout_ms_{};
  int reconnect_interval_ms_{};
  bool require_nmea_checksum_{};
  bool publish_nav_sat_fix_{};
  int fd_{-1};
  std::mutex serial_mutex_;
  std::string input_;
  std::chrono::steady_clock::time_point last_open_error_{};
  rclcpp::Publisher<nmea_msgs::msg::Sentence>::SharedPtr nmea_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::NavSatFix>::SharedPtr fix_publisher_;
  rclcpp::Subscription<mavros_msgs::msg::RTCM>::SharedPtr rtcm_subscription_;
  rclcpp::TimerBase::SharedPtr read_timer_;
  std::unique_ptr<RotatingFileLogger> operation_log_;
  std::unique_ptr<RotatingFileLogger> nmea_log_;
};
}  // namespace kalmix_gnss_serial

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<kalmix_gnss_serial::SerialBridge>());
  rclcpp::shutdown();
  return 0;
}
