/* A line-number directive after a closed block comment on the same line is
 * a directive: translation phase 3 turns the comment into one space before
 * phase 4 reads directives. The graph keeps the file's own line numbers, so
 * the directive must be blanked like a bare `#line`. The call main -> answer
 * exists only through the expansion of ASK, so the graph takes it from the
 * preprocessed text and its line map: an unblanked directive would map it to
 * gen.y, and the main-file check would drop it. */
#include <stdio.h>
#define ASK answer()

static int answer(void) { return 42; }

/* generated */ #line 900 "gen.y"
static int unused_marker;

# /* marker */ line 950 "gen.y"
int main(void) {
    printf("main at %d\n", __LINE__);
    return ASK != 42 + unused_marker;
}
