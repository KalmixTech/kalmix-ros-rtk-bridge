// Copyright 2026 KALMIX
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace kalmix_ntrip {

class Rtcm3Parser {
 public:
  using MessageCallback = std::function<void(std::vector<std::uint8_t>)>;
  explicit Rtcm3Parser(MessageCallback callback);
  void Push(const std::uint8_t* data, std::size_t size);
  static bool HasValidCrc(const std::uint8_t* frame, std::size_t size);
  static std::uint32_t Crc24q(const std::uint8_t* data, std::size_t size);

 private:
  void Parse();
  MessageCallback callback_;
  std::vector<std::uint8_t> buffer_;
};

}  // namespace kalmix_ntrip
