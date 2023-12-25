#! /usr/bin/env bash
##
## Copyright (c) 2023 by Sebastian Pipping
## Apache License 2.0
##

set -e -u

: ${MAKE:=make}

self_dir="$(dirname "$(type -P "$0")")"

cd "${self_dir}"

if ! git diff --exit-code >/dev/null \
        || ! git diff --cached --exit-code >/dev/null ; then
    echo 'ERROR: Please commit/stash your uncommitted work first, aborting.' >&2
    exit 1
fi

"${MAKE}" -C ..  # to ensure up-to-date ttyplot binary

./record.sh

cp actual.txt expected.txt

git add expected.txt

if git diff --cached --exit-code >/dev/null ; then
    echo 'Already in sync, good.'
else
    EDITOR=true git commit -m 'recordings: Sync expected.txt'
fi
