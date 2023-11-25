//
// ttyplot: a realtime plotting utility for terminal with data input from stdin
// Copyright (c) 2018 by Antoni Sawicki
// Copyright (c) 2019-2023 by Google LLC
// Copyright (c) 2023 by Edgar Bonet
// Copyright (c) 2023 by Sebastian Pipping
// Apache License 2.0
//

#include <assert.h>
#include <ctype.h>  // isspace
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <float.h>
#include <time.h>
#include <sys/time.h>
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
#define VERSION_MINOR 5
#define VERSION_PATCH 2
#if VERSION_PATCH == 0
    #define VERSION_STR STR(VERSION_MAJOR) "." STR(VERSION_MINOR)
#else
    #define VERSION_STR STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_PATCH)
#endif

#ifdef NOACS
#define T_HLINE '-'
#define T_VLINE '|'
#define T_RARR '>'
#define T_UARR '^'
#define T_LLCR 'L'
#else
#define T_HLINE ACS_HLINE
#define T_VLINE ACS_VLINE
#define T_RARR ACS_RARROW
#define T_UARR ACS_UARROW
#define T_LLCR ACS_LLCORNER
#endif

sigset_t block_sigset;
sigset_t empty_sigset;
volatile sig_atomic_t sigint_pending = 0;
volatile sig_atomic_t sigwinch_pending = 0;
chtype plotchar, max_errchar, min_errchar;
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
const char *verstring = "https://github.com/tenox7/ttyplot " VERSION_STR;

void usage(void) {
    printf("Usage:\n  ttyplot [-h] [-v] [-2] [-r] [-c plotchar] [-s scale] [-m max] [-M min] [-t title] [-u unit]\n\n"
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

void draw_line(int x, int ph, int l1, int l2, chtype c1, chtype c2, chtype hce, chtype lce) {
    if(l1 > l2) {
        mvvline(ph+1-l1, x, c1, l1-l2 );
        mvvline(ph+1-l2, x, c2|A_REVERSE, l2 );
    } else if(l1 < l2) {
        mvvline(ph+1-l2, x, (c2==hce || c2==lce) ? c2|A_REVERSE : ' '|A_REVERSE,  l2-l1 );
        mvvline(ph+1-l1, x, c1|A_REVERSE, l1 );
    } else {
        mvvline(ph+1-l2, x, c2|A_REVERSE, l2 );
    }
}

void plot_values(int ph, int pw, double *v1, double *v2, double max, double min, int n, chtype pc, chtype hce, chtype lce, double hm) {
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

    lt=localtime(&now.tv_sec);
    asctime_r(lt, ls);
    mvaddstr(height-2, width-strlen(ls), ls);

    mvvline(height-2, 5, plotchar|A_NORMAL, 1);
    if (v > 0) {
        mvprintw(height-2, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s ",  values1[n], min1, max1, avg1, unit);
        if(rate)
            printw(" interval=%.3gs", td);

        if(two) {
            mvaddch(height-1, 5, ' '|A_REVERSE);
            mvprintw(height-1, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s   ",  values2[n], min2, max2, avg2, unit);
        }
    }

    plot_values(plotheight, plotwidth, values1, values2, max, hardmin, n, plotchar, max_errchar, min_errchar, hardmax);

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

int main(int argc, char *argv[]) {
    int i;
    char *errstr = NULL;
    bool stdin_is_open = true;
    int cached_opterr;
    const char *optstring = "2rc:e:E:s:m:M:t:u:vh";
    int show_ver;
    int show_usage;

    plotchar=T_VLINE;
    max_errchar='e';
    min_errchar='v';

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
                plotchar='|';
                break;
            case 'c':
                plotchar=optarg[0];
                break;
            case 'e':
                max_errchar=optarg[0];
                break;
            case 'E':
                min_errchar=optarg[0];
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

    char input_buf[4096] = "";
    size_t input_len = 0;

    while(1) {
        if (sigint_pending) {
            break;
        }

        if (sigwinch_pending) {
            sigwinch_pending = 0;

            endwin();
            initscr();
            clear();
            refresh();

            gethw();

            goto redraw_and_continue;
        }

        // Block until (a) we receive a signal or (b) stdin can be read without blocking
        // or (c) timeout expires, in oder to reduce use of CPU and power while idle
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
        const bool previous_parse_succeeded = (r == (two ? 2 : 1));
        struct timespec timeout;
        timeout.tv_sec = 0;
        if (previous_parse_succeeded) {
            timeout.tv_nsec = 0;  // we may have more input pressing, let's not throttle it down
        } else {
            timeout.tv_nsec = 500 * 1000 * 1000;  // <=500 milliseconds for a healthy clock display
        }
        const int select_ret = pselect(select_nfds, &read_fds, NULL, NULL, &timeout, &empty_sigset);

        const bool signal_received = ((select_ret == -1) && (errno == EINTR));

        if (signal_received) {
            continue;  // i.e. skip right to signal handling
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

        const bool stdin_can_be_read_without_blocking = ((select_ret > 0) && FD_ISSET(STDIN_FILENO, &read_fds));

        // Read as much from stdin as we can (first read after select is non-blocking)
        if (stdin_can_be_read_without_blocking) {
            char * const read_target = input_buf + input_len;
            const size_t max_bytes_to_read = sizeof(input_buf) - (input_len + 1 /* terminator */);
            const ssize_t bytes_read_or_error = read(STDIN_FILENO, read_target, max_bytes_to_read);

            if (bytes_read_or_error > 0) {
                input_len += bytes_read_or_error;

                // Last resort: truncate existing input if input line ever is
                //              too long
                if (input_len >= sizeof(input_buf) - 1) {
                    input_len = 0;
                }

                assert(input_len < sizeof(input_buf));
                input_buf[input_len] = '\0';
            } else {
                assert(bytes_read_or_error <= 0);
                if (bytes_read_or_error == 0) {
                    close(STDIN_FILENO);
                    errstr = "input stream closed";
                    stdin_is_open = false;
                } else {
                    assert(bytes_read_or_error == -1);
                    if ((errno != EINTR) && (errno != EAGAIN)) {
                        errstr = strerror(errno);
                        stdin_is_open = false;
                    }
                }
            }
        }

        // Extract values from record; note that the record may turn out incomplete
        double d1 = 0.0;
        double d2 = 0.0;
        int record_len = -1;

        if(two)
            r = sscanf(input_buf, "%lf %lf%*[ \t\r\n]%n", &d1, &d2, &record_len);
        else
            r = sscanf(input_buf, "%lf%*[ \t\r\n]%n", &d1, &record_len);

        // We need to detect and avoid mis-parsing "1.23" as two records "1.2" and "3"
        const bool supposedly_complete_record = (r == (two ? 2 : 1));
        const bool trailing_whitespace_present = (record_len != -1);

        if (supposedly_complete_record && ! trailing_whitespace_present) {
            const bool need_more_input = stdin_is_open;
            if (need_more_input) {
                r = 0;  // so that the parse is not mis-classified as a success further up
                goto redraw_and_continue;
            }

            record_len = input_len;  // i.e. the whole thing
        }

        // In order to not get stuck with non-doubles garbage input forever,
        // we need to drop input that we know(!) will never parse as doubles later.
        if (! supposedly_complete_record && (input_len > 0)) {
            char * walker = input_buf;

            while (isspace(*walker)) walker++;  // skip leading whitespace (if any)

            while ((*walker != '\0') && ! isspace(*walker)) walker++;  // skip non-double

            if (two) {
                if (*walker == '\0') {
                    goto redraw_and_continue;
                }

                while (isspace(*walker)) walker++;  // skip gap whitespace

                if (*walker == '\0') {
                    goto redraw_and_continue;
                }

                while ((*walker != '\0') && ! isspace(*walker)) walker++;  // skip non-double
            }

            if (*walker == '\0') {
                goto redraw_and_continue;
            }

            while (isspace(*walker)) walker++;  // skip trailing whitespace (if any)

            record_len = walker - input_buf;  // i.e. how much to drop
        }

        // Drop the record that we just processed (well-formed or not) from the input buffer
        if ((input_len > 0) && (record_len > 0)) {
            char * move_source = input_buf + record_len;
            const size_t bytes_to_move = input_len - record_len;
            memmove(input_buf, move_source, bytes_to_move);
            input_len = bytes_to_move;
            input_buf[input_len] = '\0';
        }

        if (! supposedly_complete_record) {
            goto redraw_and_continue;
        }

        v++;

        if (n < plotwidth - 1)
            n++;
        else
            n=0;

        values1[n] = d1;
        values2[n] = d2;

        if(values1[n] < 0)
            values1[n] = 0;
        if(values2[n] < 0)
            values2[n] = 0;

        gettimeofday(&now, NULL);
        if (rate)
            td=derivative(&values1[n], two ? &values2[n] : NULL, &now);

redraw_and_continue:
        redraw_screen(errstr);
    }

    endwin();
    return 0;
}
