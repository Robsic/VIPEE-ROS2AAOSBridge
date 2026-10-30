#include "vipee/bridge.h"

namespace vipee {

Bridge::Bridge(const VehicleMap& map, PropertySink& sink, Logger& logger,
               int64_t now_ns)
    : map_(map),
      sink_(sink),
      logger_(logger),
      valid_(validateMap(map) == MapError::kNone),
      derived_selection_(map.gear_selection_source == "derived") {
  const Reason reason = valid_ ? Reason::kNoData : Reason::kMapInvalid;
  for (Prop* p : {&speed_, &selection_, &current_gear_, &brake_}) {
    p->reason = reason;
    logger_.transition(p->id, p->status, p->reason);
    sink_.publish({p->id, p->status, 0, 0.0f, now_ns});
  }
}

void Bridge::onTelemetry(const TelemetryIn& in, int64_t now_ns) {
  if (!valid_) return;
  telemetry_rx_ns_ = now_ns;

  const SpeedResult speed = speedKmhToMps(in.speed_kmh, map_);
  if (speed.reason == Reason::kNone) {
    set(speed_, Status::kAvailable, Reason::kNone, 0, speed.mps, now_ns);
  } else {
    set(speed_, Status::kError, speed.reason, 0, 0.0f, now_ns);
  }

  setGear(current_gear_, currentGearFromEngaged(in.gear), now_ns);
  if (derived_selection_) {
    setGear(selection_, derivedSelectionFromEngaged(in.gear), now_ns);
  }
}

void Bridge::onLever(const LeverIn& in, int64_t now_ns) {
  if (!valid_ || derived_selection_) return;
  lever_rx_ns_ = now_ns;
  setGear(selection_, selectionFromLever(in.transmission, map_), now_ns);
}

void Bridge::tick(int64_t now_ns) {
  if (!valid_) return;
  if (isStale(telemetry_rx_ns_, now_ns)) {
    setUnavailable(speed_, Reason::kStale, now_ns);
    setUnavailable(current_gear_, Reason::kStale, now_ns);
    if (derived_selection_) setUnavailable(selection_, Reason::kStale, now_ns);
  }
  if (isStale(lever_rx_ns_, now_ns)) {
    setUnavailable(selection_, Reason::kStale, now_ns);
  }
}

bool Bridge::isStale(int64_t rx_ns, int64_t now_ns) const {
  return rx_ns >= 0 && now_ns - rx_ns > map_.stale_ms * 1000000;
}

void Bridge::setGear(Prop& p, const GearResult& g, int64_t now_ns) {
  if (g.reason == Reason::kNone) {
    set(p, Status::kAvailable, Reason::kNone, g.gear, 0.0f, now_ns);
  } else {
    setUnavailable(p, g.reason, now_ns);
  }
}

void Bridge::setUnavailable(Prop& p, Reason reason, int64_t now_ns) {
  set(p, Status::kUnavailable, reason, 0, 0.0f, now_ns);
}

void Bridge::set(Prop& p, Status status, Reason reason, int32_t int_value,
                 float float_value, int64_t now_ns) {
  const bool status_changed = p.status != status;
  const bool value_changed =
      p.int_value != int_value || p.float_value != float_value;
  if (status_changed || p.reason != reason) {
    logger_.transition(p.id, status, reason);
  }
  p.status = status;
  p.reason = reason;
  p.int_value = int_value;
  p.float_value = float_value;
  const bool available = status == Status::kAvailable;
  if (status_changed || (available && (value_changed || p.continuous))) {
    sink_.publish({p.id, status, int_value, float_value, now_ns});
  }
}

}  // namespace vipee
