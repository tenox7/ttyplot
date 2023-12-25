#! /usr/bin/env bash
##
## Copyright (c) 2023 by Sebastian Pipping
## Apache License 2.0
##

set -e -u

self_dir="$(dirname "$(realpath "$(type -P "$0")")")"
ttyplot_bin_dir="${self_dir}/.."  # i.e. the local build

export PATH="${ttyplot_bin_dir}:${PATH}"

# Consistent clock display for reproducibility
export FAKETIME=yesplease


cd "${self_dir}"

# Ensure recent and pinned pyte
[[ -d venv/ ]] || python3 -m venv venv/
source venv/bin/activate
pip3 install --disable-pip-version-check pyte==0.8.2

# Check and report on runtime requirements
which realpath timeout ttyplot

# Enforce a diff on failure
rm -f actual.txt

# Run and record the actual test run
# MallocNanoZone=0 is for AddressSanitizer on macOS, see https://stackoverflow.com/a/70209891/11626624 .
./flip_book.py \
    timeout -s INT 5 sh -c '{ sleep 2.5; seq 4; sleep 1.5; } | MallocNanoZone=0 ttyplot -2 -c X' 2>&1 \
    | tee actual.txt
