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
    "Usage: %s [-h] [-2]\n"
    "  -h       print this help message and exit\n"
    "  -2       output two waves\n";

const char optstring[] = "h2";

int main(int argc, char *argv[]) {
    int opt;
    bool two_waves = false;

    // Parse the command line.
    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch (opt) {
            case 'h':
                printf(help, argv[0]);
                return EXIT_SUCCESS;
            case '2':
                two_waves = true;
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

    for (unsigned int n=0; ; n+=5) {
        printf("%.1f\n", (sin(n*M_PI/180)*5)+5);
        if(two_waves)
            printf("%.1f\n", (cos(n*M_PI/180)*5)+5);
        fflush(stdout);
        usleep(10000);
    }

    return 0;
}
