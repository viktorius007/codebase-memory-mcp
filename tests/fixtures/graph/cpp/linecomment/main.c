/* A line-number directive after a closed block comment on the same line is
 * a directive: translation phase 3 turns the comment into one space before
 * phase 4 reads directives. The graph keeps the file's own line numbers, so
 * the directive must be blanked like a bare `#line`. The #define sends the
 * file through the preprocessed path, where the directive would otherwise
 * renumber every line after it. */
#include <stdio.h>
#define HELPER_FORMAT "helper at %d\n"

/* generated */ #line 900 "gen.y"
static int helper(void) { return printf(HELPER_FORMAT, __LINE__); }

# /* marker */ line 950 "gen.y"
int main(void) {
    printf("main at %d\n", __LINE__);
    return helper() < 0;
}
