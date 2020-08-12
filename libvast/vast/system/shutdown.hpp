/******************************************************************************
 *                    _   _____   __________                                  *
 *                   | | / / _ | / __/_  __/     Visibility                   *
 *                   | |/ / __ |_\ \  / /          Across                     *
 *                   |___/_/ |_/___/ /_/       Space and Time                 *
 *                                                                            *
 * This file is part of VAST. It is subject to the license terms in the       *
 * LICENSE file found in the top-level directory of this distribution and at  *
 * http://vast.io/license. No part of VAST, including this file, may be       *
 * copied, modified, propagated, or distributed except according to the terms *
 * contained in the LICENSE file.                                             *
 ******************************************************************************/

#pragma once

#include "vast/defaults.hpp"

#include <caf/actor_cast.hpp>
#include <caf/event_based_actor.hpp>
#include <caf/fwd.hpp>
#include <caf/stateful_actor.hpp>

#include <vector>

namespace vast::policy {

struct sequential;
struct parallel;

} // namespace vast::policy

namespace vast::system {

/// Performs an asynchronous shutdown of a set of actors, or terminates the
/// current process if that is not possible. The shutdown process runs
/// either sequentially or in parallel, based on the provided policy parameter.
/// This involves monitoring the actor, sending an EXIT message with reason
/// `user_shutdown`, and then waiting for the DOWN. As soon as all actors have
/// terminated, the calling actor exits with `caf::exit_reason::user_shutdown`.
/// If an actor does not respond with a DOWN within the provided grace period,
/// we send out another EXIT message with reason `kill`. If the actor still
/// does not terminate within the provided timeout, the process aborts hard. If
/// these failure semantics do not suit your use case, consider using the
/// function `terminate`, which allows for more detailed control over the
/// shutdown sequence.
/// @param self The actor to terminate.
/// @param xs Actors that need to shutdown before *self* quits.
/// @param grace_period The amount of time to wait until all actors terminated
///        cleanly.
/// @param kill_timeout The timeout befor giving and calling `abort(3)`.
/// @relates terminate
template <class Policy>
void shutdown(caf::event_based_actor* self, std::vector<caf::actor> xs,
              std::chrono::seconds grace_period
              = defaults::system::shutdown_grace_period,
              std::chrono::seconds kill_timeout
              = defaults::system::shutdown_kill_timeout);

template <class Policy, class... Ts>
void shutdown(caf::typed_event_based_actor<Ts...>* self,
              std::vector<caf::actor> xs,
              std::chrono::seconds grace_period
              = defaults::system::shutdown_grace_period,
              std::chrono::seconds kill_timeout
              = defaults::system::shutdown_kill_timeout) {
  auto handle = caf::actor_cast<caf::event_based_actor*>(self);
  shutdown<Policy>(handle, std::move(xs), grace_period, kill_timeout);
}

template <class Policy>
void shutdown(caf::scoped_actor& self, std::vector<caf::actor> xs,
              std::chrono::seconds grace_period
              = defaults::system::shutdown_grace_period,
              std::chrono::seconds kill_timeout
              = defaults::system::shutdown_kill_timeout);

template <class Policy, class Actor>
void shutdown(Actor&& self, caf::actor x,
              std::chrono::seconds grace_period
              = defaults::system::shutdown_grace_period,
              std::chrono::seconds kill_timeout
              = defaults::system::shutdown_kill_timeout) {
  shutdown<Policy>(std::forward<Actor>(self),
                   std::vector<caf::actor>{std::move(x)}, grace_period,
                   kill_timeout);
}

} // namespace vast::system
