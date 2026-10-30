/**
 * @file socket_sink.h
 * @brief PropertySink that sends wire records to the VHAL over a
 * SOCK_SEQPACKET unix socket (AD-013). POSIX only: no ROS, no Android.
 */
#ifndef VIPEE_BRIDGE_SOCKET_SINK_H_
#define VIPEE_BRIDGE_SOCKET_SINK_H_

#include <array>
#include <cstdint>
#include <string>

#include "vipee/bridge.h"
#include "vipee/wire.h"

namespace vipee {

/// @brief Delay between connection attempts, in ns (500 ms).
constexpr int64_t kReconnectPeriodNs = 500000000;

/// @brief Delay between heartbeat records, in ns (100 ms).
constexpr int64_t kHeartbeatPeriodNs = 100000000;

/**
 * @brief Sends the four properties to the VHAL socket without ever blocking.
 *
 * publish() keeps the latest update of each property and sends it at once
 * when connected. poll() (called by the node's 50 ms timer, same thread)
 * connects every kReconnectPeriodNs while disconnected, resends the latest
 * state of every property after each (re)connection and sends a heartbeat
 * every kHeartbeatPeriodNs. Any send error, including a full socket buffer,
 * drops the connection so the next one resyncs the whole state: the VHAL
 * never keeps a silently lost update. Not thread-safe.
 */
class SocketSink : public PropertySink {
 public:
  /**
   * @brief Prepares the sink; no connection is attempted until poll().
   * @param path Socket path, e.g. /dev/socket/vipee_vhal.
   * @throws std::invalid_argument If path does not fit sockaddr_un.
   */
  explicit SocketSink(const std::string& path);
  ~SocketSink() override;
  SocketSink(const SocketSink&) = delete;
  SocketSink& operator=(const SocketSink&) = delete;

  /**
   * @brief Stores the update as the property's current state and sends it
   * if connected. Never blocks.
   * @param update Update with timestamp_ns in CLOCK_BOOTTIME ns.
   */
  void publish(const PropertyUpdate& update) override;

  /**
   * @brief Connects, resyncs and sends the heartbeat when due.
   * @param now_ns CLOCK_BOOTTIME ns.
   */
  void poll(int64_t now_ns);

  /// @brief True while a connection is open.
  bool connected() const { return fd_ >= 0; }

 private:
  bool send(const WireRecord& record);
  void disconnect();

  std::string path_;
  int fd_ = -1;
  bool tried_ = false;
  int64_t last_attempt_ns_ = 0;
  int64_t last_heartbeat_ns_ = 0;
  std::array<WireRecord, 4> state_{};
  std::array<bool, 4> has_state_{};
};

}  // namespace vipee

#endif  // VIPEE_BRIDGE_SOCKET_SINK_H_
