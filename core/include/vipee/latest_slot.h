/**
 * @file latest_slot.h
 * @brief One-position mailbox: the producer replaces, the consumer takes the
 * newest value (spec CAM-23, CAM-24).
 *
 * The ROS subscription puts each message and the camera thread takes it, so
 * a slow decode drops older frames instead of queueing them, and frames
 * nobody takes never accumulate. Thread-safe.
 */
#ifndef VIPEE_LATEST_SLOT_H_
#define VIPEE_LATEST_SLOT_H_

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

namespace vipee {

/**
 * @brief Holds at most one T; put() replaces it, take() removes it.
 * @tparam T Movable value, e.g. a shared_ptr to a message.
 */
template <typename T>
class LatestSlot {
 public:
  /**
   * @brief Stores value, dropping (destroying) any value not yet taken, and
   * wakes a waiting take(). Ignored after close().
   * @param value New value.
   */
  void put(T value) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) return;
      value_ = std::move(value);
    }
    cv_.notify_one();
  }

  /**
   * @brief Removes and returns the stored value, waiting up to timeout for
   * one.
   * @param timeout Longest wait.
   * @return The newest value, or nullopt on timeout or after close().
   */
  std::optional<T> take(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, timeout, [this] { return closed_ || value_; });
    if (closed_) return std::nullopt;
    std::optional<T> out = std::move(value_);
    value_.reset();
    return out;
  }

  /// @brief Wakes every take() for shutdown; later calls return nullopt.
  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
      value_.reset();
    }
    cv_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::optional<T> value_;
  bool closed_ = false;
};

}  // namespace vipee

#endif  // VIPEE_LATEST_SLOT_H_
