#include "vipee_bridge/socket_sink.h"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace vipee {
namespace {

constexpr int64_t kMs = 1000000;
constexpr int64_t kT0 = 5000 * kMs;

// SOCK_SEQPACKET server on a path inside a private temp directory.
class TestServer {
 public:
  TestServer() {
    char tmpl[] = "/tmp/vipee_sink_XXXXXX";
    dir_ = mkdtemp(tmpl);
    path_ = dir_ + "/vipee_vhal";
  }
  ~TestServer() {
    Stop();
    unlink(path_.c_str());
    rmdir(dir_.c_str());
  }
  const std::string& path() const { return path_; }

  void Start() {
    listen_fd_ = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, path_.c_str(), sizeof a.sun_path - 1);
    ASSERT_EQ(0, bind(listen_fd_, reinterpret_cast<sockaddr*>(&a), sizeof a));
    ASSERT_EQ(0, listen(listen_fd_, 1));
  }
  void Stop() {
    DropClient();
    if (listen_fd_ >= 0) close(listen_fd_);
    listen_fd_ = -1;
    unlink(path_.c_str());
  }
  void DropClient() {
    if (client_fd_ >= 0) close(client_fd_);
    client_fd_ = -1;
  }

  // Accepts a pending connection, then returns every record received so far.
  std::vector<WireRecord> Drain() {
    std::vector<WireRecord> out;
    if (client_fd_ < 0 && listen_fd_ >= 0) {
      client_fd_ = accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
    }
    if (client_fd_ < 0) return out;
    uint8_t buf[64];
    for (;;) {
      const ssize_t n = recv(client_fd_, buf, sizeof buf, 0);
      if (n <= 0) break;
      WireRecord r{};
      EXPECT_EQ(WireError::kNone, decode(buf, static_cast<size_t>(n), &r));
      out.push_back(r);
    }
    return out;
  }

 private:
  std::string dir_, path_;
  int listen_fd_ = -1;
  int client_fd_ = -1;
};

size_t Count(const std::vector<WireRecord>& rs, WireKind kind) {
  size_t n = 0;
  for (const auto& r : rs) n += (r.kind == static_cast<uint16_t>(kind));
  return n;
}

std::optional<WireRecord> Last(const std::vector<WireRecord>& rs,
                               int32_t prop) {
  std::optional<WireRecord> last;
  for (const auto& r : rs) {
    if (r.kind == static_cast<uint16_t>(WireKind::kUpdate) &&
        r.prop_id == prop) {
      last = r;
    }
  }
  return last;
}

TEST(SocketSink, SendsUpdatesWithEveryFieldOnceConnected) {
  TestServer srv;
  srv.Start();
  SocketSink sink(srv.path());
  sink.poll(kT0);
  ASSERT_TRUE(sink.connected());
  srv.Drain();

  sink.publish(PropertyUpdate{prop::kPerfVehicleSpeed, Status::kAvailable, 0,
                              -5.0f, kT0 + 7});
  sink.publish(PropertyUpdate{prop::kCurrentGear, Status::kError, gear::kDrive,
                              0.0f, kT0 + 9});
  const auto rs = srv.Drain();

  ASSERT_EQ(2u, rs.size());
  EXPECT_EQ(kWireVersion, rs[0].version);
  EXPECT_EQ(static_cast<uint16_t>(WireKind::kUpdate), rs[0].kind);
  EXPECT_EQ(prop::kPerfVehicleSpeed, rs[0].prop_id);
  EXPECT_EQ(0, rs[0].status);
  EXPECT_EQ(-5.0f, rs[0].float_value);
  EXPECT_EQ(kT0 + 7, rs[0].timestamp_ns);
  EXPECT_EQ(prop::kCurrentGear, rs[1].prop_id);
  EXPECT_EQ(2, rs[1].status);
  EXPECT_EQ(gear::kDrive, rs[1].int_value);
  EXPECT_EQ(kT0 + 9, rs[1].timestamp_ns);
}

TEST(SocketSink, HeartbeatEvery100MsWhileConnected) {
  TestServer srv;
  srv.Start();
  SocketSink sink(srv.path());

  sink.poll(kT0);  // connects, first heartbeat
  EXPECT_EQ(1u, Count(srv.Drain(), WireKind::kHeartbeat));
  sink.poll(kT0 + 99 * kMs);
  EXPECT_EQ(0u, Count(srv.Drain(), WireKind::kHeartbeat));
  sink.poll(kT0 + 100 * kMs);
  const auto rs = srv.Drain();
  ASSERT_EQ(1u, Count(rs, WireKind::kHeartbeat));
  EXPECT_EQ(0, rs[0].prop_id);
  EXPECT_EQ(kT0 + 100 * kMs, rs[0].timestamp_ns);
}

TEST(SocketSink, ResendsLatestStateOfEveryPropertyAfterReconnect) {
  TestServer srv;
  srv.Start();
  SocketSink sink(srv.path());
  sink.poll(kT0);
  srv.Drain();
  sink.publish(PropertyUpdate{prop::kPerfVehicleSpeed, Status::kAvailable, 0,
                              1.0f, kT0 + 1});
  sink.publish(PropertyUpdate{prop::kPerfVehicleSpeed, Status::kAvailable, 0,
                              2.0f, kT0 + 2});
  sink.publish(PropertyUpdate{prop::kParkingBrakeOn, Status::kAvailable, 1,
                              0.0f, kT0 + 3});
  srv.Drain();

  srv.DropClient();            // VHAL restarts
  sink.poll(kT0 + 100 * kMs);  // heartbeat fails, connection dropped
  EXPECT_FALSE(sink.connected());
  sink.poll(kT0 + 600 * kMs);  // 500 ms after the last attempt
  ASSERT_TRUE(sink.connected());
  const auto rs = srv.Drain();

  ASSERT_EQ(2u, Count(rs, WireKind::kUpdate));
  EXPECT_EQ(2.0f, Last(rs, prop::kPerfVehicleSpeed)->float_value);
  EXPECT_EQ(kT0 + 2, Last(rs, prop::kPerfVehicleSpeed)->timestamp_ns);
  EXPECT_EQ(1, Last(rs, prop::kParkingBrakeOn)->int_value);
  EXPECT_FALSE(Last(rs, prop::kGearSelection).has_value());
}

TEST(SocketSink, AbsentServerNeitherBlocksNorLosesTheLatestState) {
  TestServer srv;  // not started yet
  SocketSink sink(srv.path());
  sink.poll(kT0);
  EXPECT_FALSE(sink.connected());
  sink.publish(PropertyUpdate{prop::kCurrentGear, Status::kAvailable,
                              gear::kReverse, 0.0f, kT0 + 1});
  sink.publish(PropertyUpdate{prop::kCurrentGear, Status::kAvailable,
                              gear::kDrive, 0.0f, kT0 + 2});

  srv.Start();
  sink.poll(kT0 + 499 * kMs);  // too early for a new attempt
  EXPECT_FALSE(sink.connected());
  sink.poll(kT0 + 500 * kMs);
  ASSERT_TRUE(sink.connected());
  const auto rs = srv.Drain();
  ASSERT_TRUE(Last(rs, prop::kCurrentGear).has_value());
  EXPECT_EQ(gear::kDrive, Last(rs, prop::kCurrentGear)->int_value);
  EXPECT_EQ(kT0 + 2, Last(rs, prop::kCurrentGear)->timestamp_ns);
}

TEST(SocketSink, PathThatDoesNotFitSockaddrIsRefused) {
  EXPECT_THROW(SocketSink(std::string(200, 'a')), std::invalid_argument);
}

}  // namespace
}  // namespace vipee
