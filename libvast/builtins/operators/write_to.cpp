//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2021 The VAST Contributors
// SPDX-License-Identifier: BSD-3-Clause

#include <vast/arrow_table_slice.hpp>
#include <vast/concept/convertible/data.hpp>
#include <vast/concept/convertible/to.hpp>
#include <vast/concept/parseable/vast/pipeline.hpp>
#include <vast/element_type.hpp>
#include <vast/error.hpp>
#include <vast/pipeline.hpp>
#include <vast/plugin.hpp>
#include <vast/type.hpp>

#include <arrow/type.h>
#include <caf/expected.hpp>

#include <algorithm>
#include <utility>

namespace vast::plugins::write_to {

namespace {

/// The operator for printing data that will have to be joined later
/// during pipeline execution.
class print_operator final
  : public schematic_operator<print_operator, printer_plugin::printer,
                              generator<chunk_ptr>> {
public:
  explicit print_operator(const printer_plugin& printer) noexcept
    : printer_plugin_{&printer} {
  }

  auto initialize(const type& schema, operator_control_plane& ctrl) const
    -> caf::expected<state_type> override {
    return printer_plugin_->make_printer({}, schema, ctrl);
  }

  auto process(table_slice slice, state_type& state) const
    -> output_type override {
    return state(std::move(slice));
  }

  [[nodiscard]] auto to_string() const noexcept -> std::string override {
    return fmt::format("write {}", printer_plugin_->name());
  }

private:
  const printer_plugin* printer_plugin_;
};

/// The operator for saving data that will have to be joined later
/// during pipeline execution.
class save_operator final : public crtp_operator<save_operator> {
public:
  explicit save_operator(const saver_plugin& saver) noexcept
    : saver_plugin_{&saver} {
  }

  auto
  operator()(generator<chunk_ptr> input, operator_control_plane& ctrl) const
    -> generator<std::monostate> {
    // TODO: Extend API to allow schema-less make_saver().
    auto new_saver = saver_plugin_->make_saver({}, {}, ctrl);
    if (!new_saver) {
      ctrl.abort(new_saver.error());
      co_return;
    }
    for (auto&& x : input) {
      (*new_saver)(std::move(x));
      co_yield {};
    }
  }

  [[nodiscard]] auto to_string() const -> std::string override {
    return fmt::format("to {}", saver_plugin_->name());
  }

private:
  const saver_plugin* saver_plugin_;
};

struct writing_state {
  printer_plugin::printer p;
  saver_plugin::saver s;
};

/// The operator for printing and saving data without joining.
class print_save_operator final
  : public schematic_operator<print_save_operator, writing_state,
                              std::monostate> {
public:
  explicit print_save_operator(const printer_plugin& printer,
                               const saver_plugin& saver) noexcept
    : printer_plugin_{&printer}, saver_plugin_{&saver} {
  }

  auto initialize(const type& schema, operator_control_plane& ctrl) const
    -> caf::expected<state_type> override {
    writing_state ws;
    auto new_printer = printer_plugin_->make_printer({}, schema, ctrl);
    if (!new_printer) {
      return new_printer.error();
    }
    ws.p = std::move(*new_printer);
    auto new_saver = saver_plugin_->make_saver({}, schema, ctrl);
    if (!new_saver) {
      return new_saver.error();
    }
    ws.s = std::move(*new_saver);
    return ws;
  }

  auto process(table_slice slice, state_type& state) const
    -> output_type override {
    for (auto&& x : state.p(std::move(slice))) {
      state.s(std::move(x));
    }
    return {};
  }

  [[nodiscard]] auto to_string() const noexcept -> std::string override {
    return fmt::format("write {} to {}", printer_plugin_->name(),
                       saver_plugin_->name());
  }

private:
  const printer_plugin* printer_plugin_{};
  const saver_plugin* saver_plugin_{};
};

class write_plugin final : public virtual operator_plugin {
public:
  auto initialize([[maybe_unused]] const record& plugin_config,
                  [[maybe_unused]] const record& global_config)
    -> caf::error override {
    return {};
  }

  [[nodiscard]] auto name() const -> std::string override {
    return "write";
  };

  [[nodiscard]] auto make_operator(std::string_view pipeline) const
    -> std::pair<std::string_view, caf::expected<operator_ptr>> override {
    using parsers::end_of_pipeline_operator, parsers::required_ws_or_comment,
      parsers::optional_ws_or_comment, parsers::extractor, parsers::plugin_name,
      parsers::extractor_char, parsers::extractor_list;
    const auto* f = pipeline.begin();
    const auto* const l = pipeline.end();
    const auto p = optional_ws_or_comment >> plugin_name
                   >> -(required_ws_or_comment >> string_parser{"to"}
                        >> required_ws_or_comment >> plugin_name)
                   >> optional_ws_or_comment >> end_of_pipeline_operator;
    auto result = std::tuple{
      std::string{}, std::optional<std::tuple<std::string, std::string>>{}};
    if (!p(f, l, result)) {
      return {
        std::string_view{f, l},
        caf::make_error(ec::syntax_error, fmt::format("failed to parse "
                                                      "write operator: '{}'",
                                                      pipeline)),
      };
    }
    auto printer_name = std::get<0>(result);
    auto printer = plugins::find<printer_plugin>(printer_name);
    if (!printer) {
      return {std::string_view{f, l},
              caf::make_error(ec::syntax_error,
                              fmt::format("failed to parse "
                                          "write operator: no '{}' printer "
                                          "found",
                                          printer_name))};
    }
    const saver_plugin* saver;
    auto saver_argument = std::get<1>(result);
    if (saver_argument) {
      auto saver_name = std::get<1>(*saver_argument);
      saver = plugins::find<saver_plugin>(saver_name);
      if (!saver) {
        return {std::string_view{f, l},
                caf::make_error(ec::syntax_error,
                                fmt::format("failed to parse "
                                            "write operator: no '{}' saver "
                                            "found",
                                            saver_name))};
      }
    } else {
      auto default_saver = printer->make_default_saver();
      if (!default_saver) {
        return {std::string_view{f, l},
                caf::make_error(ec::invalid_configuration,
                                fmt::format("failed to parse write operator: "
                                            "no available default sink for "
                                            "printing '{}' output "
                                            "found",
                                            printer->name()))};
      }
      auto [default_saver_name, _] = *default_saver;
      saver = plugins::find<vast::saver_plugin>(default_saver_name);
      if (!saver) {
        return {std::string_view{f, l},
                caf::make_error(ec::invalid_configuration,
                                fmt::format("failed to parse write operator: "
                                            "default sink '{0}' for "
                                            "printing '{1}' output "
                                            "found",
                                            default_saver_name,
                                            printer->name()))};
      }
    }
    if (saver->saver_requires_joining()
        and not printer->printer_allows_joining()) {
      return {std::string_view{f, l},
              caf::make_error(ec::invalid_argument,
                              fmt::format("writing '{0}' to '{1}' is not "
                                          "allowed; the sink '{1}' requires a "
                                          "single input, and the format '{0}' "
                                          "has potentially multiple outputs",
                                          printer->name(), saver->name()))};
    } else if (not saver->saver_requires_joining()) {
      auto op = std::make_unique<print_save_operator>(std::move(*printer),
                                                      std::move(*saver));
      return {std::string_view{f, l}, std::move(op)};
    }
    auto ops = std::vector<operator_ptr>{};
    ops.emplace_back(std::make_unique<print_operator>(std::move(*printer)));
    ops.emplace_back(std::make_unique<save_operator>(std::move(*saver)));
    return {
      std::string_view{f, l},
      std::make_unique<class pipeline>(std::move(ops)),
    };
  }
};

class to_plugin final : public virtual operator_plugin {
public:
  auto initialize([[maybe_unused]] const record& plugin_config,
                  [[maybe_unused]] const record& global_config)
    -> caf::error override {
    return {};
  }

  [[nodiscard]] auto name() const -> std::string override {
    return "to";
  };

  [[nodiscard]] auto make_operator(std::string_view pipeline) const
    -> std::pair<std::string_view, caf::expected<operator_ptr>> override {
    using parsers::end_of_pipeline_operator, parsers::required_ws_or_comment,
      parsers::optional_ws_or_comment, parsers::extractor, parsers::plugin_name,
      parsers::extractor_char, parsers::extractor_list;
    const auto* f = pipeline.begin();
    const auto* const l = pipeline.end();
    const auto p = optional_ws_or_comment >> plugin_name
                   >> -(required_ws_or_comment >> string_parser{"write"}
                        >> required_ws_or_comment >> plugin_name)
                   >> optional_ws_or_comment >> end_of_pipeline_operator;
    auto result = std::tuple{
      std::string{}, std::optional<std::tuple<std::string, std::string>>{}};
    if (!p(f, l, result)) {
      return {
        std::string_view{f, l},
        caf::make_error(ec::syntax_error, fmt::format("failed to parse "
                                                      "to operator: '{}'",
                                                      pipeline)),
      };
    }
    auto saver_name = std::get<0>(result);
    auto saver = plugins::find<saver_plugin>(saver_name);
    if (!saver) {
      return {std::string_view{f, l},
              caf::make_error(ec::syntax_error,
                              fmt::format("failed to parse "
                                          "write operator: no '{}' saver "
                                          "found",
                                          saver_name))};
    }
    const printer_plugin* printer;
    auto printer_argument = std::get<1>(result);
    if (printer_argument) {
      auto printer_name = std::get<1>(*printer_argument);
      printer = plugins::find<printer_plugin>(printer_name);
      if (!printer) {
        return {std::string_view{f, l},
                caf::make_error(ec::syntax_error,
                                fmt::format("failed to parse "
                                            "to operator: no '{}' printer "
                                            "found",
                                            printer_name))};
      }
    } else {
      auto default_printer = saver->make_default_printer();
      if (!default_printer) {
        return {std::string_view{f, l},
                caf::make_error(ec::invalid_configuration,
                                fmt::format("failed to parse write operator: "
                                            "no available default printer for "
                                            "sink '{}' "
                                            "found",
                                            saver->name()))};
      }
      auto [default_printer_name, _] = *default_printer;
      printer = plugins::find<vast::printer_plugin>(default_printer_name);
      if (!printer) {
        return {std::string_view{f, l},
                caf::make_error(ec::invalid_configuration,
                                fmt::format("failed to parse write operator: "
                                            "default format '{0}' for "
                                            "sink '{1}' "
                                            "is unavailable",
                                            default_printer_name,
                                            saver->name()))};
      }
    }
    if (saver->saver_requires_joining()
        and not printer->printer_allows_joining()) {
      return {std::string_view{f, l},
              caf::make_error(ec::invalid_argument,
                              fmt::format("writing '{0}' to '{1}' is not "
                                          "allowed; the sink '{1}' requires a "
                                          "single input, and the format '{0}' "
                                          "has potentially multiple outputs",
                                          printer->name(), saver->name()))};
    } else if (not saver->saver_requires_joining()) {
      auto op = std::make_unique<print_save_operator>(std::move(*printer),
                                                      std::move(*saver));
      return {std::string_view{f, l}, std::move(op)};
    }
    auto ops = std::vector<operator_ptr>{};
    ops.emplace_back(std::make_unique<print_operator>(std::move(*printer)));
    ops.emplace_back(std::make_unique<save_operator>(std::move(*saver)));
    return {
      std::string_view{f, l},
      std::make_unique<class pipeline>(std::move(ops)),
    };
  }
};

} // namespace

} // namespace vast::plugins::write_to

VAST_REGISTER_PLUGIN(vast::plugins::write_to::write_plugin)
VAST_REGISTER_PLUGIN(vast::plugins::write_to::to_plugin)
