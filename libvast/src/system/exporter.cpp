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

#include "vast/system/exporter.hpp"

#include "vast/fwd.hpp"

#include "vast/concept/printable/std/chrono.hpp"
#include "vast/concept/printable/to_string.hpp"
#include "vast/concept/printable/vast/bitmap.hpp"
#include "vast/concept/printable/vast/expression.hpp"
#include "vast/concept/printable/vast/uuid.hpp"
#include "vast/detail/assert.hpp"
#include "vast/detail/fill_status_map.hpp"
#include "vast/detail/narrow.hpp"
#include "vast/error.hpp"
#include "vast/expression_visitors.hpp"
#include "vast/logger.hpp"
#include "vast/system/query_status.hpp"
#include "vast/system/report.hpp"
#include "vast/system/status_verbosity.hpp"
#include "vast/table_slice.hpp"

#include <caf/settings.hpp>
#include <caf/stream_slot.hpp>
#include <caf/typed_event_based_actor.hpp>

namespace vast::system {

namespace {

void ship_results(exporter_actor::stateful_pointer<exporter_state> self) {
  VAST_TRACE("");
  auto& st = self->state;
  VAST_DEBUG(self, "relays", st.query.cached, "events");
  while (st.query.requested > 0 && st.query.cached > 0) {
    VAST_ASSERT(!st.results.empty());
    // Fetch the next table slice. Either we grab the entire first slice in
    // st.results or we need to split it up.
    table_slice slice = {};
    if (st.results[0].rows() <= st.query.requested) {
      slice = std::move(st.results[0]);
      st.results.erase(st.results.begin());
    } else {
      auto [first, second] = split(st.results[0], st.query.requested);
      VAST_ASSERT(first.encoding() != table_slice_encoding::none);
      VAST_ASSERT(second.encoding() != table_slice_encoding::none);
      VAST_ASSERT(first.rows() == st.query.requested);
      slice = std::move(first);
      st.results[0] = std::move(second);
    }
    // Ship the slice and update state.
    auto rows = slice.rows();
    VAST_ASSERT(rows <= st.query.cached);
    st.query.cached -= rows;
    st.query.requested -= rows;
    st.query.shipped += rows;
    self->anon_send(st.sink, std::move(slice));
  }
}

void report_statistics(exporter_actor::stateful_pointer<exporter_state> self) {
  auto& st = self->state;
  if (st.statistics_subscriber)
    self->anon_send(st.statistics_subscriber, st.name, st.query);
  if (st.accountant) {
    auto hits = rank(st.hits);
    auto processed = st.query.processed;
    auto shipped = st.query.shipped;
    auto results = shipped + st.results.size();
    auto selectivity = double(results) / processed;
    auto msg = report{{"exporter.hits", hits},
                      {"exporter.processed", processed},
                      {"exporter.results", results},
                      {"exporter.shipped", shipped},
                      {"exporter.selectivity", selectivity},
                      {"exporter.runtime", st.query.runtime}};
    self->send(st.accountant, msg);
  }
}

void shutdown(exporter_actor::stateful_pointer<exporter_state> self,
              caf::error err) {
  VAST_DEBUG(self, "initiates shutdown with error", render(err));
  self->send_exit(self, std::move(err));
}

void shutdown(exporter_actor::stateful_pointer<exporter_state> self) {
  if (has_continuous_option(self->state.options))
    return;
  VAST_DEBUG(self, "initiates shutdown");
  self->send_exit(self, caf::exit_reason::normal);
}

void request_more_hits(exporter_actor::stateful_pointer<exporter_state> self) {
  auto& st = self->state;
  // Sanity check.
  if (!has_historical_option(st.options)) {
    VAST_WARNING(self, "requested more hits for continuous query");
    return;
  }
  // Do nothing if we already shipped everything the client asked for.
  if (st.query.requested == 0) {
    VAST_DEBUG(self, "shipped", self->state.query.shipped,
               "results and waits for client to request more");
    return;
  }
  // Do nothing if we are still waiting for results from the ARCHIVE.
  if (st.query.lookups_issued > st.query.lookups_complete) {
    VAST_DEBUG(self, "currently awaits",
               st.query.lookups_issued - st.query.lookups_complete,
               "more lookup results from the archive");
    return;
  }
  // If the if-statement above isn't true then the two values must be equal.
  // Otherwise, we would complete more than we issue.
  VAST_ASSERT(st.query.lookups_issued == st.query.lookups_complete);
  // Do nothing if we received everything.
  if (st.query.received == st.query.expected) {
    VAST_DEBUG(self, "received hits for all", st.query.expected, "partitions");
    return;
  }
  // If the if-statement above isn't true then `received < expected` must hold.
  // Otherwise, we would receive results for more partitions than qualified as
  // hits by the INDEX.
  VAST_ASSERT(st.query.received < st.query.expected);
  auto remaining = st.query.expected - st.query.received;
  // TODO: Figure out right number of partitions to ask for. For now, we
  // bound the number by an arbitrary constant.
  auto n = std::min(remaining, size_t{2});
  // Store how many partitions we schedule with our request. When receiving
  // 'done', we add this number to `received`.
  st.query.scheduled = n;
  // Request more hits from the INDEX.
  VAST_DEBUG(self, "asks index to process", n, "more partitions");
  self->send(st.index, st.id, detail::narrow<uint32_t>(n));
}

void handle_batch(exporter_actor::stateful_pointer<exporter_state> self,
                  table_slice slice) {
  VAST_ASSERT(slice.encoding() != table_slice_encoding::none);
  VAST_DEBUG(self, "got batch of", slice.rows(), "events");
  // Construct a candidate checker if we don't have one for this type.
  type t = slice.layout();
  auto it = self->state.checkers.find(t);
  if (it == self->state.checkers.end()) {
    auto x = tailor(self->state.expr, t);
    if (!x) {
      VAST_ERROR(self, "failed to tailor expression:", render(x.error()));
      ship_results(self);
      shutdown(self);
      return;
    }
    VAST_DEBUG(self, "tailored AST to", t, ':', x);
    std::tie(it, std::ignore)
      = self->state.checkers.emplace(type{slice.layout()}, std::move(*x));
  }
  auto& checker = it->second;
  // Perform candidate check, splitting the slice into subsets if needed.
  self->state.query.processed += slice.rows();
  auto selection = evaluate(checker, slice);
  auto selection_size = rank(selection);
  if (selection_size == 0) {
    // No rows qualify.
    return;
  }
  self->state.query.cached += selection_size;
  select(self->state.results, slice, selection);
  // Ship slices to connected SINKs.
  ship_results(self);
}

} // namespace

exporter_actor::behavior_type
exporter(exporter_actor::stateful_pointer<exporter_state> self, expression expr,
         query_options options) {
  self->state.options = options;
  self->state.expr = std::move(expr);
  if (has_continuous_option(options))
    VAST_DEBUG(self, "has continuous query option");
  self->set_exit_handler([=](const caf::exit_msg& msg) {
    VAST_DEBUG(self, "received exit from", msg.source,
               "with reason:", msg.reason);
    auto& st = self->state;
    if (msg.reason != caf::exit_reason::kill)
      report_statistics(self);
    // Sending 0 to the index means dropping further results.
    self->send<caf::message_priority::high>(st.index, st.id,
                                            static_cast<uint32_t>(0));
    self->quit(msg.reason);
  });
  self->set_down_handler([=](const caf::down_msg& msg) {
    VAST_DEBUG(self, "received DOWN from", msg.source);
    if (has_continuous_option(self->state.options)
        && (msg.source == self->state.archive
            || msg.source == self->state.index))
      report_statistics(self);
    // Without sinks and resumable sessions, there's no reason to proceed.
    self->quit(msg.reason);
  });
  auto finished = [](const query_status& qs) -> bool {
    return qs.received == qs.expected
           && qs.lookups_issued == qs.lookups_complete;
  };
  return {
    [=](atom::extract) -> caf::result<void> {
      auto& qs = self->state.query;
      // Sanity check.
      VAST_DEBUG(self, "got request to extract all events");
      if (qs.requested == max_events) {
        VAST_WARNING(self, "ignores extract request, already getting all");
        return {};
      }
      // Configure state to get all remaining partition results.
      qs.requested = max_events;
      ship_results(self);
      request_more_hits(self);
      return {};
    },
    [=](atom::extract, uint64_t requested_results) -> caf::result<void> {
      auto& qs = self->state.query;
      // Sanity checks.
      if (requested_results == 0) {
        VAST_WARNING(self, "ignores extract request for 0 results");
        return {};
      }
      if (qs.requested == max_events) {
        VAST_WARNING(self, "ignores extract request, already getting all");
        return {};
      }
      VAST_ASSERT(qs.requested < max_events);
      // Configure state to get up to `requested_results` more events.
      auto n = std::min(max_events - requested_results, requested_results);
      VAST_DEBUG(self, "got a request to extract", n,
                 "more results in addition to", qs.requested,
                 "pending results");
      qs.requested += n;
      ship_results(self);
      request_more_hits(self);
      return {};
    },
    [=](accountant_actor accountant) {
      self->state.accountant = std::move(accountant);
      self->send(self->state.accountant, atom::announce_v, self->name());
    },
    [=](archive_actor archive) {
      VAST_DEBUG(self, "registers archive", archive);
      self->state.archive = std::move(archive);
      if (has_continuous_option(self->state.options))
        self->monitor(self->state.archive);
      // Register self at the archive
      if (has_historical_option(self->state.options))
        self->send(self->state.archive, atom::exporter_v,
                   caf::actor_cast<caf::actor>(self));
    },
    [=](index_actor index) {
      VAST_DEBUG(self, "registers index", index);
      self->state.index = std::move(index);
      if (has_continuous_option(self->state.options))
        self->monitor(self->state.index);
    },
    [=](atom::sink, const caf::actor& sink) {
      VAST_DEBUG(self, "registers sink", sink);
      self->state.sink = sink;
      self->monitor(self->state.sink);
    },
    [=](atom::importer, const std::vector<caf::actor>& importers) {
      // Register for events at running IMPORTERs.
      if (has_continuous_option(self->state.options))
        for (auto& x : importers)
          self->anon_send(x, atom::exporter_v,
                          caf::actor_cast<caf::actor>(self));
    },
    [=](atom::run) {
      VAST_VERBOSE(self, "executes query:", to_string(self->state.expr));
      self->state.start = std::chrono::system_clock::now();
      if (!has_historical_option(self->state.options))
        return;
      // TODO: The index replies to expressions by manually sending back to the
      // sender, which does not work with request(...).then(...) style of
      // communication for typed actors. Hence, we must actor_cast here.
      // Ideally, we would change that index handler to actually return the
      // desired value.
      self
        ->request(caf::actor_cast<caf::actor>(self->state.index), caf::infinite,
                  self->state.expr)
        .then(
          [=](const uuid& lookup, uint32_t partitions, uint32_t scheduled) {
            VAST_VERBOSE(self, "got lookup handle", lookup, ", scheduled",
                         scheduled, '/', partitions, "partitions");
            self->state.id = lookup;
            if (partitions > 0) {
              self->state.query.expected = partitions;
              self->state.query.scheduled = scheduled;
            } else {
              shutdown(self);
            }
          },
          [=](const caf::error& e) { shutdown(self, e); });
    },
    [=](atom::statistics, const caf::actor& statistics_subscriber) {
      VAST_DEBUG(self, "registers statistics subscriber",
                 statistics_subscriber);
      self->state.statistics_subscriber = statistics_subscriber;
    },
    [=](caf::stream<table_slice> in) -> caf::inbound_stream_slot<table_slice> {
      return self
        ->make_sink(
          in,
          [](caf::unit_t&) {
            // nop
          },
          [=](caf::unit_t&, table_slice slice) {
            handle_batch(self, std::move(slice));
          },
          [=](caf::unit_t&, const caf::error& err) {
            if (err)
              VAST_ERROR(self, "got error during streaming:", err);
          })
        .inbound_slot();
    },
    // -- status_client_actor --------------------------------------------------
    [=](atom::status, status_verbosity v) {
      auto& st = self->state;
      auto result = caf::settings{};
      auto& exporter_status = put_dictionary(result, "exporter");
      if (v >= status_verbosity::info) {
        caf::settings exp;
        put(exp, "expression", to_string(st.expr));
        auto& xs = put_list(result, "queries");
        xs.emplace_back(std::move(exp));
      }
      if (v >= status_verbosity::detailed) {
        caf::settings exp;
        put(exp, "expression", to_string(st.expr));
        put(exp, "hits", rank(st.hits));
        put(exp, "start", caf::deep_to_string(st.start));
        auto& xs = put_list(result, "queries");
        xs.emplace_back(std::move(exp));
        detail::fill_status_map(exporter_status, self);
      }
      return result;
    },
    // -- archive_client_actor -------------------------------------------------
    [=](table_slice slice) { //
      handle_batch(self, std::move(slice));
    },
    [=](atom::done, const caf::error& err) {
      VAST_ASSERT(self->current_sender() == self->state.archive);
      auto& qs = self->state.query;
      ++qs.lookups_complete;
      VAST_DEBUG(self, "received done from archive:", VAST_ARG(err),
                 VAST_ARG("query", qs));
      // We skip 'done' messages of the query supervisors until we process all
      // hits first. Hence, we can never be finished here.
      VAST_ASSERT(!finished(qs));
    },
    // -- index_client_actor ---------------------------------------------------
    // The INDEX (or the EVALUATOR, to be more precise) sends us a series of
    // `ids` in response to an expression (query), terminated by 'done'.
    [=](const ids& hits) -> caf::result<void> {
      auto& st = self->state;
      // Skip results that arrive before we got our lookup handle from the
      // INDEX actor.
      if (st.query.expected == 0)
        return caf::skip;
      // Add `hits` to the total result set and update all stats.
      caf::timespan runtime = std::chrono::system_clock::now() - st.start;
      st.query.runtime = runtime;
      auto count = rank(hits);
      if (st.accountant) {
        auto r = report{};
        if (st.hits.empty())
          r.push_back({"exporter.hits.first", runtime});
        r.push_back({"exporter.hits.arrived", runtime});
        r.push_back({"exporter.hits.count", count});
        self->send(st.accountant, r);
      }
      if (count == 0) {
        VAST_WARNING(self, "got empty hits");
      } else {
        VAST_ASSERT(rank(st.hits & hits) == 0);
        VAST_DEBUG(self, "got", count, "index hits in [", (select(hits, 1)),
                   ',', (select(hits, -1) + 1), ')');
        st.hits |= hits;
        VAST_DEBUG(self, "forwards hits to archive");
        // FIXME: restrict according to configured limit.
        ++st.query.lookups_issued;
        self->send(st.archive, std::move(hits));
      }
      return {};
    },
    [=](atom::done) -> caf::result<void> {
      auto& qs = self->state.query;
      // Ignore this message until we got all lookup results from the ARCHIVE.
      // Otherwise, we can end up in weirdly interleaved state.
      if (qs.lookups_issued != qs.lookups_complete)
        return caf::skip;
      // Figure out if we're done by bumping the counter for `received` and
      // check whether it reaches `expected`.
      caf::timespan runtime
        = std::chrono::system_clock::now() - self->state.start;
      qs.runtime = runtime;
      qs.received += qs.scheduled;
      if (qs.received < qs.expected) {
        VAST_DEBUG(self, "received hits from", qs.received, '/', qs.expected,
                   "partitions");
        request_more_hits(self);
      } else {
        VAST_DEBUG(self, "received all hits from", qs.expected,
                   "partition(s) in", vast::to_string(runtime));
        if (self->state.accountant)
          self->send(self->state.accountant, "exporter.hits.runtime", runtime);
        if (finished(qs))
          shutdown(self);
      }
      return {};
    },
  };
}

} // namespace vast::system
