#!/bin/sh
# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Verifies that shortcut handling resolves every key exactly like the legacy
# implementation did. Runs traverso headless with an empty configuration, so
# the user's own settings and shortcuts are neither read nor changed.
#
# Usage: check.sh path/to/traverso

set -e

binary="$1"
here="$(cd "$(dirname "$0")" && pwd)"
config="$(mktemp -d)"
table="$config/resolution-table.txt"

XDG_CONFIG_HOME="$config" QT_QPA_PLATFORM=offscreen "$binary" --dump-shortcut-table "$table" > /dev/null 2>&1

if diff -u "$here/legacy-resolution-table.txt" "$table"; then
    echo "Shortcut resolution identical to the legacy implementation"
    rm -rf "$config"
else
    echo "Shortcut resolution differs from the legacy implementation (see diff above)"
    rm -rf "$config"
    exit 1
fi
