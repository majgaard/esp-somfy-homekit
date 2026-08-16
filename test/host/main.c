#include <stdio.h>

int g_failures = 0;
int g_checks   = 0;

void run_somfy_rts_tests(void);
void run_shade_ctl_tests(void);

int main(void)
{
    printf("somfy_rts\n");
    run_somfy_rts_tests();
    printf("shade_ctl\n");
    run_shade_ctl_tests();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
