#include "vipee/jpeg_yuyv.h"

#include <gtest/gtest.h>
#include <turbojpeg.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace vipee {
namespace {

constexpr int kW = 1280;
constexpr int kH = 720;
constexpr int kCell = 16;  // One 4:2:0 MCU, so cells do not bleed.

struct Rgb {
  int r, g, b;
};
const Rgb kColors[3] = {{200, 30, 40}, {20, 180, 60}, {40, 70, 220}};

// Checkerboard of 16x16 cells in three colors: varies along x and along y,
// so a wrong chroma row or column in the packing lands on another color.
int colorAt(int x, int y) { return (x / kCell + 2 * (y / kCell)) % 3; }

// JFIF (BT.601 full range), what a JPEG stores.
int lumaOf(Rgb c) {
  return static_cast<int>(std::lround(0.299 * c.r + 0.587 * c.g + 0.114 * c.b));
}
int cbOf(Rgb c) {
  return static_cast<int>(
      std::lround(128 - 0.168736 * c.r - 0.331264 * c.g + 0.5 * c.b));
}
int crOf(Rgb c) {
  return static_cast<int>(
      std::lround(128 + 0.5 * c.r - 0.418688 * c.g - 0.081312 * c.b));
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<uint8_t> makeJpeg(int w, int h, int subsamp) {
  std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const Rgb c = kColors[colorAt(x, y)];
      uint8_t* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
      p[0] = static_cast<uint8_t>(c.r);
      p[1] = static_cast<uint8_t>(c.g);
      p[2] = static_cast<uint8_t>(c.b);
    }
  }
  tjhandle h_ = tj3Init(TJINIT_COMPRESS);
  EXPECT_NE(h_, nullptr);
  tj3Set(h_, TJPARAM_SUBSAMP, subsamp);
  tj3Set(h_, TJPARAM_QUALITY, 95);
  unsigned char* buf = nullptr;
  size_t size = 0;
  EXPECT_EQ(tj3Compress8(h_, rgb.data(), w, 0, h, TJPF_RGB, &buf, &size), 0);
  std::vector<uint8_t> jpeg(buf, buf + size);
  tj3Free(buf);
  tj3Destroy(h_);
  return jpeg;
}

// Checks every cell center against the source color, within JPEG error.
void expectYuyvMatchesPattern(const std::vector<uint8_t>& yuyv) {
  ASSERT_EQ(yuyv.size(), static_cast<size_t>(kW) * kH * 2);
  constexpr int kTol = 4;
  for (int y = kCell / 2; y < kH; y += kCell) {
    for (int x = kCell / 2; x < kW; x += kCell) {  // x even: Y0 U Y1 V.
      const Rgb c = kColors[colorAt(x, y)];
      const uint8_t* p = &yuyv[(static_cast<size_t>(y) * kW + x) * 2];
      ASSERT_LE(std::abs(p[0] - lumaOf(c)), kTol) << "Y0 x=" << x << " y=" << y;
      ASSERT_LE(std::abs(p[1] - cbOf(c)), kTol) << "U x=" << x << " y=" << y;
      ASSERT_LE(std::abs(p[2] - lumaOf(c)), kTol) << "Y1 x=" << x << " y=" << y;
      ASSERT_LE(std::abs(p[3] - crOf(c)), kTol) << "V x=" << x << " y=" << y;
    }
  }
}

DropReason decode(const char* format, const std::vector<uint8_t>& jpeg,
                  std::vector<uint8_t>* out) {
  return decodeJpegToYuyv(format, jpeg.data(), jpeg.size(), kW, kH, out);
}

TEST(JpegYuyv, Jpeg420OfTheMapSizeDecodesToYuyv) {
  std::vector<uint8_t> out;
  EXPECT_EQ(decode("jpeg", makeJpeg(kW, kH, TJSAMP_420), &out),
            DropReason::kNone);
  expectYuyvMatchesPattern(out);
}

TEST(JpegYuyv, Jpeg422OfTheMapSizeDecodesToYuyv) {
  std::vector<uint8_t> out;
  EXPECT_EQ(decode("jpeg", makeJpeg(kW, kH, TJSAMP_422), &out),
            DropReason::kNone);
  expectYuyvMatchesPattern(out);
}

TEST(JpegYuyv, CompressedImageFormatNamingJpegIsAccepted) {
  std::vector<uint8_t> out;
  EXPECT_EQ(
      decode("rgb8; jpeg compressed bgr8", makeJpeg(kW, kH, TJSAMP_420), &out),
      DropReason::kNone);
  expectYuyvMatchesPattern(out);
}

TEST(JpegYuyv, OtherSizeIsFrameSizeMismatchWithoutDecoding) {
  std::vector<uint8_t> out = {7, 7, 7};
  EXPECT_EQ(decode("jpeg", makeJpeg(480, 480, TJSAMP_420), &out),
            DropReason::kFrameSizeMismatch);
  EXPECT_EQ(out, (std::vector<uint8_t>{7, 7, 7}));
}

TEST(JpegYuyv, FormatWithoutJpegIsUnsupported) {
  std::vector<uint8_t> out = {7};
  EXPECT_EQ(decode("png", makeJpeg(kW, kH, TJSAMP_420), &out),
            DropReason::kFormatUnsupported);
  EXPECT_EQ(decode("", makeJpeg(kW, kH, TJSAMP_420), &out),
            DropReason::kFormatUnsupported);
  EXPECT_EQ(out, (std::vector<uint8_t>{7}));
}

TEST(JpegYuyv, TruncatedJpegIsDecodeError) {
  std::vector<uint8_t> jpeg = makeJpeg(kW, kH, TJSAMP_420);
  jpeg.resize(jpeg.size() / 2);
  std::vector<uint8_t> out = {7};
  EXPECT_EQ(decode("jpeg", jpeg, &out), DropReason::kDecodeError);
  EXPECT_EQ(out, (std::vector<uint8_t>{7}));
}

TEST(JpegYuyv, BytesThatAreNotJpegAreDecodeError) {
  std::vector<uint8_t> out;
  EXPECT_EQ(decode("jpeg", std::vector<uint8_t>(64, 0x42), &out),
            DropReason::kDecodeError);
  EXPECT_EQ(decode("jpeg", {}, &out), DropReason::kDecodeError);
}

TEST(JpegYuyv, Jpeg444IsDecodeError) {
  std::vector<uint8_t> out;
  EXPECT_EQ(decode("jpeg", makeJpeg(kW, kH, TJSAMP_444), &out),
            DropReason::kDecodeError);
}

TEST(JpegYuyv, PackI420RepeatsChromaOnTheNextRow) {
  const uint8_t y[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  const uint8_t u[4] = {30, 31, 32, 33};
  const uint8_t v[4] = {40, 41, 42, 43};
  uint8_t out[32] = {};
  packI420ToYuyv(y, u, v, 4, 4, out);
  const uint8_t expected[32] = {
      1,  30, 2,  40, 3,  31, 4,  41,  // row 0: chroma row 0
      5,  30, 6,  40, 7,  31, 8,  41,  // row 1: chroma row 0 again
      9,  32, 10, 42, 11, 33, 12, 43,  // row 2: chroma row 1
      13, 32, 14, 42, 15, 33, 16, 43,  // row 3: chroma row 1 again
  };
  for (int i = 0; i < 32; ++i) EXPECT_EQ(out[i], expected[i]) << "byte " << i;
}

}  // namespace
}  // namespace vipee
