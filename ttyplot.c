//
// ttyplot: a realtime plotting utility for terminal with data input from stdin
// Copyright (c) 2018 by Antoni Sawicki
// Copyright (c) 2019-2023 by Google LLC
// Copyright (c) 2023-2024 by Edgar Bonet
// Copyright (c) 2023-2024 by Sebastian Pipping
// Apache License 2.0
//

// This is needed on macOS to get the ncurses widechar API, and pkg-config fails to
// define it.
#ifdef __APPLE__
#define _XOPEN_SOURCE_EXTENDED
#else
// This is needed for musl libc
#if ! defined(_XOPEN_SOURCE) || (_XOPEN_SOURCE < 500)
#undef _XOPEN_SOURCE  // to address warnings about potential re-definition
#define _XOPEN_SOURCE 500
#endif
#endif

#include <assert.h>
#include <ctype.h>  // isspace
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <float.h>
#include <time.h>
#include <sys/time.h>
#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef __OpenBSD__
#include <err.h>
#endif

#define STR_(x) #x
#define STR(x) STR_(x)
#define VERSION_MAJOR 1
#define VERSION_MINOR 6
#define VERSION_PATCH 2
#define VERSION_STR STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_PATCH)

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

enum Event {
    // These are made to have no set bits overlap to ease flag set testing
    EVENT_TIMEOUT = 1 << 0,
    EVENT_UNKNOWN = 1 << 1,
    EVENT_SIGNAL_READABLE = 1 << 2,
    EVENT_STDIN_READABLE = 1 << 3,
    EVENT_TTY_READABLE = 1 << 4,
};

static int signal_read_fd, signal_write_fd;
static cchar_t plotchar, max_errchar, min_errchar;
static struct timeval now;
static double td;
static double softmax = 0.0, hardmax = FLT_MAX, hardmin = 0.0;
static char title[256] = ".: ttyplot :.", unit[64] = {0}, ls[256] = {0};
static double values1[1024] = {0}, values2[1024] = {0};
static int width = 0, height = 0, n = -1, v = 0, c = 0, rate = 0, two = 0,
           plotwidth = 0, plotheight = 0;
static bool fake_clock = false;
static char *errstr = NULL;
static bool redraw_needed = false;
static const char *verstring = "https://github.com/tenox7/ttyplot " VERSION_STR;

static void usage(void) {
    printf(
        "Usage:\n"
        "  ttyplot [-2] [-r] [-c plotchar] [-s scale] [-m max] [-M min] [-t title] [-u "
        "unit]\n"
        "  ttyplot -h\n"
        "  ttyplot -v\n"
        "\n"
        "  -2 read two values and draw two plots, the second one is in reverse video\n"
        "  -r rate of a counter (divide value by measured sample interval)\n"
        "  -c character to use for plot line, eg @ # %% . etc\n"
        "  -e character to use for error line when value exceeds hardmax (default: e)\n"
        "  -E character to use for error symbol displayed when value is less than "
        "hardmin (default: v)\n"
        "  -s initial scale of the plot (can go above if data input has larger value)\n"
        "  -m maximum value, if exceeded draws error line (see -e), upper-limit of "
        "plot scale is fixed\n"
        "  -M minimum value, if entered less than this, draws error symbol (see -E), "
        "lower-limit of the plot scale is fixed\n"
        "  -t title of the plot\n"
        "  -u unit displayed beside vertical bar\n"
        "  -v print the current version and exit\n"
        "  -h print this help message and exit\n"
        "\n"
        "Hotkeys:\n"
        "   q quit\n"
        "   r toggle rate mode\n");
}

static void version(void) {
    printf("ttyplot %s\n", VERSION_STR);
}

// Replace *v1 and *v2 (if non-NULL) by their time derivatives.
//  - v1, v2: addresses of input data and storage for results
//  - now: current time
// Return time since previous call.
static double derivative(double *v1, double *v2, const struct timeval *now) {
    static double previous_v1, previous_v2, previous_t = DBL_MAX;
    const double t = now->tv_sec + 1e-6 * now->tv_usec;
    const double dt = t - previous_t;
    previous_t = t;
    if (v1) {
        const double dv1 = *v1 - previous_v1;
        previous_v1 = *v1;
        if (dt <= 0)
            *v1 = 0;
        else
            *v1 = dv1 / dt;
    }
    if (v2) {
        const double dv2 = *v2 - previous_v2;
        previous_v2 = *v2;
        if (dt <= 0)
            *v2 = 0;
        else
            *v2 = dv2 / dt;
    }
    return dt;
}

static void getminmax(int pw, double *values, double *min, double *max, double *avg,
                      int v) {
    double tot = 0;
    int i = 0;

    *min = FLT_MAX;
    *max = 0.0;

    for (i = 0; i < pw && i < v; i++) {
        if (values[i] > *max)
            *max = values[i];

        if (values[i] < *min)
            *min = values[i];

        tot = tot + values[i];
    }

    *avg = tot / i;
}

static void draw_axes(int h, int ph, int pw, double max, double min, char *unit) {
    mvhline(h - 3, 2, T_HLINE, pw);
    mvvline(2, 2, T_VLINE, ph);
    if (max - min >= 0.1) {
        mvprintw(1, 4, "%.1f %s", max, unit);
        mvprintw((ph / 4) + 1, 4, "%.1f %s", min / 4 + max * 3 / 4, unit);
        mvprintw((ph / 2) + 1, 4, "%.1f %s", min / 2 + max / 2, unit);
        mvprintw((ph * 3 / 4) + 1, 4, "%.1f %s", min * 3 / 4 + max / 4, unit);
    }
    mvaddch(h - 3, 2 + pw, T_RARR);
    mvaddch(1, 2, T_UARR);
    mvaddch(h - 3, 2, T_LLCR);
}

static void draw_line(int x, int ph, int l1, int l2, cchar_t *c1, cchar_t *c2,
                      cchar_t *hce, cchar_t *lce) {
    static cchar_t space = {.attr = A_REVERSE, .chars = {' ', '\0'}};
    cchar_t c1r = *c1, c2r = *c2;
    c1r.attr |= A_REVERSE;
    c2r.attr |= A_REVERSE;
    if (l1 > l2) {
        mvvline_set(ph + 1 - l1, x, c1, l1 - l2);
        mvvline_set(ph + 1 - l2, x, &c2r, l2);
    } else if (l1 < l2) {
        mvvline_set(ph + 1 - l2, x, (c2 == hce || c2 == lce) ? &c2r : &space, l2 - l1);
        mvvline_set(ph + 1 - l1, x, &c2r, l1);
    } else {
        mvvline_set(ph + 1 - l2, x, &c2r, l2);
    }
}

static void plot_values(int ph, int pw, double *v1, double *v2, double max, double min,
                        int n, cchar_t *pc, cchar_t *hce, cchar_t *lce, double hm) {
    const int first_col = 3;
    int i = (n + 1) % pw;
    int x;
    max -= min;

    for (x = first_col; x < first_col + pw; x++, i = (i + 1) % pw)
        draw_line(x, ph,
                  (v1[i] > hm)    ? ph
                  : (v1[i] < min) ? 1
                                  : (int)(((v1[i] - min) / max) * (double)ph),
                  (v2[i] > hm)    ? ph
                  : (v2[i] < min) ? 1
                                  : (int)(((v2[i] - min) / max) * (double)ph),
                  (v1[i] > hm)    ? hce
                  : (v1[i] < min) ? lce
                                  : pc,
                  (v2[i] > hm)    ? hce
                  : (v2[i] < min) ? lce
                                  : pc,
                  hce, lce);
}

static void show_all_centered(const char *message) {
    const size_t message_len = strlen(message);
    const int x = ((int)message_len > width) ? 0 : (width / 2 - (int)message_len / 2);
    const int y = height / 2;
    mvaddnstr(y, x, message, width);
}

static int window_big_enough_to_draw(void) {
    return (width >= 68) && (height >= 5);
}

static void show_window_size_error(void) {
    show_all_centered("Window too small...");
}

static void paint_plot(void) {
    double max = 0;
    double min1 = FLT_MAX, max1 = 0, avg1 = 0;
    double min2 = FLT_MAX, max2 = 0, avg2 = 0;
    struct tm *lt;
    erase();
    getmaxyx(stdscr, height, width);

    plotheight = height - 4;
    plotwidth = width - 4;
    if (plotwidth >= (int)((sizeof(values1) / sizeof(double)) - 1))
        exit(0);

    getminmax(plotwidth, values1, &min1, &max1, &avg1, v);
    getminmax(plotwidth, values2, &min2, &max2, &avg2, v);

    if (max1 > max2)
        max = max1;
    else
        max = max2;

    if (max < softmax)
        max = softmax;
    if (hardmax != FLT_MAX)
        max = hardmax;

    mvaddstr(height - 1, width - strlen(verstring) - 1, verstring);

    const char *clock_display;
    if (fake_clock) {
        clock_display = "Thu Jan  1 00:00:00 1970 ";
    } else {
        lt = localtime(&now.tv_sec);
        asctime_r(lt, ls);
        clock_display = ls;
    }
    mvaddstr(height - 2, width - strlen(clock_display), clock_display);

    mvvline_set(height - 2, 5, &plotchar, 1);
    if (v > 0) {
        mvprintw(height - 2, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s ", values1[n],
                 min1, max1, avg1, unit);
        if (rate)
            printw(" interval=%.3gs", td);
    }
    if (two) {
        mvaddch(height - 1, 5, ' ' | A_REVERSE);
        if (v > 0) {
            mvprintw(height - 1, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s   ",
                     values2[n], min2, max2, avg2, unit);
        }
    }

    plot_values(plotheight, plotwidth, values1, values2, max, hardmin, n, &plotchar,
                &max_errchar, &min_errchar, hardmax);

    draw_axes(height, plotheight, plotwidth, max, hardmin, unit);

    mvaddstr(0, (width / 2) - (strlen(title) / 2), title);

    move(0, 0);
}

// Send signals through a pipe, in order to catch them without race conditions.
// pselect() could be an alternative, but it is unreliable on Linux.
// (Related: https://stackoverflow.com/q/62315082)
static void signal_handler(int signum) {
    const unsigned char signal_number =
        (unsigned char)signum;  // signum is either 2 (SIGINT) or 28 (SIGWINCH)
    ssize_t write_res;
    do {
        write_res = write(signal_write_fd, &signal_number, 1);
    } while ((write_res == -1) && (errno == EINTR));
}

static void redraw_screen(const char *errstr) {
    if (window_big_enough_to_draw()) {
        paint_plot();

        if (errstr != NULL) {
            show_all_centered(errstr);
        } else if (v < 1) {
            show_all_centered("waiting for data from stdin");
        }
    } else {
        show_window_size_error();
    }

    refresh();
}

// Return a pointer to the last occurrence within [s, s+n) of one of the bytes in the
// string accept.
static char *find_last(char *s, size_t n, const char *accept) {
    for (int pos = n - 1; pos >= 0; pos--) {
        if (strchr(accept, s[pos]))
            return s + pos;
    }
    return NULL;  // not found
}

// Handle a single value from the input stream.
// Return whether we got a full data record.
static bool handle_value(double value) {
    static double saved_value;
    static int saved_value_valid = 0;

    // First value of a 2-value record: save it for later.
    if (two && ! saved_value_valid) {
        saved_value = value;
        saved_value_valid = 1;
        return false;
    }

    // Otherwise we have a full record.
    n = (n + 1) % plotwidth;
    if (two) {
        values1[n] = saved_value;
        values2[n] = value;
        saved_value_valid = 0;
    } else {
        values1[n] = value;
    }
    if (rate)
        td = derivative(&values1[n], two ? &values2[n] : NULL, &now);
    return true;
}

// Handle a chunk of input data: extract the numbers, store them, redraw if needed.
// Return the number of bytes consumed.
static size_t handle_input_data(char *buffer, size_t length) {
    static const char delimiters[] = " \t\r\n";  // white space

    // Find the last delimiter.
    char *end = find_last(buffer, length, delimiters);
    if (! end)
        return 0;
    *end = '\0';

    // Tokenize and parse.
    int records = 0;  // number or records found
    char *str = buffer;
    char *token;
    while ((token = strtok(str, delimiters)) != NULL) {
        str = NULL;  // tell strtok() to stay on the same string next time

        char *number_end;
        double value = strtod(token, &number_end);
        if (*number_end != '\0')  // garbage found
            continue;
        if (! isfinite(value))
            continue;
        if (handle_value(value))
            records++;
    }
    v += records;
    if (records > 0)
        redraw_needed = true;
    return end - buffer + 1;
}

// Handle an "input ready" event, where only a single read() is guaranteed to not block.
// Return whether the input stream got closed.
static bool handle_input_event(void) {
    static char buffer[4096];
    static size_t buffer_pos = 0;

    // Buffer incoming data.
    ssize_t bytes_read =
        read(STDIN_FILENO, buffer + buffer_pos, sizeof(buffer) - 1 - buffer_pos);
    if (bytes_read < 0) {                       // read error
        if (errno == EINTR || errno == EAGAIN)  // we should try again later
            return false;
        errstr = strerror(errno);  // other errors are considered fatal
        redraw_needed = true;      // redraw to display the error message
        return true;
    }
    if (bytes_read == 0) {
        errstr = "input stream closed";
        buffer[buffer_pos++] = '\n';  // attempt to extract one last value
        handle_input_data(buffer, buffer_pos);
        redraw_needed = true;  // redraw to display the error message
        return true;
    }

    // The data we read could contain null bytes, so we replace those
    // by one of the supported delimiters to not lose all input coming after.
    for (size_t i = buffer_pos; i < buffer_pos + bytes_read; i++) {
        if (buffer[i] == '\0') {
            buffer[i] = ' ';
        }
    }

    buffer_pos += bytes_read;

    // Handle this new data.
    size_t bytes_consumed = handle_input_data(buffer, buffer_pos);

    // If we have excessive garbage, discard a bunch. This is to ensure that we can
    // always ask read for >= 1K bytes, and keep good performance, especially with high
    // input pressure.
    if (buffer_pos - bytes_consumed > sizeof(buffer) / 2)
        bytes_consumed += sizeof(buffer) / 4;

    if (bytes_consumed > 0 && bytes_consumed < buffer_pos)
        memmove(buffer, buffer + bytes_consumed, buffer_pos - bytes_consumed);
    buffer_pos -= bytes_consumed;
    return false;
}

// Refresh the clock on the next full second (plus a few milliseconds).
//
// We will sleep for a duration of up to a full second here knowing that:
// - we are technically putting two redraws apart by more than one second
// - that extra is only a few milliseconds (<25 in practice, often <1)
// - a few milliseconds is on the edge of what the human eye can notice
// - we can save CPU time (and potentially battery life) through giving
//   up on these milliseconds more in clock refresh delay accuracy.
//
// We had a constant timeout of 500 milliseconds before (which translates
// to twice the frequency of the maximum desired delay: redrawing at least
// once per second, the Nyquist frequency at work) but it ran the loop twice
// as much (including calling `select` twice as often) as the new approach.
// So we decided for lower CPU usage and a timeout of up to a full second.
//
static struct timeval calculate_clock_refresh_timeout_from(suseconds_t now_tv_usec) {
    const int microseconds_per_second = 1e6;
    const int microseconds_remaining = microseconds_per_second - now_tv_usec;
    return (struct timeval){
        .tv_sec = microseconds_remaining / microseconds_per_second,
        .tv_usec = microseconds_remaining % microseconds_per_second};
}

// Block until (a) we receive a signal or (b) stdin can be read without blocking
// or (c) timeout expires, in order to reduce use of CPU and power while idle
//
// Returns one of:
//   A) EVENT_TIMEOUT
//   B) EVENT_UNKNOWN
//   C) One or more of EVENT_*_READABLE or'ed together
//
static int wait_for_events(int signal_read_fd, int tty, bool stdin_is_open,
                           struct timeval *timeout) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(signal_read_fd, &read_fds);
    int select_nfds = signal_read_fd + 1;
    if (stdin_is_open) {
        FD_SET(STDIN_FILENO, &read_fds);
        if (STDIN_FILENO >= select_nfds)
            select_nfds = STDIN_FILENO + 1;
    }
    if (tty != -1) {
        FD_SET(tty, &read_fds);
        if (tty >= select_nfds)
            select_nfds = tty + 1;
    }

    const int select_ret = select(select_nfds, &read_fds, NULL, NULL, timeout);

    if (select_ret == 0) {
        return EVENT_TIMEOUT;
    }

    if (select_ret > 0) {
        int ret = 0;

        if (FD_ISSET(signal_read_fd, &read_fds)) {
            ret |= EVENT_SIGNAL_READABLE;
        }

        if ((tty != -1) && FD_ISSET(tty, &read_fds)) {
            ret |= EVENT_TTY_READABLE;
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            ret |= EVENT_STDIN_READABLE;
        }

        assert(ret != 0);

        return ret;
    }

    return EVENT_UNKNOWN;
}

int main(int argc, char *argv[]) {
    int i;
    bool stdin_is_open = true;
    int cached_opterr;
    const char *optstring = "2rc:e:E:s:m:M:t:u:vh";
    int show_ver;
    int show_usage;

    // To make UI testing more robust, we display a clock that is frozen at
    // "Thu Jan  1 00:00:03 1970" when variable FAKETIME is set
    fake_clock = (getenv("FAKETIME") != NULL);

    setlocale(LC_ALL, "");
    if (MB_CUR_MAX > 1)              // if non-ASCII characters are supported:
        plotchar.chars[0] = 0x2502;  // U+2502 box drawings light vertical
    else
        plotchar.chars[0] = '|';  // U+007C vertical line
    max_errchar.chars[0] = 'e';
    min_errchar.chars[0] = 'v';

    cached_opterr = opterr;
    opterr = 0;

    show_ver = 0;
    show_usage = 0;

    // Run a 1st iteration over the arguments to check for usage,
    // version or error.
    while ((c = getopt(argc, argv, optstring)) != -1) {
        switch (c) {
            case 'v':
                show_ver = 1;
                break;
            case 'h':
                show_usage = 1;
                break;
            case '?':
                // Upon error exit immediately.
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

    // Run a 2nd iteration over the arguments to actually process the options.
    // According to getopt's documentation this is done by setting optind to 1
    // (or 0 in some special cases). On BSDs and Macs optreset must be set to 1
    // in addition.
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
                break;
            case 'c':
                mbtowc(&plotchar.chars[0], optarg, MB_CUR_MAX);
                break;
            case 'e':
                mbtowc(&max_errchar.chars[0], optarg, MB_CUR_MAX);
                break;
            case 'E':
                mbtowc(&min_errchar.chars[0], optarg, MB_CUR_MAX);
                break;
            case 's':
                softmax = atof(optarg);
                break;
            case 'm':
                hardmax = atof(optarg);
                break;
            case 'M':
                hardmin = atof(optarg);
                for (i = 0; i < 1024; i++) {
                    values1[i] = hardmin;
                    values2[i] = hardmin;
                }
                break;
            case 't':
                snprintf(title, sizeof(title), "%s", optarg);
                break;
            case 'u':
                snprintf(unit, sizeof(unit), "%s", optarg);
                break;
        }
    }

    opterr = cached_opterr;

    if (softmax <= hardmin)
        softmax = hardmin + 1;
    if (hardmax <= hardmin)
        hardmax = FLT_MAX;

    initscr(); /* uses filesystem, so before pledge */

#ifdef __OpenBSD__
    if (pledge("stdio tty", NULL) == -1)
        err(1, "pledge");
#endif

    gettimeofday(&now, NULL);
    noecho();
    curs_set(FALSE);
    erase();
    refresh();
    getmaxyx(stdscr, height, width);

    redraw_screen(errstr);

    // If stdin is redirected, open the terminal for reading user's keystrokes.
    int tty = -1;
    if (! isatty(STDIN_FILENO))
        tty = open("/dev/tty", O_RDONLY);
    if (tty != -1) {
        // Disable input line buffering. The function below works even when stdin
        // is redirected: it searches for a terminal in stdout and stderr.
        cbreak();
    }

    int signal_fds[2];
    if (pipe(signal_fds) != 0) {
        perror("pipe");
        exit(1);
    }
    signal_read_fd = signal_fds[0];
    signal_write_fd = signal_fds[1];

    signal(SIGWINCH, signal_handler);
    signal(SIGINT, signal_handler);

    while (1) {
        struct timeval timeout = calculate_clock_refresh_timeout_from(now.tv_usec);

        const int events =
            wait_for_events(signal_read_fd, tty, stdin_is_open, &timeout);

        // Refresh the clock if the seconds have changed.
        const time_t displayed_time = now.tv_sec;
        gettimeofday(&now, NULL);
        if (now.tv_sec != displayed_time)
            redraw_needed = true;

        // Handle signals.
        if (events & EVENT_SIGNAL_READABLE) {
            unsigned char signal_number;
            const ssize_t count = read(signal_read_fd, &signal_number, 1);
            if (count > 0) {
                if (signal_number == SIGINT)
                    break;
                if (signal_number == SIGWINCH) {
                    endwin();
                    initscr();
                    erase();
                    refresh();
                    getmaxyx(stdscr, height, width);
                    redraw_needed = true;
                }
            }
        }

        // Handle user's keystrokes.
        if (events & EVENT_TTY_READABLE) {
            char key;
            int count = read(tty, &key, 1);
            if (count == 1) {    // we did catch a keystroke
                if (key == 'r')  // 'r' = toggle rate mode
                    rate = ! rate;
                else if (key == 'q')  // 'q' = quit
                    break;
            } else if (count == 0) {
                close(tty);
                tty = -1;
            }
        }

        // Handle input data.
        if (events & EVENT_STDIN_READABLE) {
            bool input_closed = handle_input_event();
            if (input_closed) {
                close(STDIN_FILENO);
                stdin_is_open = false;
            }
        }

        // Refresh the screen if needed.
        if (redraw_needed) {
            redraw_screen(errstr);
            redraw_needed = false;
        }
    }

    endwin();
    return 0;
}
