#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Checks the MMUART1 output of a headless Renode run ('make sim').
#
#   sh renode/check-uart.sh <uart log> <renode log> <expected lines>...
#
# Fails if the log is missing or empty, if any line reports a failure
# ("FAIL", "no response", or a line starting "*** ": a fault report from
# the libraries), or if a line from the expected files (fixed strings; '#'
# comment lines and blank lines skipped) does not appear. The renode log is
# only shown when the UART log is empty. POSIX sh: also runs
# under SoftConsole's busybox sh on Windows.

uart="$1"
renode_log="$2"

if [ "$#" -lt 3 ]; then
    echo "usage: $0 <uart log> <renode log> <expected lines>..." >&2
    exit 2
fi
shift 2

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
for expected in "$@"; do
    # The last line may lack a newline: still read it.
    while IFS= read -r line || [ -n "$line" ]; do
        line=$(printf '%s' "$line" | tr -d '\r')
        case "$line" in
            ''|'#'*) continue ;;
        esac
        if grep -F -e "$line" "$uart" > /dev/null; then
            found=$((found + 1))
        else
            echo "check-uart: FAIL: missing in $uart: $line" >&2
            missing=$((missing + 1))
            status=1
        fi
    done < "$expected"
done

if [ "$status" -eq 0 ]; then
    echo "check-uart: PASS ($found expected lines found in $uart)"
else
    echo "check-uart: $found expected lines found, $missing missing" >&2
fi
exit "$status"
