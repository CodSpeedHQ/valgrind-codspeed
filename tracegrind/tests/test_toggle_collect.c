#include "tracegrind.h"

static int work(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++)
        sum += i;
    return sum;
}

int main(void) {
    /* Collection on by default, do some traced work */
    int result = work(10);

    /* Toggle collection off */
    TRACEGRIND_TOGGLE_COLLECT;
    result += work(20);  /* not collected */

    /* Toggle collection back on */
    TRACEGRIND_TOGGLE_COLLECT;
    result += work(30);  /* collected again */

    return result == 0;
}
