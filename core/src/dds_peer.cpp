#include "vipee/dds_peer.h"

#include <string>

namespace vipee {
namespace {

// Host name / IPv4 alphabet: ASCII letters, digits, '.' and '-'.
bool IsHostChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '.' || c == '-';
}

}  // namespace

DdsPeerDecision DecideDdsPeer(bool xml_uses_host_var, std::string_view host) {
  DdsPeerDecision d;
  if (!xml_uses_host_var) {
    d.start = true;
    return d;
  }
  if (host.empty()) {
    d.error = DdsPeerError::kUnset;
    return d;
  }
  for (char c : host) {
    if (!IsHostChar(c)) {
      d.error = DdsPeerError::kInvalid;
      return d;
    }
  }
  d.start = true;
  d.vipee_host = std::string(host);
  return d;
}

const char* DdsPeerErrorName(DdsPeerError error) {
  switch (error) {
    case DdsPeerError::kUnset:
      return "DDS_PEER_UNSET";
    case DdsPeerError::kInvalid:
      return "DDS_PEER_INVALID";
    case DdsPeerError::kNone:
      break;
  }
  return "NONE";
}

}  // namespace vipee
