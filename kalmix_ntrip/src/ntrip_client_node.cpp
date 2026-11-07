// Copyright 2026 KALMIX
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <diagnostic_msgs/msg/key_value.hpp>
#include <mavros_msgs/msg/rtcm.hpp>
#include <nmea_msgs/msg/sentence.hpp>
#include <openssl/ssl.h>
#include <rclcpp/rclcpp.hpp>

#include "kalmix_ntrip/rtcm3_parser.hpp"
#include "kalmix_ntrip/rotating_file_logger.hpp"

namespace kalmix_ntrip {
namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using namespace std::chrono_literals;

namespace {
std::string Trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string Base64Encode(const std::string& input) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((input.size() + 2U) / 3U) * 4U);
  for (std::size_t offset = 0; offset < input.size(); offset += 3) {
    const std::uint32_t a = static_cast<unsigned char>(input[offset]);
    const std::uint32_t b = offset + 1 < input.size()
        ? static_cast<unsigned char>(input[offset + 1]) : 0U;
    const std::uint32_t c = offset + 2 < input.size()
        ? static_cast<unsigned char>(input[offset + 2]) : 0U;
    const std::uint32_t bits = (a << 16U) | (b << 8U) | c;
    out += kAlphabet[(bits >> 18U) & 0x3FU];
    out += kAlphabet[(bits >> 12U) & 0x3FU];
    out += offset + 1 < input.size() ? kAlphabet[(bits >> 6U) & 0x3FU] : '=';
    out += offset + 2 < input.size() ? kAlphabet[bits & 0x3FU] : '=';
  }
  return out;
}

bool IsWouldBlock(const boost::system::error_code& error) {
  return error == asio::error::would_block || error == asio::error::try_again;
}
}  // namespace

class KalmixNtripClient final : public rclcpp::Node {
 public:
  KalmixNtripClient()
      : Node("kalmix_ntrip_client"),
        parser_([this](std::vector<std::uint8_t> frame) { PublishRtcm(std::move(frame)); }) {
    DeclareParameters();
    LoadParameters();
    rtcm_publisher_ = create_publisher<mavros_msgs::msg::RTCM>(rtcm_topic_, rclcpp::QoS(20));
    diagnostics_publisher_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
        diagnostics_topic_, rclcpp::QoS(10));
    nmea_subscription_ = create_subscription<nmea_msgs::msg::Sentence>(
        nmea_topic_, rclcpp::QoS(20),
        [this](const nmea_msgs::msg::Sentence::SharedPtr message) { HandleNmea(*message); });
    diagnostics_timer_ = create_wall_timer(1s, [this] { PublishDiagnostics(); });
    worker_ = std::thread([this] { WorkerLoop(); });
  }

  ~KalmixNtripClient() override {
    stopping_.store(true);
    wakeup_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

 private:
  struct Settings {
    std::string host;
    int port{};
    std::string mountpoint;
    std::string username;
    std::string password;
    bool use_tls{};
    bool send_gga{};
    bool require_nmea_checksum{};
    int gga_interval_ms{};
    int connect_timeout_ms{};
    int handshake_timeout_ms{};
    int reconnect_initial_ms{};
    int reconnect_max_ms{};
    int max_reconnect_attempts{};
    bool enable_file_logging{};
    std::string log_directory;
    int log_max_file_size_mb{};
    int log_max_files{};
  } settings_;

  void DeclareParameters() {
    declare_parameter("host", "");
    declare_parameter("port", 2101);
    declare_parameter("mountpoint", "");
    declare_parameter("username", "");
    declare_parameter("password", "");
    declare_parameter("username_env", "KALMIX_NTRIP_USERNAME");
    declare_parameter("password_env", "KALMIX_NTRIP_PASSWORD");
    declare_parameter("use_tls", false);
    declare_parameter("send_gga", true);
    declare_parameter("require_nmea_checksum", true);
    declare_parameter("gga_interval_ms", 1000);
    declare_parameter("connect_timeout_ms", 10000);
    declare_parameter("handshake_timeout_ms", 10000);
    declare_parameter("reconnect_initial_ms", 1000);
    declare_parameter("reconnect_max_ms", 30000);
    declare_parameter("max_reconnect_attempts", 0);
    declare_parameter("nmea_topic", "/nmea");
    declare_parameter("rtcm_topic", "/rtcm");
    declare_parameter("diagnostics_topic", "/diagnostics");
    declare_parameter("enable_file_logging", true);
    declare_parameter("log_directory", "/root/kalmix-rtk-logs");
    declare_parameter("log_max_file_size_mb", 50);
    declare_parameter("log_max_files", 7);
  }

  static std::string ReadSecret(const std::string& explicit_value, const std::string& env_name) {
    if (!explicit_value.empty()) return explicit_value;
    if (env_name.empty()) return {};
    const char* value = std::getenv(env_name.c_str());
    return value == nullptr ? std::string{} : std::string{value};
  }

  void LoadParameters() {
    settings_.host = Trim(get_parameter("host").as_string());
    settings_.port = static_cast<int>(get_parameter("port").as_int());
    settings_.mountpoint = Trim(get_parameter("mountpoint").as_string());
    while (!settings_.mountpoint.empty() && settings_.mountpoint.front() == '/') {
      settings_.mountpoint.erase(settings_.mountpoint.begin());
    }
    settings_.username = ReadSecret(get_parameter("username").as_string(),
        get_parameter("username_env").as_string());
    settings_.password = ReadSecret(get_parameter("password").as_string(),
        get_parameter("password_env").as_string());
    settings_.use_tls = get_parameter("use_tls").as_bool();
    settings_.send_gga = get_parameter("send_gga").as_bool();
    settings_.require_nmea_checksum = get_parameter("require_nmea_checksum").as_bool();
    settings_.gga_interval_ms = static_cast<int>(get_parameter("gga_interval_ms").as_int());
    settings_.connect_timeout_ms = static_cast<int>(get_parameter("connect_timeout_ms").as_int());
    settings_.handshake_timeout_ms = static_cast<int>(get_parameter("handshake_timeout_ms").as_int());
    settings_.reconnect_initial_ms = static_cast<int>(get_parameter("reconnect_initial_ms").as_int());
    settings_.reconnect_max_ms = static_cast<int>(get_parameter("reconnect_max_ms").as_int());
    settings_.max_reconnect_attempts = static_cast<int>(get_parameter("max_reconnect_attempts").as_int());
    nmea_topic_ = get_parameter("nmea_topic").as_string();
    rtcm_topic_ = get_parameter("rtcm_topic").as_string();
    diagnostics_topic_ = get_parameter("diagnostics_topic").as_string();
    settings_.enable_file_logging = get_parameter("enable_file_logging").as_bool();
    settings_.log_directory = Trim(get_parameter("log_directory").as_string());
    settings_.log_max_file_size_mb = static_cast<int>(get_parameter("log_max_file_size_mb").as_int());
    settings_.log_max_files = static_cast<int>(get_parameter("log_max_files").as_int());

    if (settings_.host.empty() || settings_.mountpoint.empty() || settings_.port < 1 ||
        settings_.port > 65535 || settings_.gga_interval_ms < 200 ||
        settings_.connect_timeout_ms < 100 || settings_.handshake_timeout_ms < 100 ||
        settings_.reconnect_initial_ms < 100 || settings_.reconnect_max_ms < settings_.reconnect_initial_ms) {
      throw std::invalid_argument("invalid NTRIP parameters; see config/ntrip.example.yaml");
    }
    if (settings_.host.find_first_of(" \t\r\n") != std::string::npos ||
        settings_.mountpoint.find_first_of("\r\n") != std::string::npos) {
      throw std::invalid_argument("host and mountpoint must not contain HTTP control characters");
    }
    if (settings_.max_reconnect_attempts < 0) {
      throw std::invalid_argument("max_reconnect_attempts must be zero (unlimited) or positive");
    }
    if (settings_.username.empty() != settings_.password.empty()) {
      throw std::invalid_argument("NTRIP username and password must be set together");
    }
    if (settings_.enable_file_logging && (settings_.log_directory.empty() ||
        settings_.log_max_file_size_mb < 1 || settings_.log_max_file_size_mb > 1024 ||
        settings_.log_max_files < 1 || settings_.log_max_files > 100)) {
      throw std::invalid_argument("invalid file logging parameters");
    }
    if (settings_.enable_file_logging) {
      const auto max_bytes = static_cast<std::uintmax_t>(settings_.log_max_file_size_mb) * 1024U * 1024U;
      operation_log_ = std::make_unique<RotatingFileLogger>(
          true, settings_.log_directory, "kalmix_ntrip_operation.log", max_bytes, settings_.log_max_files);
      rtcm_log_ = std::make_unique<RotatingFileLogger>(
          true, settings_.log_directory, "kalmix_rtcm3.bin", max_bytes, settings_.log_max_files);
      if (!operation_log_->enabled() || !rtcm_log_->enabled()) {
        RCLCPP_ERROR(get_logger(), "file logging disabled: %s %s", operation_log_->failure().c_str(),
            rtcm_log_->failure().c_str());
      } else {
        operation_log_->WriteOperation("NTRIP client starting; host=" + settings_.host +
            "; port=" + std::to_string(settings_.port) + "; mountpoint=" + settings_.mountpoint +
            "; tls=" + (settings_.use_tls ? "true" : "false"));
      }
    }
  }

  static bool IsValidGga(const std::string& sentence, bool require_checksum) {
    if (sentence.rfind("$GPGGA,", 0) != 0 && sentence.rfind("$GNGGA,", 0) != 0) return false;
    const auto star = sentence.find('*');
    if (star == std::string::npos) return !require_checksum;
    if (star + 3 != sentence.size()) return false;
    unsigned expected = 0;
    std::istringstream hex{sentence.substr(star + 1)};
    hex >> std::hex >> expected;
    if (hex.fail()) return false;
    unsigned actual = 0;
    for (std::size_t i = 1; i < star; ++i) actual ^= static_cast<unsigned char>(sentence[i]);
    return actual == expected;
  }

  void HandleNmea(const nmea_msgs::msg::Sentence& message) {
    const std::string sentence = Trim(message.sentence);
    if (!settings_.send_gga || !IsValidGga(sentence, settings_.require_nmea_checksum)) return;
    std::lock_guard<std::mutex> lock(gga_mutex_);
    latest_gga_ = sentence + "\r\n";
  }

  std::string TakeGgaIfDue(std::chrono::steady_clock::time_point& last_sent) {
    std::lock_guard<std::mutex> lock(gga_mutex_);
    if (latest_gga_.empty() || std::chrono::steady_clock::now() - last_sent <
        std::chrono::milliseconds(settings_.gga_interval_ms)) return {};
    last_sent = std::chrono::steady_clock::now();
    return latest_gga_;
  }

  std::string Request() const {
    std::ostringstream request;
    request << "GET /" << settings_.mountpoint << " HTTP/1.1\r\n"
            << "Host: " << settings_.host << ':' << settings_.port << "\r\n"
            << "Ntrip-Version: Ntrip/2.0\r\n"
            << "User-Agent: KALMIX-ROS2-NTRIP/0.1\r\n"
            << "Connection: keep-alive\r\n";
    if (!settings_.username.empty()) {
      request << "Authorization: Basic " << Base64Encode(settings_.username + ':' + settings_.password) << "\r\n";
    }
    return request.str() + "\r\n";
  }

  template <typename Stream>
  bool Connect(Stream& stream, asio::io_context& io) {
    tcp::resolver resolver{io};
    asio::steady_timer timeout{io};
    boost::system::error_code result = asio::error::operation_aborted;
    bool done = false;
    timeout.expires_after(std::chrono::milliseconds(settings_.connect_timeout_ms));
    timeout.async_wait([&](const boost::system::error_code& error) {
      if (!error && !done) stream.lowest_layer().close();
    });
    resolver.async_resolve(settings_.host, std::to_string(settings_.port),
        [&](const boost::system::error_code& error, const tcp::resolver::results_type& endpoints) {
          if (error) { result = error; done = true; timeout.cancel(); return; }
          asio::async_connect(stream.lowest_layer(), endpoints,
              [&](const boost::system::error_code& connect_error, const tcp::endpoint&) {
                result = connect_error; done = true; timeout.cancel();
              });
        });
    io.run();
    io.restart();
    if (result) { SetLastError("connect: " + result.message()); return false; }
    return true;
  }

  bool Handshake(ssl::stream<tcp::socket>& stream, asio::io_context& io) {
    asio::steady_timer timeout{io};
    boost::system::error_code result = asio::error::operation_aborted;
    bool done = false;
    timeout.expires_after(std::chrono::milliseconds(settings_.handshake_timeout_ms));
    timeout.async_wait([&](const boost::system::error_code& error) {
      if (!error && !done) stream.lowest_layer().close();
    });
    stream.async_handshake(ssl::stream_base::client, [&](const boost::system::error_code& error) {
      result = error; done = true; timeout.cancel();
    });
    io.run();
    io.restart();
    if (result) { SetLastError("TLS handshake: " + result.message()); return false; }
    return true;
  }

  template <typename Stream>
  bool WriteAll(Stream& stream, const std::string& data) {
    std::size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(settings_.handshake_timeout_ms);
    while (offset < data.size() && std::chrono::steady_clock::now() < deadline && !stopping_.load()) {
      boost::system::error_code error;
      const auto written = stream.write_some(asio::buffer(data.data() + offset, data.size() - offset), error);
      offset += written;
      if (!error) continue;
      if (IsWouldBlock(error)) { std::this_thread::sleep_for(20ms); continue; }
      SetLastError("send: " + error.message());
      return false;
    }
    if (offset == data.size()) return true;
    SetLastError("send timeout");
    return false;
  }

  template <typename Stream>
  bool ReadResponse(Stream& stream, std::vector<std::uint8_t>* initial_payload) {
    std::array<char, 4096> chunk{};
    std::string response;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(settings_.handshake_timeout_ms);
    while (std::chrono::steady_clock::now() < deadline && !stopping_.load()) {
      boost::system::error_code error;
      const auto count = stream.read_some(asio::buffer(chunk), error);
      if (error && !IsWouldBlock(error)) { SetLastError("response: " + error.message()); return false; }
      if (count > 0) response.append(chunk.data(), count);
      if (response.size() > 16384U) { SetLastError("response headers exceed 16 KiB"); return false; }
      const auto first_line_end = response.find("\r\n");
      if (first_line_end == std::string::npos) { std::this_thread::sleep_for(20ms); continue; }
      const std::string first_line = response.substr(0, first_line_end);
      if (first_line.rfind("ICY 200", 0) == 0) {
        initial_payload->assign(response.begin() + static_cast<std::ptrdiff_t>(first_line_end + 2), response.end());
        return true;
      }
      const auto header_end = response.find("\r\n\r\n");
      if (header_end == std::string::npos) { std::this_thread::sleep_for(20ms); continue; }
      if (first_line.rfind("HTTP/", 0) != 0 || first_line.find(" 200") == std::string::npos) {
        SetLastError("caster rejected request: " + first_line);
        return false;
      }
      const std::string headers = response.substr(0, header_end);
      std::string lower_headers = headers;
      std::transform(lower_headers.begin(), lower_headers.end(), lower_headers.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (lower_headers.find("transfer-encoding: chunked") != std::string::npos) {
        SetLastError("chunked NTRIP response is unsupported by this release");
        return false;
      }
      initial_payload->assign(response.begin() + static_cast<std::ptrdiff_t>(header_end + 4), response.end());
      return true;
    }
    SetLastError("response timeout");
    return false;
  }

  template <typename Stream>
  bool RunStream(Stream& stream) {
    stream.lowest_layer().non_blocking(true);
    if (!WriteAll(stream, Request())) return false;
    std::vector<std::uint8_t> first_payload;
    if (!ReadResponse(stream, &first_payload)) return false;
    if (!first_payload.empty()) parser_.Push(first_payload.data(), first_payload.size());
    connected_.store(true);
    reconnect_attempts_.store(0);
    SetLastError({});
    LogOperation("NTRIP stream connected");
    auto last_gga_sent = std::chrono::steady_clock::now() - std::chrono::hours(1);
    std::array<std::uint8_t, 4096> data{};
    while (!stopping_.load()) {
      const std::string gga = TakeGgaIfDue(last_gga_sent);
      if (!gga.empty() && !WriteAll(stream, gga)) break;
      boost::system::error_code error;
      const auto count = stream.read_some(asio::buffer(data), error);
      if (count > 0) {
        bytes_received_.fetch_add(count);
        parser_.Push(data.data(), count);
      }
      if (error && !IsWouldBlock(error)) {
        if (error != asio::error::eof) SetLastError("stream: " + error.message());
        break;
      }
      if (count == 0) std::this_thread::sleep_for(20ms);
    }
    connected_.store(false);
    LogOperation("NTRIP stream disconnected");
    boost::system::error_code ignored;
    stream.lowest_layer().shutdown(tcp::socket::shutdown_both, ignored);
    stream.lowest_layer().close(ignored);
    return true;
  }

  void WorkerLoop() {
    int delay_ms = settings_.reconnect_initial_ms;
    while (!stopping_.load()) {
      if (settings_.max_reconnect_attempts > 0 &&
          reconnect_attempts_.load() >= static_cast<std::uint64_t>(settings_.max_reconnect_attempts)) {
        SetLastError("maximum reconnect attempts reached");
        return;
      }
      bool session_started = false;
      try {
        asio::io_context io;
        if (settings_.use_tls) {
          ssl::context context{ssl::context::tls_client};
          context.set_default_verify_paths();
          ssl::stream<tcp::socket> stream{io, context};
          stream.set_verify_mode(ssl::verify_peer);
          stream.set_verify_callback(ssl::host_name_verification(settings_.host));
          if (SSL_set_tlsext_host_name(stream.native_handle(), settings_.host.c_str()) != 1) {
            throw std::runtime_error("could not set TLS server name indication");
          }
          if (Connect(stream, io) && Handshake(stream, io)) session_started = RunStream(stream);
        } else {
          tcp::socket stream{io};
          if (Connect(stream, io)) session_started = RunStream(stream);
        }
      } catch (const std::exception& error) {
        SetLastError(std::string{"client exception: "} + error.what());
      }
      if (session_started) delay_ms = settings_.reconnect_initial_ms;
      reconnect_attempts_.fetch_add(1);
      std::unique_lock<std::mutex> lock(wakeup_mutex_);
      wakeup_.wait_for(lock, std::chrono::milliseconds(delay_ms), [this] { return stopping_.load(); });
      delay_ms = std::min(delay_ms * 2, settings_.reconnect_max_ms);
    }
  }

  void PublishRtcm(std::vector<std::uint8_t> frame) {
    if (rtcm_log_) rtcm_log_->WriteBytes(frame.data(), frame.size());
    mavros_msgs::msg::RTCM message;
    message.data = std::move(frame);
    rtcm_messages_.fetch_add(1);
    rtcm_publisher_->publish(message);
  }

  void SetLastError(std::string value) {
    if (!value.empty()) LogOperation("error: " + value);
    std::lock_guard<std::mutex> lock(status_mutex_);
    last_error_ = std::move(value);
  }

  void LogOperation(const std::string& message) {
    if (operation_log_) operation_log_->WriteOperation(message);
  }

  void PublishDiagnostics() {
    diagnostic_msgs::msg::DiagnosticArray array;
    array.header.stamp = now();
    diagnostic_msgs::msg::DiagnosticStatus status;
    status.name = "KALMIX NTRIP Client";
    status.hardware_id = settings_.host + ":" + std::to_string(settings_.port);
    status.level = connected_.load() ? diagnostic_msgs::msg::DiagnosticStatus::OK
                                     : diagnostic_msgs::msg::DiagnosticStatus::ERROR;
    status.message = connected_.load() ? "connected" : "disconnected";
    std::string error;
    { std::lock_guard<std::mutex> lock(status_mutex_); error = last_error_; }
    const auto add_value = [&status](const std::string& key, const std::string& value) {
      diagnostic_msgs::msg::KeyValue item;
      item.key = key;
      item.value = value;
      status.values.push_back(std::move(item));
    };
    add_value("bytes_received", std::to_string(bytes_received_.load()));
    add_value("rtcm_messages", std::to_string(rtcm_messages_.load()));
    add_value("reconnect_attempts", std::to_string(reconnect_attempts_.load()));
    add_value("last_error", error);
    array.status.push_back(std::move(status));
    diagnostics_publisher_->publish(array);
  }

  std::string nmea_topic_;
  std::string rtcm_topic_;
  std::string diagnostics_topic_;
  rclcpp::Publisher<mavros_msgs::msg::RTCM>::SharedPtr rtcm_publisher_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diagnostics_publisher_;
  rclcpp::Subscription<nmea_msgs::msg::Sentence>::SharedPtr nmea_subscription_;
  rclcpp::TimerBase::SharedPtr diagnostics_timer_;
  Rtcm3Parser parser_;
  std::thread worker_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> connected_{false};
  std::atomic<std::uint64_t> bytes_received_{0};
  std::atomic<std::uint64_t> rtcm_messages_{0};
  std::atomic<std::uint64_t> reconnect_attempts_{0};
  std::mutex gga_mutex_;
  std::string latest_gga_;
  std::mutex status_mutex_;
  std::string last_error_;
  std::mutex wakeup_mutex_;
  std::condition_variable wakeup_;
  std::unique_ptr<RotatingFileLogger> operation_log_;
  std::unique_ptr<RotatingFileLogger> rtcm_log_;
};
}  // namespace kalmix_ntrip

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<kalmix_ntrip::KalmixNtripClient>());
  rclcpp::shutdown();
  return 0;
}
