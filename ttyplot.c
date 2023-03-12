//
// ttyplot: a realtime plotting utility for terminal with data input from stdin
// Copyright (c) 2018 by Antoni Sawicki
// Copyright (c) 2019-2023 by Google LLC
// Apache License 2.0
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <float.h>
#include <time.h>
#include <curses.h>
#include <signal.h>

#ifdef __OpenBSD__
#include <err.h>
#endif

#define verstring "github.com/tenox7/ttyplot 1.5"

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

sigset_t sigmsk;
chtype plotchar, max_errchar, min_errchar;
time_t t1,t2,td;
struct tm *lt;
double max=FLT_MIN;
double softmax=FLT_MIN, hardmax=FLT_MAX, hardmin=0.0;
char title[256]=".: ttyplot :.", unit[64]={0}, ls[256]={0};
double values1[1024]={0}, values2[1024]={0};
double cval1=FLT_MAX, pval1=FLT_MAX;
double cval2=FLT_MAX, pval2=FLT_MAX;
double min1=FLT_MAX, max1=FLT_MIN, avg1=0;
double min2=FLT_MAX, max2=FLT_MIN, avg2=0;
int width=0, height=0, n=0, r=0, v=0, c=0, rate=0, two=0, plotwidth=0, plotheight=0;

void usage() {
    printf("Usage:\n  ttyplot [-2] [-r] [-c plotchar] [-s scale] [-m max] [-M min] [-t title] [-u unit]\n\n"
            "  -2 read two values and draw two plots, the second one is in reverse video\n"
            "  -r rate of a counter (divide value by measured sample interval)\n"
            "  -c character to use for plot line, eg @ # %% . etc\n"
            "  -e character to use for error line when value exceeds hardmax (default: e)\n"
            "  -E character to use for error symbol displayed when value is less than hardmin (default: v)\n"
            "  -s initial scale of the plot (can go above if data input has larger value)\n"
            "  -m maximum value, if exceeded draws error line (see -e), upper-limit of plot scale is fixed\n"
            "  -M minimum value, if entered less than this, draws error symbol (see -E), lower-limit of the plot scale is fixed\n"
            "  -t title of the plot\n"
            "  -u unit displayed beside vertical bar\n\n");
    exit(0);
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

void gethw() {
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
    int i;
    int x=3;
    max-=min;

    for(i=n+1; i<pw; i++)
        draw_line(x++, ph,
                  (v1[i]>hm) ? ph  : (v1[i]<min) ?  1  : (int)(((v1[i]-min)/max)*(double)ph),
                  (v2[i]>hm) ? ph  : (v2[i]<min) ?  1  : (int)(((v2[i]-min)/max)*(double)ph),
                  (v1[i]>hm) ? hce : (v1[i]<min) ? lce : pc,
                  (v2[i]>hm) ? hce : (v2[i]<min) ? lce : pc,
                  hce, lce);

    for(i=0; i<=n; i++)
        draw_line(x++, ph,
                  (v1[i]>hm) ? ph  : (v1[i]<min) ?  1  : (int)(((v1[i]-min)/max)*(double)ph),
                  (v2[i]>hm) ? ph  : (v2[i]<min) ?  1  : (int)(((v2[i]-min)/max)*(double)ph),
                  (v1[i]>hm) ? hce : (v1[i]<min) ? lce : pc,
                  (v2[i]>hm) ? hce : (v2[i]<min) ? lce : pc,
                  hce, lce);
}

void paint_plot() {
    erase();
    #ifdef _AIX
    refresh();
    #endif
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

    mvprintw(height-1, width-sizeof(verstring)/sizeof(char), verstring);

    lt=localtime(&t1);
    #ifdef __sun
    asctime_r(lt, ls, sizeof(ls));
    #else
    asctime_r(lt, ls);
    #endif
    mvprintw(height-2, width-strlen(ls), "%s", ls);

    #ifdef _AIX
    mvaddch(height-2, 5, plotchar);
    #else
    mvvline(height-2, 5, plotchar|A_NORMAL, 1);
    #endif
    mvprintw(height-2, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s ",  values1[n], min1, max1, avg1, unit);
    if(rate)
        printw(" interval=%llds", (long long int)td);

    if(two) {
        mvaddch(height-1, 5, ' '|A_REVERSE);
        mvprintw(height-1, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s   ",  values2[n], min2, max2, avg2, unit);
    }

    plot_values(plotheight, plotwidth, values1, values2, max, hardmin, n, plotchar, max_errchar, min_errchar, hardmax);

    draw_axes(height, plotheight, plotwidth, max, hardmin, unit);

    mvprintw(0, (width/2)-(strlen(title)/2), "%s", title);

    move(0,0);
    sigprocmask(SIG_BLOCK, &sigmsk, NULL);
    refresh();
    sigprocmask(SIG_UNBLOCK, &sigmsk, NULL);
}

void resize() {
    sigprocmask(SIG_BLOCK, &sigmsk, NULL);
    endwin();
    refresh();
    clear();
    sigprocmask(SIG_UNBLOCK, &sigmsk, NULL);
    paint_plot();
}

void finish() {
    sigprocmask(SIG_BLOCK, &sigmsk, NULL);
    curs_set(FALSE);
    echo();
    refresh();
    endwin();
    sigprocmask(SIG_UNBLOCK, &sigmsk, NULL);
    exit(0);
}

int main(int argc, char *argv[]) {
    plotchar=T_VLINE;
    max_errchar='e';
    min_errchar='v';

    opterr=0;
    while((c=getopt(argc, argv, "2rc:e:E:s:m:M:t:u:")) != -1)
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
                int i;
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
            case '?':
                usage();
                break;
        }

    if(softmax <= hardmin)
        softmax = hardmin + 1;
    if(hardmax <= hardmin)
        hardmax = FLT_MAX;

    initscr(); /* uses filesystem, so before pledge */

    #ifdef __OpenBSD__
    if (pledge("stdio tty", NULL) == -1)
        err(1, "pledge");
    #endif

    time(&t1);
    noecho();
    curs_set(FALSE);
    erase();
    refresh();
    gethw();
    mvprintw(height/2, (width/2)-14, "waiting for data from stdin");
    refresh();

    signal(SIGWINCH, (void*)resize);
    signal(SIGINT, (void*)finish);
    sigemptyset(&sigmsk);
    sigaddset(&sigmsk, SIGWINCH);

    while(1) {
        if(two)
            r=scanf("%lf %lf", &values1[n], &values2[n]);
        else
            r=scanf("%lf", &values1[n]);

        v++;

        if(r==0) {
            while(getchar()!='\n');
            continue;
        }
        else if(r<0) {
            mvprintw(height/2, (width/2)-10, "input stream closed");
            sigprocmask(SIG_BLOCK, &sigmsk, NULL);
            refresh();
            sigprocmask(SIG_UNBLOCK, &sigmsk, NULL);
            pause();
        }

        if(values1[n] < 0)
            values1[n] = 0;
        if(values2[n] < 0)
            values2[n] = 0;

        if(rate) {
            t2=t1;
            time(&t1);
            td=t1-t2;
            if(td==0)
                td=1;

            if(cval1==FLT_MAX)
                pval1=values1[n];
            else
                pval1=cval1;
            cval1=values1[n];

            values1[n]=(cval1-pval1)/td;

            if(values1[n] < 0) // counter rewind
                values1[n]=0;

            if(two) {
                if(cval2==FLT_MAX)
                    pval2=values2[n];
                else
                    pval2=cval2;
                cval2=values2[n];

                values2[n]=(cval2-pval2)/td;

                if(values2[n] < 0) // counter rewind
                    values2[n]=0;
            }
        } else {
            time(&t1);
        }

        paint_plot();

        if(n<(int)((plotwidth)-1))
            n++;
        else
            n=0;
    }

    endwin();
    return 0;
}
