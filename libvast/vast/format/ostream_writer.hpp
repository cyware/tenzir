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

#include "vast/detail/overload.hpp"
#include "vast/error.hpp"
#include "vast/format/writer.hpp"
#include "vast/policy/flatten_layout.hpp"
#include "vast/policy/include_field_names.hpp"
#include "vast/table_slice.hpp"
#include "vast/table_slice_row.hpp"

#include <caf/error.hpp>

#include <iosfwd>
#include <memory>
#include <string_view>
#include <vector>

namespace vast::format {

class ostream_writer : public writer {
public:
  // -- member types -----------------------------------------------------------

  using ostream_ptr = std::unique_ptr<std::ostream>;

  // -- constructors, destructors, and assignment operators --------------------

  explicit ostream_writer(ostream_ptr out);

  ostream_writer() = default;

  ostream_writer(ostream_writer&&) = default;

  ostream_writer& operator=(ostream_writer&&) = default;

  ~ostream_writer() override;

  // -- overrides --------------------------------------------------------------

  caf::expected<void> flush() override;

  // -- properties -------------------------------------------------------------

  /// @returns the managed output stream.
  /// @pre `out_ != nullptr`
  std::ostream& out();

protected:
  struct line_elements {
    std::string_view separator;
    std::string_view kv_separator;
    std::string_view begin_of_line;
    std::string_view end_of_line;
  };

  /// Appends `x` to `buf_`.
  void append(std::string_view x) {
    buf_.insert(buf_.end(), x.begin(), x.end());
  }

  /// Appends `x` to `buf_`.
  void append(char x) {
    buf_.emplace_back(x);
  }

  template <class... Policies, class Printer>
  caf::error
  print_record(Printer& printer, const line_elements& le,
               const record_type& layout, table_slice_row row, size_t pos) {
    auto print_field = [&](auto& iter, const record_field& f, size_t column) {
      auto rep = [&](data_view x) {
        if constexpr (detail::is_any_v<policy::include_field_names, Policies...>)
          return std::pair{std::string_view{f.name}, x};
        else
          return x;
      };
      auto x = to_canonical(f.type, row[column]);
      return printer.print(iter, rep(std::move(x)));
    };
    auto iter = std::back_inserter(buf_);
    append(le.begin_of_line);
    auto start = 0;
    for (const auto& f : layout.fields) {
      if (!!start++)
        append(le.separator);
      if (const auto& r = caf::get_if<record_type>(&f.type)) {
        if constexpr (detail::is_any_v<policy::include_field_names,
                                       Policies...>) {
          append(f.name);
          append(le.kv_separator);
        }
        if (auto err = print_record<Policies...>(printer, le, *r, row, pos))
          return err;
      } else {
        if (!print_field(iter, f, pos++))
          return ec::print_error;
      }
    }
    append(le.end_of_line);
    return caf::none;
  }

  /// Prints a table slice using the given VAST printer. This function assumes
  /// a human-readable output where each row in the slice gets printed to a
  /// single line.
  /// @tparam Policies... accepted tags are ::include_field_names to repeat the
  ///         field name for each value (e.g., JSON output) and
  ///         ::flatten_layout to flatten nested records into the top level
  ///         event.
  /// @param printer The VAST printer for generating formatted output.
  /// @param xs The table slice for printing.
  /// @param begin_of_line Prefix for each printed line. For example, a JSON
  ///        writer would start each line with a '{'.
  /// @param separator Character sequence for separating columns. For example,
  ///        most human-readable formats could use ", ".
  /// @param end_of_line Suffix for each printed line. For example, a JSON
  ///        writer would end each line with a '}'.
  /// @returns `ec::print_error` if `printer` fails to generate output,
  ///          otherwise `caf::none`.
  template <class... Policies, class Printer>
  caf::error
  print(Printer& printer, const table_slice& xs, const line_elements& le) {
    auto&& layout = [&]() {
      if constexpr (detail::is_any_v<policy::flatten_layout, Policies...>)
        return flatten(xs.layout());
      else
        return xs.layout();
    }();
    for (size_t row = 0; row < xs.rows(); ++row) {
      if (auto err = print_record<Policies...>(printer, le, layout,
                                               table_slice_row{xs, row}, 0))
        return err;
      append('\n');
      write_buf();
    }
    return caf::none;
  }

  /// Writes the content of `buf_` to `out_` and clears `buf_` afterwards.
  void write_buf();

  /// Buffer for building lines before writing to `out_`. Printing into this
  /// buffer with a `back_inserter` and then calling `out_->write(...)` gives a
  /// 4x speedup over printing directly to `out_`, even when setting
  /// `sync_with_stdio(false)`.
  std::vector<char> buf_;

  /// Output stream for writing to STDOUT or disk.
  ostream_ptr out_;
};

/// @relates ostream_writer
using ostream_writer_ptr = std::unique_ptr<ostream_writer>;

} // namespace vast::format
