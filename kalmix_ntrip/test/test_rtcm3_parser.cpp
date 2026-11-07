// Copyright 2026 KALMIX
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <vector>

#include "kalmix_ntrip/rtcm3_parser.hpp"

TEST(Rtcm3Parser, PublishesOnlyCompleteCrcCheckedFrames) {
  std::vector<std::vector<std::uint8_t>> received;
  kalmix_ntrip::Rtcm3Parser parser([&received](std::vector<std::uint8_t> frame) {
    received.push_back(std::move(frame));
  });
  std::vector<std::uint8_t> frame{0xD3, 0x00, 0x03, 0xAA, 0xBB, 0xCC};
  const auto crc = kalmix_ntrip::Rtcm3Parser::Crc24q(frame.data(), frame.size());
  frame.push_back(static_cast<std::uint8_t>(crc >> 16U));
  frame.push_back(static_cast<std::uint8_t>(crc >> 8U));
  frame.push_back(static_cast<std::uint8_t>(crc));
  ASSERT_TRUE(kalmix_ntrip::Rtcm3Parser::HasValidCrc(frame.data(), frame.size()));

  const std::vector<std::uint8_t> noise{0x01, 0x02, 0xD2};
  parser.Push(noise.data(), noise.size());
  parser.Push(frame.data(), 4);
  EXPECT_TRUE(received.empty());
  parser.Push(frame.data() + 4, frame.size() - 4);
  ASSERT_EQ(received.size(), 1U);
  EXPECT_EQ(received.front(), frame);
}

TEST(Rtcm3Parser, RejectsBadCrcAndResynchronizes) {
  std::vector<std::vector<std::uint8_t>> received;
  kalmix_ntrip::Rtcm3Parser parser([&received](std::vector<std::uint8_t> frame) {
    received.push_back(std::move(frame));
  });
  std::vector<std::uint8_t> bad{0xD3, 0x00, 0x00, 0x00, 0x00, 0x00};
  parser.Push(bad.data(), bad.size());
  EXPECT_TRUE(received.empty());
}
