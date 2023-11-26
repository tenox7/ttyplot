#! /usr/bin/env bash
##
## Copyright (c) 2023 by Sebastian Pipping
## Apache License 2.0
##

set -e -u

self_dir="$(dirname "$(type -P "$0")")"

cd "${self_dir}"

if ! git diff --exit-code >/dev/null \
        || ! git diff --cached --exit-code >/dev/null ; then
    echo 'ERROR: Please commit/stash your uncommitted work first, aborting.' >&2
    exit 1
fi

make -C ..  # to ensure up-to-date ttyplot binary

./record.sh

for i in actual-*.png ; do
    cp -v "${i}" "${i/actual/expected}"
done

if type -P zopflipng &>/dev/null; then
    for i in expected-*.png ; do
        # https://github.com/google/zopfli
        zopflipng -y "${i}" "${i}"
    done
fi

git add expected-*.png

EDITOR=true git commit -m 'recordings: Sync expected-*.png images'
