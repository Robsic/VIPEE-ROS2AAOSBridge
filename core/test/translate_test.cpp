#include "vipee/translate.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "test_maps.h"

namespace vipee {
namespace {

TEST(SpeedKmhToMps, Converts36KmhTo10Mps) {
  const SpeedResult r = speedKmhToMps(36.0, Cat793fMap());
  EXPECT_EQ(r.reason, Reason::kNone);
  EXPECT_FLOAT_EQ(r.mps, 10.0f);
}

TEST(SpeedKmhToMps, KeepsSignWhenMovingBackwards) {
  const SpeedResult r = speedKmhToMps(-18.0, Cat793fMap());
  EXPECT_EQ(r.reason, Reason::kNone);
  EXPECT_FLOAT_EQ(r.mps, -5.0f);
}

TEST(SpeedKmhToMps, NegativeZeroBecomesPositiveZero) {
  const SpeedResult r = speedKmhToMps(-0.0, Cat793fMap());
  EXPECT_EQ(r.reason, Reason::kNone);
  EXPECT_EQ(r.mps, 0.0f);
  EXPECT_FALSE(std::signbit(r.mps));
}

TEST(SpeedKmhToMps, LimitIsInclusive) {
  EXPECT_EQ(speedKmhToMps(80.0, Cat793fMap()).reason, Reason::kNone);
  EXPECT_EQ(speedKmhToMps(-80.0, Cat793fMap()).reason, Reason::kNone);
}

TEST(SpeedKmhToMps, AboveLimitIsOutOfRange) {
  EXPECT_EQ(speedKmhToMps(80.1, Cat793fMap()).reason, Reason::kOutOfRange);
  EXPECT_EQ(speedKmhToMps(-80.1, Cat793fMap()).reason, Reason::kOutOfRange);
}

TEST(SpeedKmhToMps, NanAndInfinityAreOutOfRange) {
  const double inf = std::numeric_limits<double>::infinity();
  EXPECT_EQ(speedKmhToMps(std::nan(""), Cat793fMap()).reason,
            Reason::kOutOfRange);
  EXPECT_EQ(speedKmhToMps(inf, Cat793fMap()).reason, Reason::kOutOfRange);
  EXPECT_EQ(speedKmhToMps(-inf, Cat793fMap()).reason, Reason::kOutOfRange);
}

TEST(CurrentGear, NegativeGearsAreReverse) {
  EXPECT_EQ(currentGearFromEngaged(-1).gear, 0x0002);
  EXPECT_EQ(currentGearFromEngaged(-2).gear, 0x0002);
  EXPECT_EQ(currentGearFromEngaged(-9).gear, 0x0002);
  EXPECT_EQ(currentGearFromEngaged(-2).reason, Reason::kNone);
}

TEST(CurrentGear, ZeroIsNeutral) {
  EXPECT_EQ(currentGearFromEngaged(0).gear, 0x0001);
  EXPECT_EQ(currentGearFromEngaged(0).reason, Reason::kNone);
}

TEST(CurrentGear, ForwardGearsMapToGearN) {
  const int32_t expected[] = {0x0010, 0x0020, 0x0040, 0x0080, 0x0100,
                              0x0200, 0x0400, 0x0800, 0x1000};
  for (int n = 1; n <= 9; ++n) {
    EXPECT_EQ(currentGearFromEngaged(n).gear, expected[n - 1]) << n;
    EXPECT_EQ(currentGearFromEngaged(n).reason, Reason::kNone) << n;
  }
}

TEST(CurrentGear, OutsideMinus9To9IsUnknownGear) {
  EXPECT_EQ(currentGearFromEngaged(10).reason, Reason::kUnknownGear);
  EXPECT_EQ(currentGearFromEngaged(-10).reason, Reason::kUnknownGear);
}

TEST(DerivedSelection, ReverseNeutralDrive) {
  EXPECT_EQ(derivedSelectionFromEngaged(-1).gear, 0x0002);
  EXPECT_EQ(derivedSelectionFromEngaged(0).gear, 0x0001);
  for (int n = 1; n <= 9; ++n) {
    EXPECT_EQ(derivedSelectionFromEngaged(n).gear, 0x0008) << n;
  }
}

TEST(DerivedSelection, NeverPark) {
  for (int g = -128; g <= 127; ++g) {
    const GearResult r = derivedSelectionFromEngaged(g);
    EXPECT_NE(r.gear, 0x0004) << g;
    if (g < -9 || g > 9) {
      EXPECT_EQ(r.reason, Reason::kUnknownGear) << g;
    } else {
      EXPECT_EQ(r.reason, Reason::kNone) << g;
    }
  }
}

TEST(LeverSelection, Cat793fCodes) {
  const VehicleMap m = Cat793fMap();
  EXPECT_EQ(selectionFromLever(80, m).gear, 0x0004);  // 'P'
  EXPECT_EQ(selectionFromLever(82, m).gear, 0x0002);  // 'R'
  EXPECT_EQ(selectionFromLever(78, m).gear, 0x0001);  // 'N'
  EXPECT_EQ(selectionFromLever(68, m).gear, 0x0008);  // 'D'
  EXPECT_EQ(selectionFromLever(49, m).gear, 0x0010);  // '1'
  EXPECT_EQ(selectionFromLever(50, m).gear, 0x0020);  // '2'
  EXPECT_EQ(selectionFromLever(80, m).reason, Reason::kNone);
}

TEST(LeverSelection, UnknownCodeIsUnknownGear) {
  EXPECT_EQ(selectionFromLever(51, Cat793fMap()).reason, Reason::kUnknownGear);
  EXPECT_EQ(selectionFromLever(0, Cat793fMap()).reason, Reason::kUnknownGear);
}

}  // namespace
}  // namespace vipee
