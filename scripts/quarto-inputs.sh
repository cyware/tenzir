#! /bin/sh

# A utility script for use the Makefile in /web that retrieves all inputs used
# for quarto relative to the current working directory.

dir="$(dirname "$(readlink -f "$0")")"
toplevel="$(git -C ${dir} rev-parse --show-toplevel)"
for qmd_file in "$(find "${toplevel}/web" -type f -name "*.qmd")"; do
  find "$(dirname "${qmd_file}")" \
    -type f \
    -not -wholename "$(dirname "${qmd_file}")/$(basename -s ".qmd" "${qmd_file}")_files/*" \
    -not -wholename "$(dirname "${qmd_file}")/$(basename -s ".qmd" "${qmd_file}").md"
done | sort -u
