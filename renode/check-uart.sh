#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Checks the MMUART1 output of a headless Renode run ('make sim').
#
#   sh renode/check-uart.sh <uart log> <expected lines> [<renode log>]
#
# Fails if the log is missing or empty, if any line reports a failure
# ("FAIL", "no response", or a line starting "*** ": a fault report from
# the libraries), or if a line from the expected file (fixed strings; '#'
# comment lines and blank lines skipped) does not appear. POSIX sh: also runs
# under SoftConsole's busybox sh on Windows.

uart="$1"
expected="$2"
renode_log="$3"

if [ -z "$uart" ] || [ -z "$expected" ]; then
    echo "usage: $0 <uart log> <expected lines> [<renode log>]" >&2
    exit 2
fi

if [ ! -s "$uart" ]; then
    echo "check-uart: FAIL: no UART output in $uart" >&2
    if [ -n "$renode_log" ] && [ -f "$renode_log" ]; then
        echo "check-uart: last lines of $renode_log:" >&2
        tail -n 15 "$renode_log" >&2
    fi
    exit 1
fi

status=0

if grep -n -e 'FAIL' -e 'no response' -e '^\*\*\* ' "$uart" > /dev/null; then
    echo "check-uart: FAIL: failure reported in $uart:" >&2
    grep -n -e 'FAIL' -e 'no response' -e '^\*\*\* ' "$uart" >&2
    status=1
fi

found=0
missing=0
# The last line may lack a newline: still read it.
while IFS= read -r line || [ -n "$line" ]; do
    line=$(printf '%s' "$line" | tr -d '\r')
    case "$line" in
        ''|'#'*) continue ;;
    esac
    if grep -F -e "$line" "$uart" > /dev/null; then
        found=$((found + 1))
    else
        echo "check-uart: FAIL: missing: $line" >&2
        missing=$((missing + 1))
        status=1
    fi
done < "$expected"

if [ "$status" -eq 0 ]; then
    echo "check-uart: PASS ($found expected lines found in $uart)"
else
    echo "check-uart: $found expected lines found, $missing missing" >&2
fi
exit "$status"
