//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2018 The Tenzir Contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "tenzir/configuration.hpp"
#include "tenzir/data.hpp"
#include "tenzir/logger.hpp"
#include "tenzir/plugin.hpp"
#include "tenzir/test/data.hpp"
#include "tenzir/test/test.hpp"

#include <caf/message_builder.hpp>
#include <caf/test/unit_test.hpp>

#include <iostream>
#include <set>
#include <string>

namespace artifacts::logs::zeek {

const char* conn = TENZIR_TEST_PATH "artifacts/logs/zeek/conn.log";
const char* dns = TENZIR_TEST_PATH "artifacts/logs/zeek/dns.log";
const char* ftp = TENZIR_TEST_PATH "artifacts/logs/zeek/ftp.log";
const char* http = TENZIR_TEST_PATH "artifacts/logs/zeek/http.log";
const char* small_conn = TENZIR_TEST_PATH "artifacts/logs/zeek/small_conn.log";
const char* smtp = TENZIR_TEST_PATH "artifacts/logs/zeek/smtp.log";
const char* ssl = TENZIR_TEST_PATH "artifacts/logs/zeek/ssl.log";

} // namespace artifacts::logs::zeek

namespace artifacts::logs::suricata {

const char* alert = TENZIR_TEST_PATH "artifacts/logs/suricata/alert.json";
const char* dns = TENZIR_TEST_PATH "artifacts/logs/suricata/dns.json";
const char* fileinfo = TENZIR_TEST_PATH "artifacts/logs/suricata/fileinfo.json";
const char* flow = TENZIR_TEST_PATH "artifacts/logs/suricata/flow.json";
const char* http = TENZIR_TEST_PATH "artifacts/logs/suricata/http.json";
const char* netflow = TENZIR_TEST_PATH "artifacts/logs/suricata/netflow.json";
const char* stats = TENZIR_TEST_PATH "artifacts/logs/suricata//stats.json";

} // namespace artifacts::logs::suricata

namespace artifacts::logs::syslog {

const char* syslog_msgs
  = TENZIR_TEST_PATH "artifacts/logs/syslog/syslog-test.txt";

} // namespace artifacts::logs::syslog

namespace artifacts::schemas {

const char* base = TENZIR_TEST_PATH "artifacts/schemas/base.schema";
const char* suricata = TENZIR_TEST_PATH "artifacts/schemas/suricata.schema";

} // namespace artifacts::schemas

namespace artifacts::traces {

const char* nmap_vsn = TENZIR_TEST_PATH "artifacts/traces/nmap_vsn.pcap";
const char* workshop_2011_browse
  = TENZIR_TEST_PATH "artifacts/traces/workshop_2011_browse.pcap";

} // namespace artifacts::traces

namespace caf::test {

int main(int, char**);

} // namespace caf::test

namespace tenzir::test {

extern std::set<std::string> config;

} // namespace tenzir::test

namespace {

// Retrieves arguments after the '--' delimiter.
std::vector<std::string> get_test_args(int argc, const char* const* argv) {
  // Parse everything after after '--'.
  constexpr std::string_view delimiter = "--";
  auto start = argv + 1;
  auto end = argv + argc;
  auto args_start = std::find(start, end, delimiter);
  if (args_start == end)
    return {};
  return {args_start + 1, end};
}

} // namespace

int main(int argc, char** argv) {
  std::string tenzir_loglevel = "quiet";
  auto test_args = get_test_args(argc, argv);
  if (!test_args.empty()) {
    auto options = caf::config_option_set{}
                     .add(tenzir_loglevel, "tenzir-verbosity",
                          "console verbosity for libtenzir")
                     .add<bool>("help", "print this help text");
    caf::settings cfg;
    auto res = options.parse(cfg, test_args);
    if (res.first != caf::pec::success) {
      std::cout << "error while parsing argument \"" << *res.second
                << "\": " << to_string(res.first) << "\n\n";
      std::cout << options.help_text() << std::endl;
      return 1;
    }
    if (caf::get_or(cfg, "help", false)) {
      std::cout << options.help_text() << std::endl;
      return 0;
    }
    tenzir::test::config = {
      std::make_move_iterator(std::begin(test_args)),
      std::make_move_iterator(std::end(test_args)),
    };
  }
  // TODO: Only initialize built-in endpoints here by default,
  // and allow the unit tests to specify a list of required
  // plugins and their config.
  for (auto& plugin : tenzir::plugins::get_mutable()) {
    if (auto err = plugin->initialize({}, {})) {
      fmt::print(stderr, "failed to initialize plugin {}: {}", plugin->name(),
                 err);
      return EXIT_FAILURE;
    }
  }

  // Make sure to deinitialize all plugins at the end.
  auto plugin_guard = caf::detail::make_scope_guard([]() noexcept {
    // Ideally, we would not have this deinitialize function at all and could
    // just call `plugins::get_mutable().clear()`, but that has a race condition
    // in that some detached actors may still be alive that are owned by
    // plugins, which then often dereference a nullptr through the global actor
    // system config.
    for (auto& plugin : tenzir::plugins::get_mutable()) {
      plugin->deinitialize();
    }
  });
  caf::settings log_settings;
  put(log_settings, "tenzir.console-verbosity", tenzir_loglevel);
  put(log_settings, "tenzir.console-format", "%^[%s:%#] %v%$");
  bool is_server = false;
  auto log_context
    = tenzir::create_log_context(is_server, tenzir::invocation{}, log_settings);
  // Initialize factories.
  [[maybe_unused]] auto config = tenzir::configuration{};
  // Run the unit tests.
  auto result = caf::test::main(argc, argv);
  return result;
}
