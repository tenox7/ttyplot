#! /usr/bin/env python3
# Copyright (c) 2015 by pyte authors and contributors
# Copyright (c) 2023 by Sebastian Pipping <sebastian@pipping.org>
#
# Licensed under LGPL v3, see pyte's LICENSE file for more details.
#
# Based on pyte's example "nanoterm.py"
# https://raw.githubusercontent.com/selectel/pyte/master/examples/nanoterm.py
# and a few lines from
# https://github.com/selectel/pyte/blob/master/pyte/screens.py

import enum
import os
import pty
import select
import signal
import sys
import time
from functools import lru_cache
from typing import Callable, Generator

import pyte
from pyte.screens import Char, StaticDefaultDict
from wcwidth import wcwidth as _wcwidth  # type: ignore[import-untyped]


wcwidth: Callable[[str], int] = lru_cache(maxsize=4096)(_wcwidth)  # from pyte/screens.py


class AnsiGraphics(enum.Enum):
    # https://en.wikipedia.org/wiki/ANSI_escape_code#SGR_(Select_Graphic_Rendition)_parameters
    RESET = 0
    REVERSE = 7


def ansi_sequence(n):
    # https://en.wikipedia.org/wiki/ANSI_escape_code#CSI_(Control_Sequence_Introducer)_sequences
    return f'\033[{n.value}m'


def pyte_char_to_ansi(ch: Char) -> str:
    """
    Render a single isolated ``pyte.screens.Char`` using ANSI escape sequences

    https://pyte.readthedocs.io/en/latest/api.html#pyte.screens.Char
    """
    chunks = []

    if ch.reverse:
        chunks.append(ansi_sequence(AnsiGraphics.RESET))
        chunks.append(ansi_sequence(AnsiGraphics.REVERSE))

    chunks.append(ch.data)

    if ch.reverse:
        chunks.append(ansi_sequence(AnsiGraphics.RESET))

    return ''.join(chunks)


def ansi_display(screen):
    """
    A (mostly identical) fork of ``pyte.screens.Screen.display``
    that uses ``pyte_char_to_ansi`` rather than ``line[x].data``.
    """
    def render(line: StaticDefaultDict[int, Char]) -> Generator[str, None, None]:
        is_wide_char = False
        for x in range(screen.columns):
            if is_wide_char:  # Skip stub
                is_wide_char = False
                continue
            char = line[x].data
            assert sum(map(wcwidth, char[1:])) == 0
            is_wide_char = wcwidth(char[0]) == 2
            yield pyte_char_to_ansi(line[x])

    return ["".join(render(screen.buffer[y])) for y in range(screen.lines)]


def dump(screen, frame_number):
    print(f'[{screen.columns}x{screen.lines}] Frame {frame_number}:')
    print(f'+{"-" * screen.columns}+')
    for line in ansi_display(screen):
        print(f'|{line}|')
    print(f'+{"-" * screen.columns}+')
    print(flush=True)


def get_runtime_nanos():
    return time.clock_gettime_ns(time.CLOCK_MONOTONIC)


def create_rhythmic_dumper(screen):
    nano_before = get_runtime_nanos()
    frame_number = 1

    screen.dirty.clear()

    def dump_or_not(last=False):
        nonlocal nano_before
        nonlocal frame_number

        nano_now = get_runtime_nanos()

        # For CI robustness, we want to:
        # 1. Display 1 frame per second at most
        # 2. Never display two identical consecutive frames
        # 3. Only show an all-empty frame if it is not the first frame
        #    or the last frame
        # 4. Always show at least one frame (overruling 3.)
        if last or nano_now - nano_before >= 1_000_000_000:
            if (frame_number == 1 and last) or (screen.dirty and not ((frame_number == 1 or last) and ''.join(ansi_display(screen)).isspace())):
                dump(screen, frame_number=frame_number)
                screen.dirty.clear()
                frame_number += 1
            nano_before = nano_now

    return dump_or_not


if __name__ == "__main__":
    if len(sys.argv) <= 1:
        progname_py = os.path.basename(sys.argv[0])
        sys.exit(f'usage: python3 {progname_py} COMMAND [ARG ..]')

    COLUMNS = 90
    LINES = 20

    screen = pyte.Screen(COLUMNS, LINES)
    stream = pyte.ByteStream(screen)

    dump_or_not = create_rhythmic_dumper(screen)

    p_pid, master_fd = pty.fork()
    if p_pid == 0:  # Child.
        env = os.environ.copy()
        env.update(dict(TERM="linux", COLUMNS=str(COLUMNS), LINES=str(LINES)))
        os.execvpe(sys.argv[1], sys.argv[1:], env=env)

    while True:
        try:
            readables, _w, _x = select.select(
                [master_fd], [], [], 0.1)
        except (KeyboardInterrupt,  # Stop right now!
                ValueError):        # Nothing to read.
            break

        dump_or_not()

        if not readables:
            continue

        try:
            data = os.read(master_fd, 1024)
        except OSError:
            break

        if not data:
            break

        stream.feed(data)

        dump_or_not()

    os.kill(p_pid, signal.SIGTERM)

    dump_or_not(last=True)
