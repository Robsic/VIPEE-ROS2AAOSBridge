/**
 * @file dds_peer.h
 * @brief Decision on the DDS host address of the board build (spec RPI-05,
 * RPI-06). Pure: no ROS, no Android; the caller reads the property and the
 * CycloneDDS XML and applies the outcome.
 */
#ifndef VIPEE_DDS_PEER_H_
#define VIPEE_DDS_PEER_H_

#include <optional>
#include <string>
#include <string_view>

namespace vipee {

/// @brief Named failure of the host address decision.
enum class DdsPeerError {
  kNone,     ///< No failure.
  kUnset,    ///< DDS_PEER_UNSET: XML uses ${VIPEE_HOST}, property is empty.
  kInvalid,  ///< DDS_PEER_INVALID: value is not a host name or IPv4.
};

/// @brief Outcome of DecideDdsPeer.
struct DdsPeerDecision {
  bool start = false;  ///< True when DDS may start.
  /// Value to export as VIPEE_HOST before starting; nullopt leaves the
  /// environment untouched.
  std::optional<std::string> vipee_host;
  DdsPeerError error = DdsPeerError::kNone;  ///< kNone iff start is true.
};

/**
 * @brief Decides whether DDS starts and with which VIPEE_HOST.
 * @param xml_uses_host_var True when the CycloneDDS XML in use contains the
 * literal `${VIPEE_HOST}`.
 * @param host Value of persist.vendor.vipee.host (empty when unset).
 * @return Without the variable in the XML: start, environment untouched
 * (host is ignored). With it: start and export VIPEE_HOST=host when host is a
 * non-empty host name or IPv4 (letters, digits, '.', '-'); kUnset when empty;
 * kInvalid for any other character (blank, ':', ...). Never starts on error.
 */
DdsPeerDecision DecideDdsPeer(bool xml_uses_host_var, std::string_view host);

/**
 * @brief Stable log code of an error.
 * @param error Error to name.
 * @return "DDS_PEER_UNSET", "DDS_PEER_INVALID" or "NONE".
 */
const char* DdsPeerErrorName(DdsPeerError error);

}  // namespace vipee

#endif  // VIPEE_DDS_PEER_H_
