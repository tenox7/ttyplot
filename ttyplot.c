//
// ttyplot: a realtime plotting utility for terminal with data input from stdin
// Copyright (c) 2018-2025 by Antoni Sawicki
// Copyright (c) 2019-2024 by Google LLC
// Copyright (c) 2023-2024 by Edgar Bonet
// Copyright (c) 2023-2024 by Sebastian Pipping
// Apache License 2.0
//
// LEGACY 1.4.1 build: for old operating systems (UnixWare, QNX, IRIX, Tru64,
// AIX, HP-UX, old Solaris, ...). Keeps the simple, portable, blocking model:
//   - narrow-character curses only (no widechar / ncursesw, no braille/blocks)
//   - blocking scanf() input, no select()/poll()/non-blocking I/O, no threads
//   - screen only repaints on new data or a window resize (no independent clock)
//   - plain signal() handling (no self-pipe), painting done in the main loop
// Backported from 1.6/1.7: true negative values, colors, sub-second rate,
// smaller-window support, robustness fixes.
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <float.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <curses.h>
#include <signal.h>
#include <errno.h>

#ifdef __OpenBSD__
#include <err.h>
#endif

#ifndef VERSION_STR
#define VERSION_STR "1.4.1"
#endif

// Line-drawing glyphs. Old/limited terminals: build with -DNOACS for ASCII.
#define T_RARR '>'
#define T_UARR '^'
#ifdef NOACS
#define T_HLINE '-'
#define T_VLINE '|'
#define T_LLCR 'L'
#else
#define T_HLINE ACS_HLINE
#define T_VLINE ACS_VLINE
#define T_LLCR ACS_LLCORNER
#endif

// Default background for color pairs. use_default_colors() (transparent bg, -1)
// is an ncurses extension; classic SVr4 curses lacks it, so fall back to black.
#ifdef NCURSES_VERSION
#define TP_DEFAULT_BG (-1)
#else
#define TP_DEFAULT_BG COLOR_BLACK
#endif

// Minimum window geometry.
#define WIDTH_MIN 44
#define HEIGHT_MIN 5
#define WIDTH_CLOCK 24  // strlen("Thu Jan  1 00:00:00 1970")
#define WIDTH_CLOCK_MIN (WIDTH_MIN + WIDTH_CLOCK)

// Standard curses color numbers, for readability.
#define C_BLACK 0
#define C_RED 1
#define C_GREEN 2
#define C_YELLOW 3
#define C_BLUE 4
#define C_MAGENTA 5
#define C_CYAN 6
#define C_WHITE 7

enum ColorElement {
    LINE_COLOR = 0,
    AXES_COLOR,
    TEXT_COLOR,
    TITLE_COLOR,
    MAX_ERROR_COLOR,
    MIN_ERROR_COLOR,
    NUM_COLOR_ELEMENTS
};

#define VALUES_MAX 1024

chtype plotchar, max_errchar, min_errchar;
time_t t1;
struct tm *lt;
double td = 0;
double softmax = 0.0, hardmax = FLT_MAX, softmin = 0.0, hardmin = -FLT_MAX;
char title[256] = ".: ttyplot :.", unit[64] = {0}, ls[256] = {0};
double values1[VALUES_MAX] = {0}, values2[VALUES_MAX] = {0};
char valid[VALUES_MAX] = {0};  // which columns hold real data (portable NaN-free)
int width = 0, height = 0, n = -1, v = 0, rate = 0, two = 0;
int plotwidth = WIDTH_MIN - 4, plotheight = 0;
int fake_clock = 0;
char *errstr = NULL;
int colors[NUM_COLOR_ELEMENTS] = {-1, -1, -1, -1, -1, -1};
int line2color = -1;  // parsed for -C compatibility; unused without braille
const char *verstring = "https://github.com/tenox7/ttyplot " VERSION_STR;

// Set by the SIGWINCH handler; acted upon in the main loop (never paint from a
// signal handler -- that is what made the old resize path fragile).
volatile sig_atomic_t got_winch = 0;

static int iround(double x) {  // all callers pass x >= 0
    return (int)(x + 0.5);
}

static void usage(void) {
    // Split across several printf() calls to stay within the 509-char string
    // literal limit that C90 compilers are only required to support.
    printf(
        "Usage:\n"
        "  ttyplot [-2] [-r] [-c plotchar] [-s scale] [-S min] [-m max] [-M min]\n"
        "          [-t title] [-u unit] [-C colors]\n"
        "  ttyplot -h\n"
        "  ttyplot -v\n"
        "\n");
    printf(
        "  -2 read two values and draw two plots, the second one is in reverse video\n"
        "  -r rate of a counter (divide value by measured sample interval)\n"
        "  -c character to use for plot line, eg @ # %% . etc\n"
        "  -e character to use for error line when value exceeds max (default: e)\n"
        "  -E character to use for error symbol when value is below min (default: v)\n");
    printf(
        "  -s initial scale of the plot (can grow if data has larger value)\n"
        "  -S initial minimum of the scale (can grow down if data has smaller value)\n"
        "  -m maximum value, if exceeded draws error line (see -e), fixes upper scale\n"
        "  -M minimum value, if underrun draws error symbol (see -E), fixes lower scale\n"
        "  -t title of the plot\n"
        "  -u unit displayed beside vertical bar\n");
    printf(
        "  -C color[,axes,text,title,max_err,min_err]  set colors 0-7:\n"
        "     black=0 red=1 green=2 yellow=3 blue=4 magenta=5 cyan=6 white=7\n"
        "     schemes: dark1 dark2 light1 light2 (e.g. -C dark1)\n"
        "  -v print the current version and exit\n"
        "  -h print this help message and exit\n"
        "\n"
        "Press Ctrl-C to quit.\n");
}

static void version(void) {
    printf("ttyplot %s\n", VERSION_STR);
}

static void set_color_scheme(const char *name) {
    if (strcmp(name, "dark1") == 0) {
        colors[LINE_COLOR] = C_RED;
        line2color = C_GREEN;
        colors[AXES_COLOR] = C_CYAN;
        colors[TEXT_COLOR] = C_WHITE;
        colors[TITLE_COLOR] = C_YELLOW;
        colors[MAX_ERROR_COLOR] = C_RED;
        colors[MIN_ERROR_COLOR] = C_GREEN;
    } else if (strcmp(name, "dark2") == 0) {
        colors[LINE_COLOR] = C_BLUE;
        line2color = C_YELLOW;
        colors[AXES_COLOR] = C_CYAN;
        colors[TEXT_COLOR] = C_WHITE;
        colors[TITLE_COLOR] = C_GREEN;
        colors[MAX_ERROR_COLOR] = C_RED;
        colors[MIN_ERROR_COLOR] = C_MAGENTA;
    } else if (strcmp(name, "light1") == 0) {
        colors[LINE_COLOR] = C_GREEN;
        colors[AXES_COLOR] = C_BLUE;
        colors[TEXT_COLOR] = C_BLACK;
        colors[TITLE_COLOR] = C_RED;
        colors[MAX_ERROR_COLOR] = C_RED;
        colors[MIN_ERROR_COLOR] = C_MAGENTA;
    } else if (strcmp(name, "light2") == 0) {
        colors[LINE_COLOR] = C_BLUE;
        colors[AXES_COLOR] = C_GREEN;
        colors[TEXT_COLOR] = C_BLACK;
        colors[TITLE_COLOR] = C_YELLOW;
        colors[MAX_ERROR_COLOR] = C_RED;
        colors[MIN_ERROR_COLOR] = C_MAGENTA;
    }
}

// Replace *v1/*v2 by their time derivatives; return seconds since previous call.
// Uses gettimeofday() (portable via <sys/time.h>) for sub-second resolution.
static double derivative(double *v1, double *v2) {
    static double pv1 = 0, pv2 = 0, pt = 0;
    static int first = 1;
    struct timeval tv;
    double t, dt, d;
    gettimeofday(&tv, NULL);
    t = tv.tv_sec + 1e-6 * tv.tv_usec;
    dt = first ? 0 : t - pt;  // no meaningful interval on the first sample
    first = 0;
    pt = t;
    if (v1) {
        d = *v1 - pv1;
        pv1 = *v1;
        *v1 = (dt > 0) ? d / dt : 0;
        if (*v1 < 0)  // counter rewind
            *v1 = 0;
    }
    if (v2) {
        d = *v2 - pv2;
        pv2 = *v2;
        *v2 = (dt > 0) ? d / dt : 0;
        if (*v2 < 0)
            *v2 = 0;
    }
    return dt;
}

static void getminmax(int pw, double *values, double *min, double *max, double *avg) {
    double tot = 0;
    int i, cnt = 0;
    *min = FLT_MAX;
    *max = -FLT_MAX;
    for (i = 0; i < pw; i++) {
        if (! valid[i])
            continue;
        if (values[i] > *max)
            *max = values[i];
        if (values[i] < *min)
            *min = values[i];
        tot += values[i];
        cnt++;
    }
    if (cnt == 0) {
        *min = 0;
        *max = 0;
        *avg = 0;
    } else {
        *avg = tot / cnt;
    }
}

static void gethw(void) {
#ifdef NOGETMAXYX
    height = LINES;
    width = COLS;
#else
    getmaxyx(stdscr, height, width);
#endif
}

static void draw_axes(int h, int ph, int pw, double max, double min, char *unit) {
    double lv;
    if (colors[AXES_COLOR] != -1)
        attron(COLOR_PAIR(AXES_COLOR + 1));
    mvhline(h - 3, 2, T_HLINE, pw);
    mvvline(2, 2, T_VLINE, ph);
    mvaddch(h - 3, 2 + pw, T_RARR);
    mvaddch(1, 2, T_UARR);
    mvaddch(h - 3, 2, T_LLCR);
    if (colors[AXES_COLOR] != -1)
        attroff(COLOR_PAIR(AXES_COLOR + 1));

    if (colors[TEXT_COLOR] != -1)
        attron(COLOR_PAIR(TEXT_COLOR + 1));
    if (max - min >= 0.1) {  // guard against unreadable/duplicate labels
        mvprintw(1, 4, "%.1f %s", max, unit);
        lv = min / 4 + max * 3 / 4;
        if (lv > -0.01 && lv < 0.01)
            lv = 0.0;  // avoid "-0.0"
        mvprintw((ph / 4) + 1, 4, "%.1f %s", lv, unit);
        lv = min / 2 + max / 2;
        if (lv > -0.01 && lv < 0.01)
            lv = 0.0;
        mvprintw((ph / 2) + 1, 4, "%.1f %s", lv, unit);
        lv = min * 3 / 4 + max / 4;
        if (lv > -0.01 && lv < 0.01)
            lv = 0.0;
        mvprintw((ph * 3 / 4) + 1, 4, "%.1f %s", lv, unit);
    }
    if (colors[TEXT_COLOR] != -1)
        attroff(COLOR_PAIR(TEXT_COLOR + 1));
}

// Draw one column. Handles all-positive plots (zero_pos == 0, original logic)
// and plots that cross a zero line (zero_pos > 0). c1/c2 already carry color.
static void draw_line(int x, int ph, int l1, int l2, chtype c1, chtype c2,
                      int c2_is_err, int zero_pos, double d1, double d2, int has_v2,
                      chtype space) {
    chtype c1r = c1 | A_REVERSE;
    chtype c2r = c2 | A_REVERSE;

    if (zero_pos > 0) {  // plot has negative values: draw relative to a zero line
        int zero_line = ph + 1 - zero_pos;
        int y1s, y1e, y2s, y2e;
        (void)c1r;

        if (d1 > 0) {
            y1s = ph + 1 - l1;
            y1e = zero_line;
        } else if (d1 < 0) {
            y1s = zero_line;
            y1e = ph + 1 - l1;
        } else {
            y1s = y1e = zero_line;
        }

        if (has_v2) {
            if (d2 > 0) {
                y2s = ph + 1 - l2;
                y2e = zero_line;
            } else if (d2 < 0) {
                y2s = zero_line;
                y2e = ph + 1 - l2;
            } else {
                y2s = y2e = zero_line;
            }
        } else {
            y2s = y2e = -1;
        }

        if (has_v2 && y2s != -1) {
            int os = (y1s > y2s) ? y1s : y2s;
            int oe = (y1e < y2e) ? y1e : y2e;
            if (y1s < y2s)
                mvvline(y1s, x, c1, y2s - y1s);
            else if (y2s < y1s)
                mvvline(y2s, x, c2_is_err ? c2r : space, y1s - y2s);
            if (os <= oe)
                mvvline(os, x, c2r, oe - os + 1);
            if (y1e > y2e)
                mvvline(y2e + 1, x, c1, y1e - y2e);
            else if (y2e > y1e)
                mvvline(y1e + 1, x, c2_is_err ? c2r : space, y2e - y1e);
        } else {
            if (y1s < y1e)
                mvvline(y1s, x, c1, y1e - y1s + 1);
            else if (y1s > y1e)
                mvvline(y1e, x, c1, y1s - y1e + 1);
            else
                mvvline(y1s, x, c1, 1);
        }
    } else {  // all-positive plot: original 1.4 logic
        if (l1 > l2) {
            mvvline(ph + 1 - l1, x, c1, l1 - l2);
            mvvline(ph + 1 - l2, x, c2r, l2);
        } else if (l1 < l2) {
            mvvline(ph + 1 - l2, x, c2_is_err ? c2r : space, l2 - l1);
            mvvline(ph + 1 - l1, x, c2r, l1);
        } else {
            mvvline(ph + 1 - l2, x, c2r, l2);
        }
    }
}

static void plot_values(int ph, int pw, double max, double min, int cur, chtype pc,
                        chtype hce, chtype lce) {
    chtype linecol = (colors[LINE_COLOR] != -1) ? COLOR_PAIR(LINE_COLOR + 1) : 0;
    chtype maxcol = (colors[MAX_ERROR_COLOR] != -1) ? COLOR_PAIR(MAX_ERROR_COLOR + 1) : 0;
    chtype mincol = (colors[MIN_ERROR_COLOR] != -1) ? COLOR_PAIR(MIN_ERROR_COLOR + 1) : 0;
    chtype space = ' ' | A_REVERSE | linecol;
    double range = max - min;
    int zero_pos = 0;
    int x, i;

    if (range <= 0)
        range = 1;
    if (min < 0 && max > 0)
        zero_pos = iround((0 - min) / range * ph);
    else if (max <= 0)
        zero_pos = ph;

    i = (cur + 1) % pw;
    for (x = 3; x < 3 + pw; x++, i = (i + 1) % pw) {
        int l1, l2, c2_is_err = 0;
        chtype c1, c2;
        double d1, d2 = 0;
        if (! valid[i])
            continue;

        d1 = values1[i];
        if (d1 > hardmax) {
            l1 = ph;
            c1 = hce | maxcol;
        } else if (d1 < hardmin) {
            l1 = 1;
            c1 = lce | mincol;
        } else {
            l1 = iround((d1 - min) / range * ph);
            c1 = pc | linecol;
        }

        if (! two) {
            l2 = 0;
            c2 = pc | linecol;
        } else {
            d2 = values2[i];
            if (d2 > hardmax) {
                l2 = ph;
                c2 = hce | maxcol;
                c2_is_err = 1;
            } else if (d2 < hardmin) {
                l2 = 1;
                c2 = lce | mincol;
                c2_is_err = 1;
            } else {
                l2 = iround((d2 - min) / range * ph);
                c2 = pc | linecol;
            }
        }

        draw_line(x, ph, l1, l2, c1, c2, c2_is_err, zero_pos, d1, d2, two, space);
    }
}

static void show_all_centered(const char *message) {
    const size_t message_len = strlen(message);
    const int x = ((int)message_len > width) ? 0 : (width / 2 - (int)message_len / 2);
    const int y = height / 2;
    if (colors[TITLE_COLOR] != -1)
        attron(COLOR_PAIR(TITLE_COLOR + 1));
    mvaddnstr(y, x, message, width);
    if (colors[TITLE_COLOR] != -1)
        attroff(COLOR_PAIR(TITLE_COLOR + 1));
}

static int window_big_enough_to_draw(void) {
    return (width >= WIDTH_MIN) && (height >= HEIGHT_MIN);
}

static void show_window_size_error(void) {
    show_all_centered("Window too small...");
}

static void paint_plot(void) {
    double min, max;
    double min1 = FLT_MAX, max1 = -FLT_MAX, avg1 = 0;
    double min2 = FLT_MAX, max2 = -FLT_MAX, avg2 = 0;
    chtype linecol = (colors[LINE_COLOR] != -1) ? COLOR_PAIR(LINE_COLOR + 1) : 0;
    int cur = (n < 0) ? 0 : n;
    int vx;

    erase();
#ifdef _AIX
    refresh();
#endif
    gethw();

    plotheight = height - 4;
    plotwidth = width - 4;
    if (plotwidth < 1)
        plotwidth = 1;
    if (plotwidth > VALUES_MAX - 1)
        plotwidth = VALUES_MAX - 1;  // clamp to buffer instead of exiting
    if (n >= plotwidth)
        n = plotwidth - 1;

    getminmax(plotwidth, values1, &min1, &max1, &avg1);
    if (two)
        getminmax(plotwidth, values2, &min2, &max2, &avg2);

    max = (max1 > max2) ? max1 : max2;
    if (max < softmax)
        max = softmax;
    if (hardmax != FLT_MAX)
        max = hardmax;
    min = (min1 < min2) ? min1 : min2;
    if (min > softmin)
        min = softmin;
    if (hardmin != -FLT_MAX)
        min = hardmin;
    if (max <= min)
        max = min + 1;

    // Version string (bottom-right) and wall clock (above it).
    if (colors[TEXT_COLOR] != -1)
        attron(COLOR_PAIR(TEXT_COLOR + 1));
    vx = width - (int)strlen(verstring) - 1;
    if (vx < 0)
        vx = 0;
    mvaddstr(height - 1, vx, verstring);
    if (width >= WIDTH_CLOCK_MIN) {
        const char *cd;
        if (fake_clock) {
            cd = "Thu Jan  1 00:00:00 1970";
        } else {
            lt = localtime(&t1);
#ifdef __sun
            asctime_r(lt, ls, sizeof(ls));
#else
            asctime_r(lt, ls);
#endif
            ls[strlen(ls) - 1] = '\0';  // drop trailing newline from asctime_r
            cd = ls;
        }
        mvaddstr(height - 2, width - (int)strlen(cd) - 1, cd);
    }
    if (colors[TEXT_COLOR] != -1)
        attroff(COLOR_PAIR(TEXT_COLOR + 1));

    // Legend swatch + stats for series 1.
#ifdef _AIX
    mvaddch(height - 2, 5, plotchar | linecol);
#else
    mvvline(height - 2, 5, plotchar | A_NORMAL | linecol, 1);
#endif
    if (colors[TEXT_COLOR] != -1)
        attron(COLOR_PAIR(TEXT_COLOR + 1));
    if (v > 0) {
        mvprintw(height - 2, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s ", values1[cur],
                 min1, max1, avg1, unit);
        if (rate)
            printw(" interval=%.3gs", td);
    }
    if (two) {
        mvaddch(height - 1, 5, ' ' | A_REVERSE | linecol);
        if (v > 0)
            mvprintw(height - 1, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s   ",
                     values2[cur], min2, max2, avg2, unit);
    }
    if (colors[TEXT_COLOR] != -1)
        attroff(COLOR_PAIR(TEXT_COLOR + 1));

    plot_values(plotheight, plotwidth, max, min, cur, plotchar, max_errchar,
                min_errchar);
    draw_axes(height, plotheight, plotwidth, max, min, unit);

    if (colors[TITLE_COLOR] != -1)
        attron(COLOR_PAIR(TITLE_COLOR + 1));
    mvaddstr(0, (width / 2) - ((int)strlen(title) / 2), title);
    if (colors[TITLE_COLOR] != -1)
        attroff(COLOR_PAIR(TITLE_COLOR + 1));

    move(0, 0);
}

static void redraw_screen(void) {
    if (window_big_enough_to_draw()) {
        paint_plot();
        if (errstr != NULL)
            show_all_centered(errstr);
        else if (v < 1)
            show_all_centered("waiting for data from stdin");
    } else {
        erase();
        gethw();
        show_window_size_error();
    }
    refresh();
}

static void winch_handler(int signum) {
    int saved_errno = errno;  // keep errno intact for the interrupted syscall
    (void)signum;
    got_winch = 1;
    errno = saved_errno;
}

static void finish(int signum) {
    (void)signum;
    curs_set(TRUE);
    echo();
    refresh();
    endwin();
    exit(0);
}

// Store one incoming number. In -2 mode, pair two numbers into one record
// (pairing may span line boundaries). Returns 1 when a full record was stored.
static int handle_value(double value) {
    static double saved;
    static int have_saved = 0;

    if (two && ! have_saved) {
        saved = value;
        have_saved = 1;
        return 0;
    }

    n = (n + 1) % plotwidth;
    if (two) {
        values1[n] = saved;
        values2[n] = value;
        have_saved = 0;
    } else {
        values1[n] = value;
    }
    valid[n] = 1;
    if (v < INT_MAX)
        v++;

    time(&t1);  // wall clock for the display
    if (rate)
        td = derivative(&values1[n], two ? &values2[n] : NULL);
    return 1;
}

// Parse every number from one input line and store it. Garbage tokens and
// non-finite values (NaN, +/-inf) are skipped rather than corrupting the plot.
// Returns the number of full records stored (used to decide whether to redraw).
static int handle_line(char *line) {
    static const char delims[] = " \t\r\n\v\f";
    char *token, *str = line, *number_end;
    double value;
    int records = 0;

    while ((token = strtok(str, delims)) != NULL) {
        str = NULL;  // keep strtok working on the same string
        value = strtod(token, &number_end);
        if (*number_end != '\0')  // token was not a pure number
            continue;
        if (value != value || value > DBL_MAX || value < -DBL_MAX)  // NaN / inf
            continue;
        records += handle_value(value);
    }
    return records;
}

static void parse_colors(char *arg) {
    char *token, *slash;
    int idx = 0;
    if (strcmp(arg, "dark1") == 0 || strcmp(arg, "dark2") == 0 ||
        strcmp(arg, "light1") == 0 || strcmp(arg, "light2") == 0) {
        set_color_scheme(arg);
        return;
    }
    token = strtok(arg, ",");
    while (token != NULL && idx < NUM_COLOR_ELEMENTS) {
        if (idx == 0) {
            slash = strchr(token, '/');  // line/line2 form (line2 unused here)
            if (slash) {
                *slash = '\0';
                line2color = atoi(slash + 1);
            }
        }
        if (*token)
            colors[idx] = atoi(token);
        idx++;
        token = strtok(NULL, ",");
    }
}

int main(int argc, char *argv[]) {
    int i, c;
    int input_closed = 0;
    int show_ver = 0, show_usage = 0;
    int cached_opterr;
    const char *optstring = "2rc:e:E:s:S:m:M:t:u:vhC:";
    char linebuf[4096];
    int any_color;

    // plotchar defaults to T_VLINE, but ACS_VLINE is only valid after initscr()
    // (it reads the runtime acs_map[]), so it is set below. -c/-2 may override it.
    plotchar = 0;
    max_errchar = 'e';
    min_errchar = 'v';

    // Frozen clock for deterministic UI testing.
    fake_clock = (getenv("FAKETIME") != NULL);

    cached_opterr = opterr;
    opterr = 0;

    // First pass: handle -h / -v / errors before touching the terminal.
    while ((c = getopt(argc, argv, optstring)) != -1) {
        switch (c) {
            case 'v':
                show_ver = 1;
                break;
            case 'h':
                show_usage = 1;
                break;
            case '?':
                usage();
                exit(1);
        }
    }
    if (show_usage) {
        usage();
        exit(0);
    }
    if (show_ver) {
        version();
        exit(0);
    }

    // Second pass: process options. Reset optind (and optreset on BSD/macOS).
    optind = 1;
#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__DragonFly__) || defined(__APPLE__)
    optreset = 1;
#endif

    while ((c = getopt(argc, argv, optstring)) != -1) {
        switch (c) {
            case 'r':
                rate = 1;
                break;
            case '2':
                two = 1;
                plotchar = '|';
                break;
            case 'c':
                plotchar = optarg[0];
                break;
            case 'e':
                max_errchar = optarg[0];
                break;
            case 'E':
                min_errchar = optarg[0];
                break;
            case 's':
                softmax = atof(optarg);
                break;
            case 'S':
                softmin = atof(optarg);
                break;
            case 'm':
                hardmax = atof(optarg);
                break;
            case 'M':
                hardmin = atof(optarg);
                break;
            case 't':
                snprintf(title, sizeof(title), "%s", optarg);
                break;
            case 'u':
                snprintf(unit, sizeof(unit), "%s", optarg);
                break;
            case 'C':
                parse_colors(optarg);
                break;
        }
    }
    opterr = cached_opterr;

    if (softmax <= hardmin)
        softmax = hardmin + 1;
    if (hardmax <= hardmin)
        hardmax = FLT_MAX;

    if (initscr() == NULL) {
        fprintf(stderr, "Error: failed to initialize curses\n");
        exit(1);
    }

    if (plotchar == 0)  // not set by -c/-2; now that acs_map[] is live, use T_VLINE
        plotchar = T_VLINE;

#ifdef __OpenBSD__
    if (pledge("stdio tty", NULL) == -1)
        err(1, "pledge");
#endif

    any_color = 0;
    for (i = 0; i < NUM_COLOR_ELEMENTS; i++)
        if (colors[i] != -1)
            any_color = 1;
    if (any_color && has_colors()) {
        start_color();
#ifdef NCURSES_VERSION
        use_default_colors();
#endif
        for (i = 0; i < NUM_COLOR_ELEMENTS; i++)
            if (colors[i] != -1)
                init_pair(i + 1, colors[i], TP_DEFAULT_BG);
    }

    time(&t1);
    noecho();
    curs_set(FALSE);
    erase();
    refresh();
    gethw();
    redraw_screen();

    // sigaction (POSIX.1, on every target OS) gives reliable semantics: the
    // handler stays installed (no SysV one-shot re-arm) and, with no SA_RESTART,
    // a resize interrupts the blocking read instead of being swallowed.
    {
        struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = winch_handler;
        sigaction(SIGWINCH, &sa, NULL);
        sa.sa_handler = finish;
        sigaction(SIGINT, &sa, NULL);
    }

    while (1) {
        // A resize was requested; rebuild curses state and repaint (in the main
        // loop, not the signal handler).
        if (got_winch) {
            got_winch = 0;
            endwin();
            refresh();
            clear();
            redraw_screen();
        }

        // Input is gone: wait for a signal (resize -> repaint, INT -> quit)
        // instead of spinning. pause() returns on any caught signal.
        if (input_closed) {
            pause();
            continue;
        }

        // Blocking line read. fgets keeps the portable model (no select/
        // non-blocking); handle_line() does the robust strtod tokenizing.
        if (fgets(linebuf, sizeof(linebuf), stdin) == NULL) {
            if (errno == EINTR) {  // interrupted by a signal (likely SIGWINCH)
                clearerr(stdin);
                errno = 0;
                continue;
            }
            errstr = feof(stdin) ? "input stream closed" : strerror(errno);
            input_closed = 1;
            redraw_screen();
            continue;
        }

        if (handle_line(linebuf) > 0)  // redraw only when new data was stored
            redraw_screen();
    }

    endwin();
    return 0;
}
