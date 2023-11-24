#! /usr/bin/env python3
# Copyright (c) 2015 by pyte authors and contributors
# Copyright (c) 2023 by Sebastian Pipping <sebastian@pipping.org>
#
# Licensed under LGPL v3, see pyte's LICENSE file for more details.
#
# Based on pyte's example "capture.py"
# https://raw.githubusercontent.com/selectel/pyte/master/examples/capture.py

import os
import pty
import signal
import select
import sys


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(0)

    p_pid, master_fd = pty.fork()
    if p_pid == 0:  # Child.
        env = os.environ.copy()
        env['TERM'] = 'xterm-256color'
        os.execvpe(sys.argv[1], sys.argv[1:], env=env)
        assert False  # never gets here

    # Parent.
    while True:
        try:
            [_master_fd], _w, _x = select.select([master_fd], [], [])
        except (KeyboardInterrupt,  # Stop right now!
                ValueError):        # Nothing to read.
            break

        try:
            data = os.read(master_fd, 1024)
        except OSError:
            break

        if not data:
            break

    os.kill(p_pid, signal.SIGTERM)
