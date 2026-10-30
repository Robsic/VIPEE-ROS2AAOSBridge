#include "vipee_bridge/socket_sink.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace vipee {
namespace {

// Slot of a property in the sink's state tables, or -1 if not one of the four.
int slotOf(int32_t prop_id) {
  switch (prop_id) {
    case prop::kPerfVehicleSpeed:
      return 0;
    case prop::kGearSelection:
      return 1;
    case prop::kCurrentGear:
      return 2;
    case prop::kParkingBrakeOn:
      return 3;
    default:
      return -1;
  }
}

}  // namespace

SocketSink::SocketSink(const std::string& path) : path_(path) {
  if (path.size() >= sizeof(sockaddr_un::sun_path)) {
    throw std::invalid_argument("socket path too long: " + path);
  }
}

SocketSink::~SocketSink() { disconnect(); }

void SocketSink::disconnect() {
  if (fd_ >= 0) close(fd_);
  fd_ = -1;
}

bool SocketSink::send(const WireRecord& record) {
  uint8_t buf[kWireRecordSize];
  encode(record, buf);
  ssize_t n;
  do {
    n = ::send(fd_, buf, sizeof buf, MSG_DONTWAIT | MSG_NOSIGNAL);
  } while (n < 0 && errno == EINTR);
  if (n == static_cast<ssize_t>(sizeof buf)) return true;
  disconnect();  // closed peer or full buffer: reconnect and resync
  return false;
}

void SocketSink::publish(const PropertyUpdate& u) {
  const int slot = slotOf(u.prop_id);
  if (slot < 0) return;
  WireRecord r{};
  r.version = kWireVersion;
  r.kind = static_cast<uint16_t>(WireKind::kUpdate);
  r.prop_id = u.prop_id;
  r.status = static_cast<int32_t>(u.status);
  r.int_value = u.int_value;
  r.float_value = u.float_value;
  r.timestamp_ns = u.timestamp_ns;
  state_[slot] = r;
  has_state_[slot] = true;
  if (fd_ >= 0) send(r);
}

void SocketSink::poll(int64_t now_ns) {
  if (fd_ < 0) {
    if (tried_ && now_ns - last_attempt_ns_ < kReconnectPeriodNs) return;
    tried_ = true;
    last_attempt_ns_ = now_ns;
    const int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path_.c_str(), sizeof addr.sun_path - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
      close(fd);
      return;
    }
    fd_ = fd;
    for (size_t i = 0; i < state_.size() && fd_ >= 0; ++i) {
      if (has_state_[i]) send(state_[i]);
    }
    last_heartbeat_ns_ = now_ns - kHeartbeatPeriodNs;  // heartbeat below
  }
  if (fd_ >= 0 && now_ns - last_heartbeat_ns_ >= kHeartbeatPeriodNs) {
    WireRecord hb{};
    hb.version = kWireVersion;
    hb.kind = static_cast<uint16_t>(WireKind::kHeartbeat);
    hb.timestamp_ns = now_ns;
    last_heartbeat_ns_ = now_ns;
    send(hb);
  }
}

}  // namespace vipee
