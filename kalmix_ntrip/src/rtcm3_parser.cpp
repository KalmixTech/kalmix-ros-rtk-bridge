// Copyright 2026 KALMIX
// SPDX-License-Identifier: Apache-2.0
#include "kalmix_ntrip/rtcm3_parser.hpp"

#include <algorithm>

namespace kalmix_ntrip {
namespace {
constexpr std::uint8_t kPreamble = 0xD3;
constexpr std::size_t kHeaderBytes = 3;
constexpr std::size_t kCrcBytes = 3;
constexpr std::size_t kMaxPayloadBytes = 1023;
}

Rtcm3Parser::Rtcm3Parser(MessageCallback callback) : callback_(std::move(callback)) {}

std::uint32_t Rtcm3Parser::Crc24q(const std::uint8_t* data, std::size_t size) {
  std::uint32_t crc = 0;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= static_cast<std::uint32_t>(data[i]) << 16;
    for (int bit = 0; bit < 8; ++bit) {
      crc <<= 1;
      if ((crc & 0x1000000U) != 0U) crc ^= 0x1864CFBU;
    }
  }
  return crc & 0xFFFFFFU;
}

bool Rtcm3Parser::HasValidCrc(const std::uint8_t* frame, std::size_t size) {
  if (size < kHeaderBytes + kCrcBytes || frame[0] != kPreamble) return false;
  const std::uint32_t received = (static_cast<std::uint32_t>(frame[size - 3]) << 16) |
      (static_cast<std::uint32_t>(frame[size - 2]) << 8) | frame[size - 1];
  return Crc24q(frame, size - kCrcBytes) == received;
}

void Rtcm3Parser::Push(const std::uint8_t* data, std::size_t size) {
  if (data == nullptr || size == 0) return;
  buffer_.insert(buffer_.end(), data, data + size);
  Parse();
}

void Rtcm3Parser::Parse() {
  while (buffer_.size() >= kHeaderBytes) {
    const auto first = std::find(buffer_.begin(), buffer_.end(), kPreamble);
    if (first == buffer_.end()) {
      buffer_.clear();
      return;
    }
    buffer_.erase(buffer_.begin(), first);
    if (buffer_.size() < kHeaderBytes) return;

    const std::size_t payload_size =
        (static_cast<std::size_t>(buffer_[1] & 0x03U) << 8) | buffer_[2];
    if (payload_size > kMaxPayloadBytes) {
      buffer_.erase(buffer_.begin());
      continue;
    }
    const std::size_t frame_size = kHeaderBytes + payload_size + kCrcBytes;
    if (buffer_.size() < frame_size) return;
    if (HasValidCrc(buffer_.data(), frame_size)) {
      callback_(std::vector<std::uint8_t>(buffer_.begin(), buffer_.begin() + frame_size));
      buffer_.erase(buffer_.begin(), buffer_.begin() + frame_size);
    } else {
      // Drop only the preamble so a valid frame inside corrupt input can still be found.
      buffer_.erase(buffer_.begin());
    }
  }
}

}  // namespace kalmix_ntrip
