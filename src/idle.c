#include "glb.h"
#include "idle.h"

static int count = 0;

void idle_main()
{
    count++;
    if (count / 100000000) {
        printf("in IDLE\n");
        count = 0;
    }
}






