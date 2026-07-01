//
// stresstest ttyplot
//
// License: Apache 2.0
//

// This is needed for musl libc
#if ! defined(_XOPEN_SOURCE) || (_XOPEN_SOURCE < 500)
#undef _XOPEN_SOURCE  // to address warnings about potential re-definition
#define _XOPEN_SOURCE 500
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

const char help[] =
    "Usage:\n"
    "  stresstest [-2] [-c] [-g] [-m] [-n] [-R] [-r rate]\n"
    "  stresstest -h\n"
    "\n"
    "  -h       print this help message and exit\n"
    "  -2       output two waves\n"
    "  -c       randomly chunk the output\n"
    "  -g       occasionally output garbage\n"
    "  -m       output realistic metrics (CPU/bandwidth-like, random spikes)\n"
    "  -n       output negative values\n"
    "  -R       output uniformly random values across the full range\n"
    "  -r rate  sample rate in samples/s (default: 100)\n"
    "  -s seed  set random seed\n";

const char optstring[] = "h2cgmnRr:s:";

// Return a uniformly random value in [lo, hi].
static double rand_range(double lo, double hi) {
    return lo + ((double)rand() / RAND_MAX) * (hi - lo);
}

// Generate one realistic metrics sample. Models a mean-reverting baseline
// (Ornstein-Uhlenbeck-style random walk) with occasional sharp spikes that
// rise instantly and decay exponentially -- resembling CPU/bandwidth load.
// State is carried in *level and *spike so multiple series stay independent.
static double metric_sample(double *level, double *spike) {
    const double target = 20.0;  // baseline the walk reverts toward
    const double theta = 0.02;   // reversion strength
    const double sigma = 1.5;    // baseline jitter amplitude
    const double decay = 0.85;   // spike decay per sample (~6 samples to 1/e)

    double noise = ((double)rand() / RAND_MAX) * 2.0 - 1.0;  // -1..1
    *level += theta * (target - *level) + sigma * noise;
    *spike *= decay;
    if (rand() < RAND_MAX / 100)               // ~1% chance of a new spike
        *spike += 30.0 + (rand() % 50);        // magnitude 30..79
    double value = *level + *spike;
    return value < 0 ? 0 : value;              // load is never negative
}

int main(int argc, char *argv[]) {
    char buffer[1024];
    size_t buffer_pos = 0;
    int opt;
    bool two_waves = false;
    bool chunked = false;
    bool add_garbage = false;
    bool metrics = false;
    bool super_random = false;
    bool output_negative = false;
    double rate = 100;
    unsigned int seed = time(NULL);

    // Parse the command line.
    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch (opt) {
            case 'h':
                printf(help);
                return EXIT_SUCCESS;
            case '2':
                two_waves = true;
                break;
            case 'c':
                chunked = true;
                break;
            case 'g':
                add_garbage = true;
                break;
            case 'm':
                metrics = true;
                break;
            case 'n':
                output_negative = true;
                break;
            case 'R':
                super_random = true;
                break;
            case 'r':
                rate = atof(optarg);
                break;
            case 's':
                seed = atoi(optarg);
                break;
            default:
                fprintf(stderr, help);
                return EXIT_FAILURE;
        }
    }
    if (argc > optind) {
        fprintf(stderr, help);
        return EXIT_FAILURE;
    }

    const useconds_t delay = 1e6 / rate;
    srand(seed);

    double level1 = 20.0, spike1 = 0.0;  // metrics state, series 1
    double level2 = 20.0, spike2 = 0.0;  // metrics state, series 2
    const double rmin = output_negative ? -100.0 : 0.0;  // -R range
    const double rmax = 100.0;

    for (unsigned int n = 0;; n += 5) {
        double wave1;
        if (metrics)
            wave1 = metric_sample(&level1, &spike1);
        else if (super_random)
            wave1 = rand_range(rmin, rmax);
        else
            wave1 = (sin(n * M_PI / 180) * 5) + (output_negative ? 0 : 5);
        buffer_pos += sprintf(buffer + buffer_pos, "%.1f\n", wave1);
        if (add_garbage && rand() <= RAND_MAX / 5)
            buffer_pos += sprintf(buffer + buffer_pos, "garbage ");
        if (two_waves) {
            double wave2;
            if (metrics)
                wave2 = metric_sample(&level2, &spike2);
            else if (super_random)
                wave2 = rand_range(rmin, rmax);
            else
                wave2 = (cos(n * M_PI / 180) * 5) + (output_negative ? 0 : 5);
            buffer_pos += sprintf(buffer + buffer_pos, "%.1f\n", wave2);
            if (add_garbage && rand() <= RAND_MAX / 5)
                buffer_pos += sprintf(buffer + buffer_pos, "garbage ");
        }
        if (chunked) {
            size_t send_pos = 0;
            while (buffer_pos - send_pos >= 16) {
                const size_t bytes_to_send = 1 + rand() % 16;  // 1..16
                const ssize_t bytes_sent =
                    write(STDOUT_FILENO, buffer + send_pos, bytes_to_send);
                usleep(50);  // let ttyplot read this before proceeding
                if (bytes_sent > 0)
                    send_pos += bytes_sent;
            }
            if (send_pos > 0 && send_pos < buffer_pos)
                memmove(buffer, buffer + send_pos, buffer_pos - send_pos);
            buffer_pos -= send_pos;
        } else {
            const ssize_t bytes_sent = write(STDOUT_FILENO, buffer, buffer_pos);
            if ((bytes_sent > 0) && ((size_t)bytes_sent < buffer_pos))
                memmove(buffer, buffer + bytes_sent, buffer_pos - bytes_sent);
            if (bytes_sent > 0)
                buffer_pos -= bytes_sent;
        }
        usleep(delay);
    }

    return 0;
}
