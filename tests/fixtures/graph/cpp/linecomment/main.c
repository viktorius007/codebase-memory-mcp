/* A line-number directive after a closed block comment on the same line is
 * a directive: translation phase 3 turns the comment into one space before
 * phase 4 reads directives. The graph keeps the file's own line numbers, so
 * the directive must be blanked like a bare `#line`. `answer` and the call
 * main -> answer exist only through macro expansion, so the graph takes
 * them from the preprocessed text and its line map: an unblanked directive
 * would place them in gen.y. */
#include <stdio.h>
#define DEFINE_GETTER(name, value) static int name(void) { return value; }
#define CALL(fn) fn()

/* generated */ #line 900 "gen.y"
DEFINE_GETTER(answer, 42)

# /* marker */ line 950 "gen.y"
int main(void) {
    printf("answer at %d\n", __LINE__);
    return CALL(answer) != 42;
}
