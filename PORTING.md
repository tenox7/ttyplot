# ttyplot 1.4.1 — legacy backport analysis

Goal: a modern-ish ttyplot that still builds on UnixWare, QNX, IRIX, Tru64, AIX,
HP-UX, old Solaris — i.e. keep the simple blocking model, no widechar, no
independent refresh, no select/threads — but pull in the worthwhile features
added since 1.4.

> **Status: implemented on this `legacy` branch.** This branch is cut from the
> **1.5.2** commit (the true fork point), so `git diff 1.5.2..legacy -- ttyplot.c`
> is the clean review diff. The build is `ttyplot.c` + `Makefile` + `README.md` at
> the repo root. It compiles clean with `-Wall -Wextra` and `-std=c89 -pedantic`,
> in the `NOACS`/`NOGETMAXYX`/`_AIX` variants, needs only `-lcurses` (no `-lm`,
> no `pkg-config`), and was rendered/verified via tmux for positives, negatives
> (one & two series), error lines, rate, colors, tiny-window, and live resize.

## TL;DR

- **Base 1.4.1 on 1.5.2, not 1.4.** 1.5.2 is the *last* version with the legacy
  architecture (blocking `scanf`, `curses.h`, `NOACS`/`NOGETMAXYX` guards) and it
  already ships most of the portable improvements for free.
- **Restore** the `_AIX` and `__sun` `#ifdef`s that 1.5.2 deleted (they still live
  in 1.4).
- **Backport 4 things** from 1.6/1.7: true negative values (+`-S`), colors (`-C`),
  divide-by-zero/tiny-window guards, and FAKETIME. Optional: sub-second rate.
- **Exclude** braille/block/aalib, keyboard hotkeys, and independent clock refresh —
  all require widechar ncurses and/or the `select()` event loop.

## Version archaeology — where portability actually broke

The user's memory conflates two separate events:

| Event | Version | Date | What happened |
|---|---|---|---|
| AIX/Solaris `#ifdef`s removed | **1.5.2** | 2023-11-17 | commit `a7a464e` "remove support for aix and solaris" — dropped the `_AIX` refresh workaround and `__sun` asctime_r variant. **Architecture unchanged.** |
| Portability dropped wholesale | **1.6.0** | 2023-12-25 | `select()` (×2) + `pipe()` self-pipe signals + widechar (`cchar_t`, `ncurses.h`) landed **together**. This is the real break. |

So 1.5.2 is *not* where "independent window refresh" arrived — it's where the last
two OS special-cases were dropped, but the code was still blocking-`scanf` and
narrow-char. **1.6.0 is the dividing line.**

## Architecture comparison

| Aspect | 1.4 | **1.5.2 (base)** | 1.6.0 → 1.7.5 (HEAD) |
|---|---|---|---|
| Input | blocking `scanf` | blocking `scanf` | non-blocking `read()` + `select()` |
| Screen refresh | on data only | on data only | independent (select timeout, clock ticks every 1s) |
| Signals | `signal(SIGWINCH/INT)` | `signal` + `sigprocmask` | self-pipe write + `select` read |
| Character model | narrow `chtype` | narrow `chtype` | wide `cchar_t` (ncursesw) |
| curses header | `curses.h` | `curses.h` | `ncurses.h` |
| Keyboard | none (Ctrl-C only) | none | q/r/^L via `/dev/tty` |
| Old-OS guards | `NOACS`,`NOGETMAXYX`,`_AIX`,`__sun` | `NOACS`,`NOGETMAXYX` | none (`_XOPEN_SOURCE` only) |

Everything in the HEAD column that says "select" or "wide" is out of scope by the
user's own constraints. That's why 1.5.2 is the right fork point.

## Port matrix

### A) Already in 1.5.2 — free, just inherit them

| Feature | Notes |
|---|---|
| `-v` / `-h` flags (two-pass getopt) | portable |
| "Window too small…" message | portable |
| `-M` hardmin + `-E` min-error char | present (but see negatives caveat below) |
| do-not-quit-on-EOF ("input stream closed", `pause()`) | portable |
| `v` counter — only average/plot values actually read | portable; **use this, not NaN** |
| single-loop `getminmax` | portable |
| real error text from `scanf` failure (`strerror`) | portable |
| `NOACS` / `NOGETMAXYX` guards | present |

### B) New backports — the actual work (1.6+/1.7 → 1.4.1)

| Feature | Source | Effort | Portability notes |
|---|---|---|---|
| **True negative values** (draw above/below a zero line) | 1.7.3 `zero_pos` logic (`75bf096`, `5b95f86`, `f3bfe02`) | **Medium — own tested phase** | Pure integer math + `mvvline`. Must (a) **remove 1.5.2's clamp-to-0** at lines 408–411, and (b) port the two-line overlap logic back from `cchar_t`/`mvvline_set` to narrow `chtype`/`mvvline`. This is the only bug-prone item. |
| **`-S` softmin** | HEAD | Small | pairs with negatives; scale floor that can grow down |
| **Colors `-C`** (schemes + `r,g,b,…` + `line/line2`) | 1.7.1 `6773c6d` | Small–Medium | `start_color`/`init_pair`/`COLOR_PAIR`/`attron` are SVr4 — portable. **TRAP:** `use_default_colors()` is an ncurses-only extension; guard it (`#ifdef NCURSES_VERSION`) and fall back to `COLOR_BLACK` background on classic curses. Skip the braille-only `PAIR_BR*` pairs. |
| **Divide-by-zero / tiny-window guards** | `c4e21c6`, `71451a2` | Trivial | `WIDTH_*`/`HEIGHT_*` macros, guard `plotwidth==0`, `max-min` denominator |
| **Axis-label polish** | HEAD `draw_axes` | Trivial | `-0.0` suppression, `max-min>=0.1` guard |
| **FAKETIME frozen clock** | HEAD | Trivial | `getenv("FAKETIME")` → fixed string; enables deterministic CI |
| **Sub-second rate** (optional) | HEAD `derivative()` | Small | swap `time()`→`gettimeofday()` (portable via `<sys/time.h>`); gives fractional `interval=` instead of integer seconds |

### C) Excluded — needs widechar / select / threads

| Feature | Why excluded |
|---|---|
| Braille `-b`, block/quadrant `-B` | wide-char glyphs (`setcchar`, `mvadd_wch`) — needs ncursesw |
| aalib `-A` | libaa + widechar fold; also experimental |
| Multibyte `-c` plotchar | wide char. Keep single-byte `optarg[0]` |
| Independent clock refresh (tick every 1s w/o data) | `select()` timeout loop. Clock will only advance on new data — accepted tradeoff |
| Keyboard hotkeys q / r / ^L | `/dev/tty` + `select`. Rely on Ctrl-C (SIGINT) to quit |
| Self-pipe signal handling | `select`-based. Use plain `signal()` |

## One decision to confirm before building — signals & resize

"No signals … no dynamic resizing," read literally, would remove even SIGINT — but
then Ctrl-C leaves the terminal wrecked (no `endwin`, no cursor restore).
Recommended middle ground (matches 1.4's own behavior, stays portable):

1. **Keep a minimal `SIGINT` handler** that only calls `curs_set(TRUE)`/`echo()`/
   `endwin()` and exits — purely to restore the terminal. Not "independent refresh."
2. **Re-read `getmaxyx` on each data draw** (1.4 already does this) so a resized
   window adapts on the *next* data point. No live repaint.
3. **Drop 1.5.2's `SIGWINCH → paint_plot()` handler.** Painting from inside a signal
   handler *is* the "dynamic resizing" you don't want, and it's the fragile part.

Net: quit is clean, resize adapts lazily, nothing repaints asynchronously.

## Suggested 1.4.1 result

- Source ≈ 1.5.2 (≈460 lines) + negatives + colors ≈ **~600 lines**, single file,
  `curses.h`, narrow char.
- Build: `cc -DNOACS -DNOGETMAXYX ttyplot.c -lcurses -o ttyplot` on the crustiest
  targets; plain `-lcurses` elsewhere.
- Restore `_AIX` (`refresh()` after `erase()`, `mvaddch` instead of `mvvline` for the
  legend swatch) and `__sun` (`asctime_r(lt, ls, sizeof ls)` 3-arg form) guards.

**Caveat on verification:** portability here is judged on API availability (POSIX +
SVr4 curses), not test builds — UnixWare/QNX/IRIX can't be compiled in this
environment. The one concrete trap identified is `use_default_colors()`.
