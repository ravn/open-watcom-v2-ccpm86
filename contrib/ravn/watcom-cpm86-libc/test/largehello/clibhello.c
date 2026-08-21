/* clibhello.c -- LARGE-model clib smoke test: exercises stdio (printf) + the
 * FAR heap (malloc, active because -ml defines __BIG_DATA__) + argv, all
 * through the real clibl.lib / cstartlm.obj -- the M1b deliverable. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    char *buf = malloc(64);
    int i;
    if (!buf) { printf("malloc FAILED\n"); return 1; }
    strcpy(buf, "far-heap OK");
    printf("clibl large model: argc=%d msg=%s\n", argc, buf);
    for (i = 0; i < argc; i++)
        printf("argv[%d]=%s\n", i, argv[i]);
    free(buf);
    return 0;
}
