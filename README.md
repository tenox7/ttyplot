# ttyplot 1.4.1 — legacy build

A deliberately old-fashioned ttyplot for **old operating systems**: UnixWare, QNX,
IRIX, Tru64, AIX, HP-UX, old Solaris, and anything else with a K&R-ish C compiler
and a narrow-character curses.

It keeps the simple, portable architecture of ttyplot 1.4/1.5.2 and back-ports the
worthwhile features added in 1.6/1.7 that do **not** require modern APIs.

## What it deliberately does NOT use

- no wide-character / UTF-8 curses (`ncursesw`, `cchar_t`, `setcchar`) — hence no
  braille (`-b`), block (`-B`) or aalib (`-A`) modes
- no `select()`/`poll()`, no non-blocking I/O, no self-pipe, no threads
- no independent/timer-based refresh — the screen repaints only on **new data** or
  a **window resize**
- signals are used the old, portable way (`signal()`), and all painting happens in
  the main loop, never inside a signal handler

## Features (vs the original 1.4)

- **True negative values** with a zero line (positive up, negative down), for one or
  two series — `-S` sets an initial lower scale, `-M` fixes it
- **Colors** — `-C` with numeric values or the `dark1/dark2/light1/light2` schemes
- **Sub-second rate** mode (`-r`) via `gettimeofday()`
- works on **smaller windows**, hides the clock when too narrow, "Window too small…"
- `-v` / `-h`, does not quit on EOF, real error messages, `-0.0` label cleanup,
  divide-by-zero / oversized-window guards
- `FAKETIME` env freezes the clock at `Thu Jan  1 00:00:00 1970` for testing

## Build

```sh
make                      # cc ttyplot.c -lcurses -o ttyplot
make CURSES=-lncurses     # if the system curses is called ncurses
```

Portability knobs (add to `CPPFLAGS`):

| define          | when to use                                                |
|-----------------|------------------------------------------------------------|
| `-DNOACS`       | terminal lacks ACS_* line glyphs → ASCII `-` `|` `L`       |
| `-DNOGETMAXYX`  | curses lacks `getmaxyx()` → falls back to `LINES`/`COLS`   |

`_AIX` and `__sun` are compiler-predefined and select the right `refresh()` /
`asctime_r()` behavior automatically. No `pkg-config`, no `-lm`.

Examples for crusty targets:

```sh
# UnixWare 7 / QNX 6
make CPPFLAGS=-DNOACS CURSES=-lcurses
# AIX (xlc predefines _AIX), Solaris (predefines __sun), IRIX, Tru64
make CURSES=-lcurses
```

## Usage

```sh
{ while :; do echo $RANDOM; sleep 1; done; } | ./ttyplot
ping -i 1 8.8.8.8 | sed -u 's/.*time=//;s/ ms//' | ./ttyplot -t "ping" -u ms
```

Quit with **Ctrl-C**. There are no keyboard hotkeys (that would need `select()` on a
second input; out of scope for the legacy model).

## Known limitations (inherent to the legacy model)

- The clock and plot only advance when a new sample arrives. With slow input the
  clock looks "stuck" between samples — this is the price of not using `select()`.
- The ring buffer is sized to the window width, so **growing** the terminal can
  briefly expose stale columns until the plot refills. (Same as 1.4/1.5.2.)
- At very narrow widths the stats line and the clock can overlap. (Same as upstream.)
