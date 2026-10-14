#!/bin/sh
# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Verifies that shortcut handling resolves every key exactly like the legacy
# implementation did, and that the built-in keymaps match the registered
# functions. Runs traverso headless with an empty configuration, so the
# user's own settings and shortcuts are neither read nor changed.
#
# Usage: check.sh path/to/traverso

set -e

binary="$1"
here="$(cd "$(dirname "$0")" && pwd)"
config="$(mktemp -d)"
table="$config/resolution-table.txt"
trap 'rm -rf "$config"' EXIT

XDG_CONFIG_HOME="$config" QT_QPA_PLATFORM=offscreen "$binary" --dump-shortcut-table "$table" > /dev/null 2>&1

result=0

if diff -u "$here/legacy-resolution-table.txt" "$table"; then
    echo "Shortcut resolution identical to the legacy implementation"
else
    echo "Shortcut resolution differs from the legacy implementation (see diff above)"
    result=1
fi

# Problems reported for the built-in keymaps that are not known yet
problems="$(awk '/^# PROBLEMS/ {p=1; next} /^#/ {p=0} p' "$table" | sort -u)"
known="$(grep -v '^#' "$here/known-problems.txt" | sort -u)"
new="$(printf '%s\n' "$problems" | grep -vxF -e "$known" | grep . || true)"

if [ -n "$new" ]; then
    echo "Problems in the built-in keymaps or registrations:"
    printf '%s\n' "$new"
    result=1
else
    echo "No new problems in the built-in keymaps or registrations"
fi

exit $result
