#! /usr/bin/env bash
##
## Copyright (c) 2023 by Sebastian Pipping
## Apache License 2.0
##

set -e -u

self_dir="$(dirname "$(realpath "$(type -P "$0")")")"
ttyplot_bin_dir="${self_dir}/.."  # i.e. the local build
agg_bin_dir="${HOME}/.cargo/bin"  # if a local build

export PATH="${ttyplot_bin_dir}:${PATH}:${agg_bin_dir}"

# Consistent clock display for reproducibility
export FAKETIME=yesplease


cd "${self_dir}"

# Ensure recent asciinema with --cols and --rows argument support
[[ -d venv/ ]] || python3 -m venv venv/
source venv/bin/activate
pip3 install asciinema==2.3.0

# Check and report on runtime requirements
which agg asciinema convert realpath timeout ttyplot

# Enforce a diff on failure
rm -f actual*.*

asciinema_args=(
    --cols 90
    --rows 20
    # MallocNanoZone=0 is for AddressSanitizer on macOS, see https://stackoverflow.com/a/70209891/11626624 .
    -c 'timeout -s INT 5s sh -c "{ sleep 1.5; echo \"1 2 3 4\"; sleep 1.5; } | MallocNanoZone=0 ttyplot -2 -c X"'
    -t 'ttyplot waiting, drawing, and shutting down'
)

./headless.py asciinema rec "${asciinema_args[@]}" actual.cast

agg_args=(
    --fps-cap 2
    --font-family 'Liberation Mono'
)

agg "${agg_args[@]}" actual.cast actual.gif

convert -coalesce actual.gif PNG8:actual.png

ls -lh actual*.*
