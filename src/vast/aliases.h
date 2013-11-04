#ifndef VAST_ALIASES_H
#define VAST_ALIASES_H

#include <cstdint>
#include <limits>

namespace vast {

/// Uniquely identifies a VAST event.
using event_id = uint64_t;

/// The smallest possible event ID.
static constexpr event_id min_event_id = 1;

/// The largest possible event ID.
static constexpr event_id max_event_id =
  std::numeric_limits<event_id>::max() - 1;

/// Uniquely identifies a VAST type.
using type_id = uint64_t;

} // namespace vast

#endif
