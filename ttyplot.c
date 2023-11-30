//
// ttyplot: a realtime plotting utility for terminal with data input from stdin
// Copyright (c) 2018 by Antoni Sawicki
// Copyright (c) 2019-2023 by Google LLC
// Copyright (c) 2023 by Edgar Bonet
// Copyright (c) 2023 by Sebastian Pipping
// Apache License 2.0
//

// This is needed on macOS to get the ncurses widechar API, and pkg-config fails to define it.
#ifdef __APPLE__
#define _XOPEN_SOURCE_EXTENDED
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
#define VERSION_PATCH 0
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

sigset_t block_sigset;
sigset_t empty_sigset;
volatile sig_atomic_t sigint_pending = 0;
volatile sig_atomic_t sigwinch_pending = 0;
cchar_t plotchar, max_errchar, min_errchar;
struct timeval now;
double td;
struct tm *lt;
double max=FLT_MIN;
double softmax=FLT_MIN, hardmax=FLT_MAX, hardmin=0.0;
char title[256]=".: ttyplot :.", unit[64]={0}, ls[256]={0};
double values1[1024]={0}, values2[1024]={0};
double min1=FLT_MAX, max1=FLT_MIN, avg1=0;
double min2=FLT_MAX, max2=FLT_MIN, avg2=0;
int width=0, height=0, n=-1, r=0, v=0, c=0, rate=0, two=0, plotwidth=0, plotheight=0;
bool fake_clock = false;
char *errstr = NULL;
bool redraw_needed = false;
const char *verstring = "https://github.com/tenox7/ttyplot " VERSION_STR;

void usage(void) {
    printf("Usage:\n"
            "  ttyplot [-2] [-r] [-c plotchar] [-s scale] [-m max] [-M min] [-t title] [-u unit]\n"
            "  ttyplot -h\n"
            "  ttyplot -v\n"
            "\n"
            "  -2 read two values and draw two plots, the second one is in reverse video\n"
            "  -r rate of a counter (divide value by measured sample interval)\n"
            "  -c character to use for plot line, eg @ # %% . etc\n"
            "  -e character to use for error line when value exceeds hardmax (default: e)\n"
            "  -E character to use for error symbol displayed when value is less than hardmin (default: v)\n"
            "  -s initial scale of the plot (can go above if data input has larger value)\n"
            "  -m maximum value, if exceeded draws error line (see -e), upper-limit of plot scale is fixed\n"
            "  -M minimum value, if entered less than this, draws error symbol (see -E), lower-limit of the plot scale is fixed\n"
            "  -t title of the plot\n"
            "  -u unit displayed beside vertical bar\n"
            "  -v print the current version and exit\n"
            "  -h print this help message and exit\n\n");
}

void version(void) {
    printf("ttyplot %s\n", VERSION_STR);
}

// Replace *v1 and *v2 (if non-NULL) by their time derivatives.
//  - v1, v2: addresses of input data and storage for results
//  - now: current time
// Return time since previous call.
double derivative(double *v1, double *v2, const struct timeval *now)
{
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

void getminmax(int pw, double *values, double *min, double *max, double *avg, int v) {
    double tot=0;
    int i=0;

    *min=FLT_MAX;
    *max=FLT_MIN;
    tot=FLT_MIN;

    for(i=0; i<pw && i<v; i++) {
       if(values[i]>*max)
            *max=values[i];

        if(values[i]<*min)
            *min=values[i];

        tot=tot+values[i];
    }

    *avg=tot/i;
}

void gethw(void) {
    #ifdef NOGETMAXYX
    height=LINES;
    width=COLS;
    #else
    getmaxyx(stdscr, height, width);
    #endif
}

void draw_axes(int h, int ph, int pw, double max, double min, char *unit) {
    mvhline(h-3, 2, T_HLINE, pw);
    mvvline(2, 2, T_VLINE, ph);
    mvprintw(1, 4, "%.1f %s", max, unit);
    mvprintw((ph/4)+1, 4, "%.1f %s", min/4 + max*3/4, unit);
    mvprintw((ph/2)+1, 4, "%.1f %s", min/2 + max/2, unit);
    mvprintw((ph*3/4)+1, 4, "%.1f %s", min*3/4 + max/4, unit);
    mvaddch(h-3, 2+pw, T_RARR);
    mvaddch(1, 2, T_UARR);
    mvaddch(h-3, 2, T_LLCR);
}

void draw_line(int x, int ph, int l1, int l2, cchar_t *c1, cchar_t *c2, cchar_t *hce, cchar_t *lce) {
    static cchar_t space = {
        .attr = A_REVERSE,
        .chars = {' ', '\0'}
    };
    cchar_t c1r = *c1, c2r = *c2;
    c1r.attr |= A_REVERSE;
    c2r.attr |= A_REVERSE;
    if(l1 > l2) {
        mvvline_set(ph+1-l1, x, c1, l1-l2 );
        mvvline_set(ph+1-l2, x, &c2r, l2 );
    } else if(l1 < l2) {
        mvvline_set(ph+1-l2, x, (c2==hce || c2==lce) ? &c2r : &space,  l2-l1 );
        mvvline_set(ph+1-l1, x, &c2r, l1 );
    } else {
        mvvline_set(ph+1-l2, x, &c2r, l2 );
    }
}

void plot_values(int ph, int pw, double *v1, double *v2, double max, double min, int n, cchar_t *pc, cchar_t *hce, cchar_t *lce, double hm) {
    const int first_col=3;
    int i=(n+1)%pw;
    int x;
    max-=min;

    for(x=first_col; x<first_col+pw; x++, i=(i+1)%pw)
        draw_line(x, ph,
                  (v1[i]>hm) ? ph  : (v1[i]<min) ?  1  : (int)(((v1[i]-min)/max)*(double)ph),
                  (v2[i]>hm) ? ph  : (v2[i]<min) ?  1  : (int)(((v2[i]-min)/max)*(double)ph),
                  (v1[i]>hm) ? hce : (v1[i]<min) ? lce : pc,
                  (v2[i]>hm) ? hce : (v2[i]<min) ? lce : pc,
                  hce, lce);
}

void show_all_centered(const char * message) {
    const size_t message_len = strlen(message);
    const int x = ((int)message_len > width) ? 0 : (width/2 - (int)message_len/2);
    const int y = height/2;
    mvaddnstr(y, x, message, width);
}

int window_big_enough_to_draw(void) {
    return (width >= 68) && (height >= 5);
}

void show_window_size_error(void) {
    show_all_centered("Window too small...");
}

void paint_plot(void) {
    erase();
    gethw();

    plotheight=height-4;
    plotwidth=width-4;
    if(plotwidth>=(int)((sizeof(values1)/sizeof(double))-1))
        exit(0);

    getminmax(plotwidth, values1, &min1, &max1, &avg1, v);
    getminmax(plotwidth, values2, &min2, &max2, &avg2, v);

    if(max1>max2)
        max=max1;
    else
        max=max2;

    if(max<softmax)
        max=softmax;
    if(hardmax!=FLT_MAX)
        max=hardmax;

    mvaddstr(height-1, width-strlen(verstring)-1, verstring);

    const char * clock_display;
    if (fake_clock) {
        clock_display = "Thu Jan  1 00:00:00 1970 ";
    } else {
        lt = localtime(&now.tv_sec);
        asctime_r(lt, ls);
        clock_display = ls;
    }
    mvaddstr(height-2, width-strlen(clock_display), clock_display);

    mvvline_set(height-2, 5, &plotchar, 1);
    if (v > 0) {
        mvprintw(height-2, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s ",  values1[n], min1, max1, avg1, unit);
        if(rate)
            printw(" interval=%.3gs", td);

    }
    if (two) {
        mvaddch(height-1, 5, ' '|A_REVERSE);
        if (v > 0) {
            mvprintw(height-1, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s   ",  values2[n], min2, max2, avg2, unit);
        }
    }

    plot_values(plotheight, plotwidth, values1, values2, max, hardmin, n, &plotchar, &max_errchar, &min_errchar, hardmax);

    draw_axes(height, plotheight, plotwidth, max, hardmin, unit);

    mvaddstr(0, (width/2)-(strlen(title)/2), title);

    move(0,0);
}

void resize(int signum) {
    (void)signum;
    sigwinch_pending = 1;
}

void finish(int signum) {
    (void)signum;
    sigint_pending = 1;
}

int pselect_without_signal_starvation(
        int nfds,
        fd_set * readfds,
        fd_set * writefds,
        fd_set * exceptfds,
        const struct timespec * timeout,
        const sigset_t * sigmask) {
    // With high pressure on file descriptors (e.g. with "ttyplot < /dev/zero")
    // a call to `pselect` could stall signal delivery for 20+ seconds on Linux.
    // To avoid that situation, we first do a call to `pselect` that is dedicated
    // to signal delivery and that only.
    // (Related: https://stackoverflow.com/q/62315082)
    const struct timespec zero_timeout = { .tv_sec = 0, .tv_nsec = 0 };

    // First call, signal delivery only
    const int select_ret = pselect(0, NULL, NULL, NULL, &zero_timeout, sigmask);

    const bool signal_received = ((select_ret == -1) && (errno == EINTR));
    if (signal_received) {
        return select_ret;
    }

    // Second call
    return pselect(nfds, readfds, writefds, exceptfds, timeout, sigmask);
}

void redraw_screen(const char * errstr) {
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

// Return a pointer to the last occurrence within [s, s+n) of one of the bytes in the string accept.
char *find_last(char *s, size_t n, const char *accept)
{
    for (int pos = n - 1; pos >= 0; pos--) {
        if (strchr(accept, s[pos]))
            return s + pos;
    }
    return NULL;  // not found
}

// Handle a single value from the input stream.
// Return whether we got a full data record.
bool handle_value(double value)
{
    static double saved_value;
    static int saved_value_valid = 0;

    // First value of a 2-value record: save it for later.
    if (two && !saved_value_valid) {
        saved_value = value;
        saved_value_valid = 1;
        return false;
    }

    // Otherwise we have a full record.
    n = (n+1) % plotwidth;
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
size_t handle_input_data(char *buffer, size_t length)
{
    static const char delimiters[] = " \t\r\n";  // white space

    // Find the last delimiter.
    char *end = find_last(buffer, length, delimiters);
    if (!end)
        return 0;
    *end = '\0';

    // Tokenize and parse.
    int records = 0;  // number or records found
    char *str = buffer;
    char *token;
    while ((token = strtok(str, delimiters)) != NULL) {
        char *number_end;
        double value = strtod(token, &number_end);
        if (*number_end != '\0')  // garbage found
            value = 0;
        if (! isfinite(value))
            value = 0;
        if (handle_value(value))
            records++;
        str = NULL;  // tell strtok() to stay on the same string
    }
    v += records;
    if (records > 0)
        redraw_needed = true;
    return end - buffer + 1;
}

// Handle an "input ready" event, where only a single read() is guaranteed to not block.
// Return whether the input stream got closed.
bool handle_input_event(void)
{
    static char buffer[4096];
    static size_t buffer_pos = 0;

    // Buffer incoming data.
    ssize_t bytes_read = read(STDIN_FILENO, buffer + buffer_pos, sizeof(buffer) - 1 - buffer_pos);
    if (bytes_read < 0) {  // read error
        if (errno == EINTR || errno == EAGAIN)  // we should try again later
            return false;
        errstr = strerror(errno);  // other errors are considered fatal
        redraw_needed = true;  // redraw to display the error message
        return true;
    }
    if (bytes_read == 0) {
        errstr = "input stream closed";
        buffer[buffer_pos++] = '\n';  // attempt to extract one last value
        handle_input_data(buffer, buffer_pos);
        redraw_needed = true;  // redraw to display the error message
        return true;
    }
    buffer_pos += bytes_read;

    // Handle this new data.
    size_t bytes_consumed = handle_input_data(buffer, buffer_pos);

    // If we have excessive garbage, discard a bunch. This is to ensure that we can always ask read for >= 1K bytes,
    // and keep good performance, especially with high input pressure.
    if (buffer_pos - bytes_consumed > sizeof(buffer) / 2)
        bytes_consumed += sizeof(buffer) / 4;

    if (bytes_consumed > 0 && bytes_consumed < buffer_pos)
        memmove(buffer, buffer + bytes_consumed, buffer_pos - bytes_consumed);
    buffer_pos -= bytes_consumed;
    return false;
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
    if (MB_CUR_MAX > 1)            // if non-ASCII characters are supported:
        plotchar.chars[0]=0x2502;  // U+2502 box drawings light vertical
    else
        plotchar.chars[0]='|';     // U+007C vertical line
    max_errchar.chars[0]='e';
    min_errchar.chars[0]='v';

    cached_opterr = opterr;
    opterr=0;

    show_ver = 0;
    show_usage = 0;

    // Run a 1st iteration over the arguments to check for usage,
    // version or error.
    while((c=getopt(argc, argv, optstring)) != -1) {
        switch(c) {
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
#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__APPLE__)
    optreset = 1;
#endif

    while((c=getopt(argc, argv, optstring)) != -1) {
        switch(c) {
            case 'r':
                rate=1;
                break;
            case '2':
                two=1;
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
                softmax=atof(optarg);
                break;
            case 'm':
                hardmax=atof(optarg);
                break;
            case 'M':
                hardmin=atof(optarg);
                for(i=0;i<1024;i++){
                    values1[i]=hardmin;
                    values2[i]=hardmin;
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

    if(softmax <= hardmin)
        softmax = hardmin + 1;
    if(hardmax <= hardmin)
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
    gethw();

    redraw_screen(errstr);

    // If stdin is redirected, open the terminal for reading user's keystrokes.
    int tty = -1;
    if (!isatty(STDIN_FILENO))
        tty = open("/dev/tty", O_RDONLY);
    if (tty != -1) {
        // Disable input line buffering. The function below works even when stdin
        // is redirected: it searches for a terminal in stdout and stderr.
        cbreak();
    }

    sigemptyset(&empty_sigset);

    sigemptyset(&block_sigset);
    sigaddset(&block_sigset, SIGINT);
    sigaddset(&block_sigset, SIGWINCH);
    sigprocmask(SIG_BLOCK, &block_sigset, NULL);

    signal(SIGWINCH, resize);
    signal(SIGINT, finish);

    while(1) {
        // Block until (a) we receive a signal or (b) stdin can be read without blocking
        // or (c) timeout expires, in order to reduce use of CPU and power while idle
        fd_set read_fds;
        FD_ZERO(&read_fds);
        int select_nfds = 0;
        if (stdin_is_open) {
            FD_SET(STDIN_FILENO, &read_fds);
            select_nfds = STDIN_FILENO + 1;
        }
        if (tty != -1) {
            FD_SET(tty, &read_fds);
            if (tty >= select_nfds)
                select_nfds = tty + 1;
        }
        struct timespec timeout = { .tv_sec = 0, .tv_nsec = 500e6 };  // 500 milliseconds for refreshing the clock
        const int select_ret = pselect_without_signal_starvation(select_nfds, &read_fds, NULL, NULL, &timeout, &empty_sigset);

        gettimeofday(&now, NULL);

        // Refresh the clock on timeouts.
        if (select_ret == 0)
            redraw_needed = true;

        // Handle signals.
        if (sigint_pending) {
            break;
        }
        if (sigwinch_pending) {
            sigwinch_pending = 0;
            endwin();
            initscr();
            erase();
            refresh();
            gethw();
            redraw_needed = true;
        }

        // Handle user's keystrokes.
        if (select_ret > 0 && FD_ISSET(tty, &read_fds)) {
            char key;
            int count = read(tty, &key, 1);
            if (count == 1) {  // we did catch a keystroke
                if (key == 'r')  // 'r' = toggle rate mode
                    rate = !rate;
                else if (key == 'q')  // 'q' = quit
                    break;
            } else if (count == 0) {
                close(tty);
                tty = -1;
            }
        }

        // Handle input data.
        if (select_ret > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
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
