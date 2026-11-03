/**
 * @file vhal_ingest.h
 * @brief VHAL-side handling of socket records: validation, watchdog,
 *        timestamp floor (AD-013).
 *
 * Speed is not decimated here: every sample becomes an event, which meets the
 * IVehicleHardware rule (VUR off: at least the subscribed rate) and lets the
 * client-side ContCarPropertyEventTracker pick events >= one period apart.
 * Decimating here on arrival stamps drops the app below the rate.
 *
 * Header-only, stdlib only, so it is unit-tested on the host. Not thread-safe:
 * the caller serializes access. All times are CLOCK_BOOTTIME in ns (the base
 * of elapsedRealtimeNano()).
 */
#ifndef VIPEE_VHAL_INGEST_H_
#define VIPEE_VHAL_INGEST_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "vipee/wire.h"

namespace vipee {

/// @brief VehicleProperty ids served by this VHAL (ct-speed-slice).
constexpr int32_t kPropPerfVehicleSpeed = 0x11600207;
constexpr int32_t kPropGearSelection = 0x11400400;
constexpr int32_t kPropCurrentGear = 0x11400401;
constexpr int32_t kPropParkingBrakeOn = 0x11200402;

/// @brief Properties fed by the bridge socket; the watchdog and a lost peer
///        mark exactly these UNAVAILABLE.
constexpr std::array<int32_t, 4> kBridgeProps = {
    kPropPerfVehicleSpeed, kPropGearSelection, kPropCurrentGear,
    kPropParkingBrakeOn};

/// @brief CalibDroid session trigger (CalibDroid AD-010; AD-018): vendor
///        GLOBAL INT32 0x21402a11, READ_WRITE, ON_CHANGE, initial 0.
constexpr int32_t kPropCalibTrigger = 0x21402a11;

/**
 * @brief Whether a property is local to the VHAL (AD-018): no ROS source,
 *        AVAILABLE from boot, written by setValues, outside the watchdog.
 * @param prop_id VehicleProperty id.
 * @return true only for kPropCalibTrigger.
 */
constexpr bool isLocalProp(int32_t prop_id) {
  return prop_id == kPropCalibTrigger;
}

/// @brief Silence after which the peer is treated as dead (design: 300 ms).
constexpr int64_t kWatchdogNs = 300LL * 1000 * 1000;

/// @brief What the caller does with one datagram.
enum class IngestAction {
  kDrop,   ///< Invalid; log `drop` and ignore. Does not feed the watchdog.
  kAlive,  ///< Heartbeat; nothing to write.
  kWrite,  ///< Write `record` to the property store.
};

/// @brief Named reason for IngestAction::kDrop.
enum class DropReason {
  kNone,
  kBadSize,         ///< Datagram is not kWireRecordSize bytes.
  kUnknownVersion,  ///< version != kWireVersion.
  kUnknownKind,     ///< kind is neither update nor heartbeat.
  kUnknownProp,     ///< prop_id is not one of the four served properties.
  kBadStatus,       ///< status is not AVAILABLE 0, UNAVAILABLE 1 or ERROR 2.
};

/// @brief Result of VhalIngest::onDatagram.
struct IngestResult {
  IngestAction action = IngestAction::kDrop;
  DropReason drop = DropReason::kNone;
  WireRecord record{};  ///< For kWrite; timestamp already raised to the floor.
};

/**
 * @brief Per-connection record handling of the VHAL.
 *
 * The VHAL calls onConnect/onDisconnect around each peer, onDatagram per
 * received datagram and expire() periodically. When onDisconnect or expire()
 * returns true the caller writes the four properties as UNAVAILABLE stamped
 * floorNs() and, for expire(), closes the peer so it reconnects and resyncs.
 */
class VhalIngest {
 public:
  /**
   * @brief A peer connected.
   * @param now_ns Current CLOCK_BOOTTIME ns.
   */
  void onConnect(int64_t now_ns) {
    alive_ = true;
    last_rx_ns_ = now_ns;
  }

  /**
   * @brief The peer closed or failed.
   * @param now_ns Current CLOCK_BOOTTIME ns.
   * @return true if the peer was alive: mark the four UNAVAILABLE.
   */
  bool onDisconnect(int64_t now_ns) { return markDead(now_ns); }

  /**
   * @brief Watchdog check.
   * @param now_ns Current CLOCK_BOOTTIME ns.
   * @return true once when no valid record arrived for more than kWatchdogNs.
   */
  bool expire(int64_t now_ns) {
    return alive_ && now_ns - last_rx_ns_ > kWatchdogNs && markDead(now_ns);
  }

  /// @return Whether a peer is connected and not expired.
  bool alive() const { return alive_; }

  /// @return Subscribed PERF_VEHICLE_SPEED rate in Hz, 0 if none.
  float speedRateHz() const { return speed_rate_hz_; }

  /// @return Timestamp of the last UNAVAILABLE marker (ns), 0 if none.
  int64_t floorNs() const { return floor_ns_; }

  /**
   * @brief Records the subscribed rate of PERF_VEHICLE_SPEED (for the dump).
   * @param hz Events per second the subscriber asked for; 0 = none.
   */
  void setSpeedRateHz(float hz) { speed_rate_hz_ = hz; }

  /**
   * @brief Validates and classifies one datagram.
   * @param data Received bytes.
   * @param len Number of bytes received.
   * @param now_ns Current CLOCK_BOOTTIME ns.
   * @return Action for the caller; see IngestResult.
   */
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  IngestResult onDatagram(const uint8_t* data, size_t len, int64_t now_ns) {
    IngestResult res;
    WireRecord r;
    switch (decode(data, len, &r)) {
      case WireError::kBadSize:
        res.drop = DropReason::kBadSize;
        return res;
      case WireError::kUnknownVersion:
        res.drop = DropReason::kUnknownVersion;
        return res;
      case WireError::kNone:
        break;
    }
    if (r.kind == static_cast<uint16_t>(WireKind::kHeartbeat)) {
      last_rx_ns_ = now_ns;
      res.action = IngestAction::kAlive;
      return res;
    }
    if (r.kind != static_cast<uint16_t>(WireKind::kUpdate)) {
      res.drop = DropReason::kUnknownKind;
      return res;
    }
    if (std::find(kBridgeProps.begin(), kBridgeProps.end(), r.prop_id) ==
        kBridgeProps.end()) {
      res.drop = DropReason::kUnknownProp;
      return res;
    }
    if (r.status < 0 || r.status > 2) {
      res.drop = DropReason::kBadStatus;
      return res;
    }
    last_rx_ns_ = now_ns;
    r.timestamp_ns = std::max(r.timestamp_ns, floor_ns_);
    res.action = IngestAction::kWrite;
    res.record = r;
    return res;
  }

 private:
  bool markDead(int64_t now_ns) {
    if (!alive_) return false;
    alive_ = false;
    floor_ns_ = now_ns;
    return true;
  }

  bool alive_ = false;
  int64_t last_rx_ns_ = 0;
  int64_t floor_ns_ = 0;
  float speed_rate_hz_ = 0;
};

/// @brief How getValues answers a stored value (AD-017).
enum class GetAnswer {
  kValue,          ///< StatusCode::OK with the stored value and its status.
  kInternalError,  ///< StatusCode::INTERNAL_ERROR, no value.
};

/**
 * @brief Maps a stored VehiclePropertyStatus to the get answer (AD-017).
 * @param status AVAILABLE 0, UNAVAILABLE 1 or ERROR 2.
 * @return kInternalError for ERROR; kValue otherwise, so a get without data
 *         is OK with status UNAVAILABLE (CarEvsService needs a non-null get).
 */
constexpr GetAnswer getAnswer(int32_t status) {
  return status == 2 ? GetAnswer::kInternalError : GetAnswer::kValue;
}

/**
 * @brief The record as it goes into the property store (AD-017).
 * @param r Record to store.
 * @return r with int_value and float_value zeroed unless status is AVAILABLE:
 *         an UNAVAILABLE or ERROR value, in a get or an event, never carries
 *         the last real value (e.g. an old GEAR_REVERSE).
 */
constexpr WireRecord storable(WireRecord r) {
  if (r.status != 0) {
    r.int_value = 0;
    r.float_value = 0.0f;
  }
  return r;
}

}  // namespace vipee

#endif  // VIPEE_VHAL_INGEST_H_
