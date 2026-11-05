#include "vipee/dds_peer.h"

#include <gtest/gtest.h>

namespace vipee {
namespace {

TEST(DdsPeer, XmlWithoutVariableStartsAndLeavesEnvironment) {
  for (const char* host : {"", "192.168.0.10", "bad value:1"}) {
    DdsPeerDecision d = DecideDdsPeer(false, host);
    EXPECT_TRUE(d.start) << host;
    EXPECT_FALSE(d.vipee_host.has_value()) << host;
    EXPECT_EQ(d.error, DdsPeerError::kNone) << host;
  }
}

TEST(DdsPeer, ValidIpv4AndHostnameStartWithVipeeHost) {
  for (const char* host :
       {"192.168.0.10", "10.0.2.2", "sim-host.lan", "Host1"}) {
    DdsPeerDecision d = DecideDdsPeer(true, host);
    EXPECT_TRUE(d.start) << host;
    ASSERT_TRUE(d.vipee_host.has_value()) << host;
    EXPECT_EQ(*d.vipee_host, host);
    EXPECT_EQ(d.error, DdsPeerError::kNone) << host;
  }
}

TEST(DdsPeer, EmptyValueIsUnset) {
  DdsPeerDecision d = DecideDdsPeer(true, "");
  EXPECT_FALSE(d.start);
  EXPECT_FALSE(d.vipee_host.has_value());
  EXPECT_EQ(d.error, DdsPeerError::kUnset);
  EXPECT_STREQ(DdsPeerErrorName(d.error), "DDS_PEER_UNSET");
}

TEST(DdsPeer, InvalidCharactersAreRejected) {
  for (const char* host : {"a b", " 1.2.3.4", "1.2.3.4 ", "1.2.3.4:7400", "a/b",
                           "a_b", "h\n", "ação"}) {
    DdsPeerDecision d = DecideDdsPeer(true, host);
    EXPECT_FALSE(d.start) << host;
    EXPECT_FALSE(d.vipee_host.has_value()) << host;
    EXPECT_EQ(d.error, DdsPeerError::kInvalid) << host;
  }
  EXPECT_STREQ(DdsPeerErrorName(DdsPeerError::kInvalid), "DDS_PEER_INVALID");
}

}  // namespace
}  // namespace vipee
