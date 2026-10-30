#ifndef VIPEE_BRIDGE_TEST_SERVER_H_
#define VIPEE_BRIDGE_TEST_SERVER_H_

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "vipee/wire.h"

namespace vipee {

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

}  // namespace vipee

#endif  // VIPEE_BRIDGE_TEST_SERVER_H_
