//
// torture ttyplot
//
// License: Apache 2.0
//

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main() {

    srand(time(NULL));

    for(;;) {
        printf("%d %d\n", rand()%100,rand()%100);
        fflush(stdout);
        //usleep(20000);
    }

    return 0;
}
