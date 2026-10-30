/**
 * @file wire.h
 * @brief Socket protocol between the ROS bridge process and the VHAL (AD-013).
 *
 * One fixed 32-byte little-endian record per SOCK_SEQPACKET datagram on
 * /dev/socket/vipee_vhal. Header-only, stdlib only: compiled by both sides.
 */
#ifndef VIPEE_WIRE_H_
#define VIPEE_WIRE_H_

#include <cstddef>
#include <cstdint>
#include <cstring>

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "wire.h encodes by memcpy and requires a little-endian target"
#endif

namespace vipee {

/// @brief Protocol version carried in every record. A receiver drops others.
constexpr uint16_t kWireVersion = 1;

/// @brief Encoded record size in bytes.
constexpr size_t kWireRecordSize = 32;

/// @brief Record kind.
enum class WireKind : uint16_t {
  kUpdate = 1,     ///< Value and status of one property.
  kHeartbeat = 2,  ///< Liveness only; prop_id is 0.
};

/**
 * @brief One property update or heartbeat, as sent on the socket.
 *
 * @note All fields little-endian at fixed offsets (checked below).
 */
struct WireRecord {
  uint16_t version;      ///< kWireVersion.
  uint16_t kind;         ///< WireKind value.
  int32_t prop_id;       ///< VehicleProperty id (0 on heartbeat).
  int32_t status;        ///< VehiclePropertyStatus: AVAILABLE 0, UNAVAILABLE 1,
                         ///< ERROR 2.
  int32_t int_value;     ///< VehicleGear bit (GEAR_*), or 0/1 for the parking
                         ///< brake.
  float float_value;     ///< PERF_VEHICLE_SPEED in m/s, signed (negative when
                         ///< moving backwards).
  int32_t reserved;      ///< Zero.
  int64_t timestamp_ns;  ///< CLOCK_BOOTTIME in ns at arrival of the source ROS
                         ///< message (same base as elapsedRealtimeNano()).
};

static_assert(sizeof(WireRecord) == kWireRecordSize, "wire record size");
static_assert(offsetof(WireRecord, version) == 0, "wire offset");
static_assert(offsetof(WireRecord, kind) == 2, "wire offset");
static_assert(offsetof(WireRecord, prop_id) == 4, "wire offset");
static_assert(offsetof(WireRecord, status) == 8, "wire offset");
static_assert(offsetof(WireRecord, int_value) == 12, "wire offset");
static_assert(offsetof(WireRecord, float_value) == 16, "wire offset");
static_assert(offsetof(WireRecord, reserved) == 20, "wire offset");
static_assert(offsetof(WireRecord, timestamp_ns) == 24, "wire offset");

/// @brief Named decode failures.
enum class WireError {
  kNone,            ///< Decoded.
  kBadSize,         ///< Datagram length is not kWireRecordSize.
  kUnknownVersion,  ///< version != kWireVersion; record must be dropped.
};

/**
 * @brief Serializes a record.
 * @param record Record to encode (fields are copied as-is).
 * @param out Buffer of at least kWireRecordSize bytes.
 */
inline void encode(const WireRecord& record, uint8_t* out) {
  std::memcpy(out, &record, kWireRecordSize);
}

/**
 * @brief Parses one datagram.
 * @param in Received bytes.
 * @param len Number of received bytes.
 * @param out Filled only when the result is WireError::kNone.
 * @return kNone, kBadSize or kUnknownVersion.
 */
inline WireError decode(const uint8_t* in, size_t len, WireRecord* out) {
  if (len != kWireRecordSize) return WireError::kBadSize;
  WireRecord r;
  std::memcpy(&r, in, kWireRecordSize);
  if (r.version != kWireVersion) return WireError::kUnknownVersion;
  *out = r;
  return WireError::kNone;
}

}  // namespace vipee

#endif  // VIPEE_WIRE_H_
