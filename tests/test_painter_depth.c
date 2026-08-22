#include "painter_depth.h"

#include <stdio.h>

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    const int count = 12;
    const float first = painter_depth(0, count);
    const float carrier = painter_depth(6, count);
    const float weapon = painter_depth(7, count);
    const float last = painter_depth(count - 1, count);

    check(first < 1.0f, "the first painter stays inside the far clip plane");
    check(weapon < carrier, "a later painter is nearer");
    check(last > 0.0f, "the last painter stays inside the near clip plane");
    check(painter_depth(0, 1) == 0.5f, "one painter lands at the depth range midpoint");

    if (failures) return 1;
    puts("painter depth: 4 checks, 0 failures");
    return 0;
}
