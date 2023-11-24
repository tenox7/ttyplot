//
// stresstest ttyplot
//
// License: Apache 2.0
//

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

const char help[] =
    "Usage: %s [-h] [-2] [-r rate]\n"
    "  -h       print this help message and exit\n"
    "  -2       output two waves\n"
    "  -r rate  sample rate in samples/s (default: 100)\n";

const char optstring[] = "h2r:";

int main(int argc, char *argv[]) {
    int opt;
    bool two_waves = false;
    double rate = 100;

    // Parse the command line.
    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch (opt) {
            case 'h':
                printf(help, argv[0]);
                return EXIT_SUCCESS;
            case '2':
                two_waves = true;
                break;
            case 'r':
                rate = atof(optarg);
                break;
            default:
                fprintf(stderr, help, argv[0]);
                return EXIT_FAILURE;
        }
    }
    if (argc > optind) {
        fprintf(stderr, help, argv[0]);
        return EXIT_FAILURE;
    }

    const useconds_t delay = 1e6 / rate;

    for (unsigned int n=0; ; n+=5) {
        printf("%.1f\n", (sin(n*M_PI/180)*5)+5);
        if(two_waves)
            printf("%.1f\n", (cos(n*M_PI/180)*5)+5);
        fflush(stdout);
        usleep(delay);
    }

    return 0;
}
