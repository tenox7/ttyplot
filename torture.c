//
// torture ttyplot
//
// License: Apache 2.0
//

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

int main(int argc, char *argv[]) {
    (void) argv;
    unsigned int n;

    for(n=0;;n+=5) {
        printf("%.1f\n", (sin(n*M_PI/180)*5)+5);
        if(argc==2)
            printf("%.1f\n", (cos(n*M_PI/180)*5)+5);
        fflush(stdout);
        usleep(10000);
    }

    return 0;
}
