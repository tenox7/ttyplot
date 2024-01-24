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
    "  stresstest [-2] [-c] [-g] [-n] [-r rate]\n"
    "  stresstest -h\n"
    "\n"
    "  -h       print this help message and exit\n"
    "  -2       output two waves\n"
    "  -c       randomly chunk the output\n"
    "  -g       occasionally output garbage\n"
    "  -n       output negative values\n"
    "  -r rate  sample rate in samples/s (default: 100)\n"
    "  -s seed  set random seed\n";

const char optstring[] = "h2cgnr:s:";

int main(int argc, char *argv[]) {
    char buffer[1024];
    size_t buffer_pos = 0;
    int opt;
    bool two_waves = false;
    bool chunked = false;
    bool add_garbage = false;
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
	    case 'n':
		output_negative = true;
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

    for (unsigned int n = 0;; n += 5) {
        buffer_pos +=
            sprintf(buffer + buffer_pos, "%.1f\n", (sin(n * M_PI / 180) * 5) + (output_negative ? 0 : 5));
        if (add_garbage && rand() <= RAND_MAX / 5)
            buffer_pos += sprintf(buffer + buffer_pos, "garbage ");
        if (two_waves) {
            buffer_pos +=
                sprintf(buffer + buffer_pos, "%.1f\n", (cos(n * M_PI / 180) * 5) + (output_negative ? 0 : 5));
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
