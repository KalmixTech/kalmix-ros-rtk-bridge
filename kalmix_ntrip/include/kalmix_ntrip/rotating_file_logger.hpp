// Copyright 2026 KALMIX
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace kalmix_ntrip {

// A deliberately small, synchronous writer for operational data.  Each
// instance owns a distinct filename, so different nodes never share a file.
class RotatingFileLogger {
 public:
  RotatingFileLogger(bool enabled, std::string directory, std::string filename,
                     std::uintmax_t max_bytes, int max_files)
      : enabled_(enabled), directory_(std::move(directory)), filename_(std::move(filename)),
        max_bytes_(max_bytes), max_files_(max_files) {
    if (!enabled_) return;
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) {
      enabled_ = false;
      failure_ = "cannot create log directory: " + error.message();
    }
  }

  void Write(const std::string& text) { WriteBytes(text.data(), text.size()); }

  void WriteLine(const std::string& text) { Write(text + "\n"); }

  void WriteOperation(const std::string& message) { WriteLine(Timestamp() + " " + message); }

  void WriteBytes(const void* data, std::size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || size == 0U) return;
    if (max_bytes_ > 0U && CurrentSize() > 0U && CurrentSize() + size > max_bytes_) Rotate();
    std::ofstream output(Path(), std::ios::binary | std::ios::app);
    if (!output) {
      enabled_ = false;
      failure_ = "cannot open log file";
      return;
    }
    output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!output) {
      enabled_ = false;
      failure_ = "cannot write log file";
    }
  }

  bool enabled() const { return enabled_; }
  const std::string& failure() const { return failure_; }

 private:
  std::filesystem::path Path() const { return std::filesystem::path(directory_) / filename_; }

  std::uintmax_t CurrentSize() const {
    std::error_code error;
    const auto size = std::filesystem::file_size(Path(), error);
    return error ? 0U : size;
  }

  void Rotate() {
    std::error_code error;
    const auto current = Path();
    const auto oldest = current.string() + "." + std::to_string(max_files_);
    std::filesystem::remove(oldest, error);
    for (int index = max_files_ - 1; index >= 1; --index) {
      const auto source = current.string() + "." + std::to_string(index);
      const auto destination = current.string() + "." + std::to_string(index + 1);
      std::filesystem::rename(source, destination, error);
      error.clear();  // A missing archive is normal on the first rotations.
    }
    std::filesystem::rename(current, current.string() + ".1", error);
    if (error) {
      enabled_ = false;
      failure_ = "cannot rotate log file: " + error.message();
    }
  }

  static std::string Timestamp() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_r(&now, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
  }

  bool enabled_{};
  std::string directory_;
  std::string filename_;
  std::uintmax_t max_bytes_{};
  int max_files_{};
  std::string failure_;
  std::mutex mutex_;
};

}  // namespace kalmix_ntrip
