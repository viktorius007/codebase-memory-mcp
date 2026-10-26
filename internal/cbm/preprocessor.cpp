// Unity build: include simplecpp implementation directly since CGo only
// compiles .cpp files from the immediate package directory, not subdirs.
#include "vendored/simplecpp/simplecpp.cpp"

#include "preprocessor.h"
#include "vendored/simplecpp/simplecpp.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>

extern "C" {

static bool has_preprocessor_work(const char *source, int source_len) {
    if (!source || source_len <= 0) {
        return false;
    }
    for (int i = 0; i < source_len - 1; i++) {
        if (source[i] == '#') {
            // Skip whitespace after #
            int j = i + 1;
            while (j < source_len && (source[j] == ' ' || source[j] == '\t'))
                j++;
            int remaining = source_len - j;
            if (remaining >= 6 && strncmp(source + j, "define", 6) == 0) {
                return true;
            }
            if (remaining >= 5 && strncmp(source + j, "ifdef", 5) == 0) {
                return true;
            }
            if (remaining >= 6 && strncmp(source + j, "ifndef", 6) == 0) {
                return true;
            }
            if (remaining >= 3 && strncmp(source + j, "if ", 3) == 0) {
                return true;
            }
        }
    }
    return false;
}

// ── Export-macro candidates (#1989) ─────────────────────────────────────────
// Build systems define symbol-export macros empty on the compiler command line
// (UE's UBT: `/D "MODULE_API="`; CMake generate_export_header: `<lib>_EXPORT`),
// so they never appear as #define lines in the source. tree-sitter has no
// preprocessor state either, and `class MODULE_API Foo` misparses with the
// macro token as the type name (worse: enums and free functions are lost to
// ERROR regions entirely). To mirror the real compile line, the preprocessed
// second pass predefines the conventional export-macro-shaped identifiers found
// in the file as empty. The shape is deliberately narrow — ALL_CAPS identifier
// ending in a known export suffix, with a non-trivial prefix — and the list is
// capped, so ordinary all-caps identifiers cannot be swept in wholesale.
static const char *kExportMacroSuffixes[] = {"_API",       "_EXPORT",     "_IMPORT",
                                             "_DLLEXPORT", "_DEPRECATED", NULL};

static bool is_export_macro_shape(const char *id, size_t len) {
    if (id[0] < 'A' || id[0] > 'Z') {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = id[i];
        bool upper = c >= 'A' && c <= 'Z';
        bool digit = c >= '0' && c <= '9';
        if (!upper && !digit && c != '_') {
            return false;
        }
    }
    for (int s = 0; kExportMacroSuffixes[s]; s++) {
        size_t slen = strlen(kExportMacroSuffixes[s]);
        // Require at least two prefix characters before the suffix so a bare
        // "X_API"-style token (single-letter, easily a genuine symbol) stays out.
        if (len >= slen + 2 && strncmp(id + len - slen, kExportMacroSuffixes[s], slen) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_identifier_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool is_identifier_char(char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

// C++ raw string literals (R"delim(...)delim"). Returns the index one past
// the closing quote, or -1 when the form cannot be confidently recognized
// (unterminated, malformed delimiter) — the caller then STOPS collecting so
// the mis-modeled literal cannot poison the scan state of the rest of the
// file (#1989 review).
// The index of the `(` ending the delimiter of a raw string whose quote is at
// `quote`, or -1 when no valid delimiter ends there.
static int raw_string_delimiter_end(const char *source, int source_len, int quote) {
    int j = quote + 1;
    int delim_start = j;
    while (j < source_len) {
        char c = source[j];
        if (c == '(') {
            break;
        }
        // Delimiter chars: the standard d-char set excludes space, parens,
        // backslash, and the line breaks — a double quote IS a legal d-char
        // (R"""(...)""" compiles), so it must not end the delimiter (#1989
        // review round 3).
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ')' || c == '\\' ||
            j - delim_start >= CBM_SOURCE_RAW_DELIM_MAX) {
            return -1;
        }
        j++;
    }
    return j < source_len && source[j] == '(' ? j : -1;
}

static int skip_raw_string(const char *source, int source_len, int quote) {
    int delim_start = quote + 1;
    int j = raw_string_delimiter_end(source, source_len, quote);
    if (j < 0) {
        return -1;
    }
    int delim_len = j - delim_start;
    j++; // past the opening "("
    while (j < source_len) {
        if (source[j] == ')') {
            int k = j + 1;
            int m = 0;
            while (m < delim_len && k + m < source_len &&
                   source[k + m] == source[delim_start + m]) {
                m++;
            }
            if (m == delim_len && k + m < source_len && source[k + m] == '"') {
                return k + m + 1;
            }
        }
        j++;
    }
    return -1; // unterminated raw string: uncertain
}

// If the identifier just scanned is a raw-string prefix (R, LR, uR, UR, u8R)
// immediately followed by a quote, return skip_raw_string(...). Returns 0 for
// an ordinary identifier (including an identifier merely ENDING in R — only a
// standalone prefix token starts a raw string) and -1 on an unrecognizable
// raw form.
static bool is_raw_string_prefix(const char *id, size_t idlen) {
    static const char *prefixes[] = {"R", "LR", "uR", "UR", "u8R", NULL};
    for (int p = 0; prefixes[p]; p++) {
        if (idlen == strlen(prefixes[p]) && strncmp(id, prefixes[p], idlen) == 0) {
            return true;
        }
    }
    return false;
}

static int raw_string_after_prefix(const char *source, int source_len, int start, int end) {
    if (!is_raw_string_prefix(source + start, (size_t)(end - start)) || end >= source_len ||
        source[end] != '"') {
        return 0;
    }
    return skip_raw_string(source, source_len, end);
}

// Advance past comments and string/char literals so only real code tokens
// consume the candidate budget (#1989 review). Handles line-spliced //
// comments (a backslash before the newline continues the comment — line
// splicing happens before comment recognition) and, via the identifier-scan
// hook in the callers, C++ raw string literals. A digit separator (1'000'000)
// can still over-skip — that fails safe, toward NOT collecting a candidate.
static int skip_non_code(const char *source, int source_len, int i) {
    while (i < source_len) {
        char c = source[i];
        if (c == '/' && i + 1 < source_len && source[i + 1] == '/') {
            i += 2;
            for (;;) {
                while (i < source_len && source[i] != '\n') {
                    i++;
                }
                if (i >= source_len) {
                    break;
                }
                // Line splice: a backslash right before the newline (allowing
                // \r\n) means the comment continues on the next line.
                int back = i - 1;
                if (back >= 0 && source[back] == '\r') {
                    back--;
                }
                if (back >= 0 && source[back] == '\\') {
                    i++; // spliced: keep consuming inside the comment
                    continue;
                }
                i++; // real end-of-comment newline
                break;
            }
        } else if (c == '/' && i + 1 < source_len && source[i + 1] == '*') {
            i += 2;
            while (i + 1 < source_len && !(source[i] == '*' && source[i + 1] == '/')) {
                i++;
            }
            if (i + 1 < source_len) {
                i += 2; // past the closing "*/"
            } else {
                i = source_len; // unterminated comment runs to EOF
            }
        } else if (c == '"' || c == '\'') {
            char quote = c;
            i++;
            while (i < source_len) {
                if (source[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (source[i] == quote) {
                    i++;
                    break;
                }
                i++;
            }
        } else {
            return i;
        }
    }
    return i;
}

// Count-only scan used by the early gate (no storage).
static bool has_export_macro_candidates(const char *source, int source_len) {
    if (!source || source_len <= 0) {
        return false;
    }
    int i = 0;
    while (i < source_len) {
        i = skip_non_code(source, source_len, i);
        if (i >= source_len) {
            break;
        }
        if (!is_identifier_start(source[i])) {
            i++;
            continue;
        }
        int start = i;
        while (i < source_len && is_identifier_char(source[i])) {
            i++;
        }
        // A raw-string prefix consumes everything up to its closing quote;
        // an unrecognizable raw form poisons the scan state, so fail toward
        // NOT running the second pass (raw behavior preserved).
        int raw = raw_string_after_prefix(source, source_len, start, i);
        if (raw < 0) {
            return false;
        }
        if (raw > 0) {
            i = raw;
            continue;
        }
        if (is_export_macro_shape(source + start, (size_t)(i - start))) {
            return true;
        }
    }
    return false;
}

static int collect_export_macro_candidates(const char *source, int source_len,
                                           char (*out)[CBM_EXPORT_MACRO_NAME_MAX], int max_out) {
    if (!source || source_len <= 0 || !out || max_out <= 0) {
        return 0;
    }
    int stored = 0;
    int i = 0;
    while (i < source_len) {
        i = skip_non_code(source, source_len, i);
        if (i >= source_len) {
            break;
        }
        if (!is_identifier_start(source[i])) {
            i++;
            continue;
        }
        int start = i;
        while (i < source_len && is_identifier_char(source[i])) {
            i++;
        }
        // Raw string (see has_export_macro_candidates): skip its body, or stop
        // collecting entirely when the form cannot be confidently recognized —
        // continuing would mis-tokenize the rest of the file and could both
        // mint phantom candidates and mask real ones (#1989 review).
        int raw = raw_string_after_prefix(source, source_len, start, i);
        if (raw < 0) {
            break;
        }
        if (raw > 0) {
            i = raw;
            continue;
        }
        size_t len = (size_t)(i - start);
        /* Length gate FIRST: the dedup below reads out[k][0..len] against rows
         * of CBM_EXPORT_MACRO_NAME_MAX bytes — an over-long candidate must be
         * rejected before any compare touches them. */
        if (len >= CBM_EXPORT_MACRO_NAME_MAX) {
            continue;
        }
        if (!is_export_macro_shape(source + start, len)) {
            continue;
        }
        bool dup = false;
        for (int k = 0; k < stored; k++) {
            if (strncmp(out[k], source + start, len) == 0 && out[k][len] == '\0') {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        if (stored >= max_out) {
            break; // bounded: stop collecting once the cap is reached
        }
        memcpy(out[stored], source + start, len);
        out[stored][len] = '\0';
        stored++;
    }
    return stored;
}

int cbm_export_macro_candidates(const char *source, int source_len,
                                char (*out)[CBM_EXPORT_MACRO_NAME_MAX], int max_out) {
    return collect_export_macro_candidates(source, source_len, out, max_out);
}

static int count_expanded_lines(const std::string &text) {
    int count = 1;
    for (char c : text) {
        if (c == '\n') {
            count++;
        }
    }
    return count;
}

static bool parse_line_directive(const char *line, size_t len, uint32_t *out_line,
                                 std::string *out_file) {
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    if (i >= len || line[i++] != '#') {
        return false;
    }
    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    static const char prefix[] = "line";
    if (i + sizeof(prefix) - 1 > len || strncmp(line + i, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }
    i += sizeof(prefix) - 1;
    if (i >= len || (line[i] != ' ' && line[i] != '\t')) {
        return false;
    }
    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    if (i >= len || line[i] < '0' || line[i] > '9') {
        return false;
    }
    uint64_t parsed_line = 0;
    while (i < len && line[i] >= '0' && line[i] <= '9') {
        parsed_line = parsed_line * 10u + (uint64_t)(line[i] - '0');
        if (parsed_line > UINT32_MAX) {
            return false;
        }
        i++;
    }
    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    if (i >= len || line[i++] != '"') {
        return false;
    }
    size_t file_start = i;
    while (i < len && line[i] != '"') {
        i++;
    }
    if (i >= len) {
        return false;
    }
    *out_line = (uint32_t)parsed_line;
    *out_file = std::string(line + file_start, i - file_start);
    return true;
}

// True when `prev` and `tok` touched in the source text: same file and line,
// `prev` ending exactly where `tok` starts (simplecpp's own adjacency test),
// and neither produced by a macro expansion (expansion tokens carry the call
// site's location, so their columns prove nothing).
static bool tokens_source_adjacent(const simplecpp::Token *prev, const simplecpp::Token *tok) {
    if (!prev->macro.empty() || !tok->macro.empty() || !prev->location.sameline(tok->location)) {
        return false;
    }
    const std::string &s = prev->str();
    if (s.find_first_of("\r\n") != std::string::npos) {
        return false;
    }
    return prev->location.col + s.size() == tok->location.col;
}

// The expanded buffer in the source's own spelling. Mirrors
// simplecpp::TokenList::stringify() line for line — the same newlines and
// #line directives, so build_line_map and every line-based remap are unchanged —
// except that tokens which touched in the source stay touching. stringify()
// joins every same-line pair with one space, so the second pass spelled a
// multi-token name differently from the raw pass: the explicit specialization
// `template<> struct bitonic<int64_t, AVX2>` became `bitonic < int64_t , AVX2 >`,
// and every QN built from that text (defs adopted by the #961 rescue, call
// scopes, C-LSP class scopes) minted a second node for the same definition, the
// QN dedup (strcmp) missing it. A gap that held whitespace or a comment is still
// one space, as before.
static std::string stringify_source_spacing(const simplecpp::TokenList &tokens) {
    std::ostringstream ret;
    simplecpp::Location loc;
    loc.line = 1;
    for (const simplecpp::Token *tok = tokens.cfront(); tok; tok = tok->next) {
        if (tok->location.line < loc.line || tok->location.fileIndex != loc.fileIndex) {
            ret << "\n#line " << tok->location.line << " \"" << tokens.file(tok->location)
                << "\"\n";
            loc = tok->location;
        }
        while (tok->location.line > loc.line) {
            ret << '\n';
            loc.line++;
        }
        const simplecpp::Token *prev = tok->previous;
        if (prev && prev->location.sameline(tok->location) && !tokens_source_adjacent(prev, tok)) {
            ret << ' ';
        }
        ret << tok->str();
        loc.adjust(tok->str());
    }
    return ret.str();
}

bool cbm_preprocessor_line_continues(const char *line, int len) {
    int end = len;
    while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r')) {
        end--;
    }
    return end > 0 && line[end - 1] == '\\';
}

// The index of the first character at or after `i` that is neither a blank
// nor inside a block comment closed on this line. Translation phase 3 turns
// each comment into one space before phase 4 sees directives, so such a
// comment counts as a blank. A comment left open or a `//` comment ends the
// line's tokens: `len` is returned.
static size_t skip_blanks_and_comments(const char *line, size_t len, size_t i) {
    while (i < len) {
        if (line[i] == ' ' || line[i] == '\t') {
            i++;
        } else if (i + 1 < len && line[i] == '/' && line[i + 1] == '*') {
            size_t close = i + 2;
            while (close + 1 < len && !(line[close] == '*' && line[close + 1] == '/')) {
                close++;
            }
            if (close + 1 >= len) {
                return len;
            }
            i = close + 2;
        } else if (i + 1 < len && line[i] == '/' && line[i + 1] == '/') {
            return len;
        } else {
            break;
        }
    }
    return i;
}

// Whether `line` (no newline) is a line-number directive simplecpp honours:
// `#line 40 "parse.y"`, `#line 40`, or the linemarker `# 40 "parse.y"`, with
// any blank between its tokens possibly a closed block comment
// (`/* c */ #line 40`).
static bool is_source_line_directive(const char *line, size_t len) {
    size_t i = skip_blanks_and_comments(line, len, 0);
    if (i >= len || line[i++] != '#') {
        return false;
    }
    i = skip_blanks_and_comments(line, len, i);
    static const char keyword[] = "line";
    const size_t keyword_len = sizeof(keyword) - 1;
    if (i + keyword_len < len && strncmp(line + i, keyword, keyword_len) == 0) {
        const size_t after = skip_blanks_and_comments(line, len, i + keyword_len);
        if (after > i + keyword_len) {
            i = after;
        }
    }
    return i < len && line[i] >= '0' && line[i] <= '9';
}

// Whether the quote at `line[quote]` opens a raw string: a prefix token
// (R, LR, uR, UR, u8R) before it.
static bool opens_raw_string(const char *line, int quote) {
    int token = quote;
    while (token > 0 && is_identifier_char(line[token - 1])) {
        token--;
    }
    return is_raw_string_prefix(line + token, (size_t)(quote - token));
}

// Whether the apostrophe at `line[quote]` separates digits (C++14 `1'000`)
// rather than opening a character literal: it continues a number.
static bool is_digit_separator(const char *line, int quote) {
    int token = quote;
    while (token > 0 && (is_identifier_char(line[token - 1]) || line[token - 1] == '.')) {
        token--;
    }
    return token < quote && line[token] >= '0' && line[token] <= '9';
}

// One step of the lexer from code at `line[k]`: the mode it enters and the
// bytes the step reads.
static int lex_from_code(cbm_source_lexer_t *lexer, const char *line, int len, int k) {
    const char c = line[k];
    const char next = k + 1 < len ? line[k + 1] : '\0';
    if (c == '/' && next == '*') {
        lexer->mode = CBM_SOURCE_BLOCK_COMMENT;
        return 2;
    }
    if (c == '/' && next == '/') {
        lexer->mode = CBM_SOURCE_LINE_COMMENT;
        return 1;
    }
    const int open =
        c == '"' && opens_raw_string(line, k) ? raw_string_delimiter_end(line, len, k) : -1;
    if (open > 0) {
        snprintf(lexer->raw_close, sizeof(lexer->raw_close), ")%.*s\"", open - k - 1, line + k + 1);
        lexer->mode = CBM_SOURCE_RAW;
        return open - k + 1;
    }
    if (c == '"') {
        lexer->mode = CBM_SOURCE_STRING;
    } else if (c == '\'' && !is_digit_separator(line, k)) {
        lexer->mode = CBM_SOURCE_CHAR;
    }
    return 1;
}

bool cbm_source_lex_line(cbm_source_lexer_t *lexer, const char *line, int len, bool continues,
                         char *blank_out) {
    bool code = false;
    for (int k = 0; k < len;) {
        bool comment =
            lexer->mode == CBM_SOURCE_BLOCK_COMMENT || lexer->mode == CBM_SOURCE_LINE_COMMENT;
        int unit = 1;
        const char c = line[k];
        switch (lexer->mode) {
        case CBM_SOURCE_CODE:
            unit = lex_from_code(lexer, line, len, k);
            comment =
                lexer->mode == CBM_SOURCE_BLOCK_COMMENT || lexer->mode == CBM_SOURCE_LINE_COMMENT;
            break;
        case CBM_SOURCE_BLOCK_COMMENT:
            if (c == '*' && k + 1 < len && line[k + 1] == '/') {
                lexer->mode = CBM_SOURCE_CODE;
                unit = 2;
            }
            break;
        case CBM_SOURCE_STRING:
        case CBM_SOURCE_CHAR:
            if (c == '\\') {
                unit = 2;
            } else if (c == (lexer->mode == CBM_SOURCE_STRING ? '"' : '\'')) {
                lexer->mode = CBM_SOURCE_CODE;
            }
            break;
        case CBM_SOURCE_RAW: {
            const size_t close_len = std::strlen(lexer->raw_close);
            if ((size_t)(len - k) >= close_len &&
                std::strncmp(line + k, lexer->raw_close, close_len) == 0) {
                lexer->mode = CBM_SOURCE_CODE;
                unit = (int)close_len;
            }
            break;
        }
        default: // CBM_SOURCE_LINE_COMMENT: to the end of the line
            break;
        }
        unit = std::min(unit, len - k);
        for (int b = k; !comment && b < k + unit; b++) {
            code = code || (line[b] != ' ' && line[b] != '\t' && line[b] != '\r');
            if (blank_out) {
                blank_out[b] = ' ';
            }
        }
        k += unit;
    }
    if (lexer->mode != CBM_SOURCE_BLOCK_COMMENT && lexer->mode != CBM_SOURCE_RAW && !continues) {
        lexer->mode = CBM_SOURCE_CODE;
    }
    return code;
}

// The source with every line-number directive blanked, its newlines and its
// comments kept (a directive continued with a backslash blanks each of its
// lines; a line that continues another line, or starts inside a comment or a
// raw string, is never a directive).
//
// A directive re-labels the lines after it: under `#line 40 "parse.y"`, which
// generated bison, flex and lemon parsers carry, the expanded buffer names
// parse.y, line 40 on, for code written in this file. build_line_map then took
// that code for another file's -- the test that drops an included header's
// rows -- and the preprocessed pass dropped every call it found there; under
// the same-file form `#line 7 "<this file>"` its calls took the declared line.
// Every row this file supplies to the expanded buffer must map to the line it
// is written on, the line the raw parse and every definition use; only a
// header read through a named include directory supplies rows of another file.
//
// The scan reads comments and literals as the preprocessor does
// (cbm_source_lex_line). `# 2 "notes"` inside a block comment is comment
// text: blanked, it took the comment's closer with it and hid the code after
// it. `#line 20 "gen.y"` followed by a comment opener keeps the comment:
// blanked whole, the commented-out lines after it became code.
static std::string without_line_directives(const char *source, int source_len) {
    std::string out(source, (size_t)source_len);
    cbm_source_lexer_t lexer = {};
    bool continued = false; // this line continues the one above
    bool blanking = false;  // ... and that one was a directive being blanked
    for (size_t start = 0; start < out.size();) {
        size_t end = out.find('\n', start);
        if (end == std::string::npos) {
            end = out.size();
        }
        size_t content_end = end;
        if (content_end > start && out[content_end - 1] == '\r') {
            content_end--;
        }
        const char *line = source + start;
        const int len = (int)(content_end - start);
        const bool continues = cbm_preprocessor_line_continues(line, len);
        if (!continued) {
            blanking = lexer.mode == CBM_SOURCE_CODE && is_source_line_directive(line, (size_t)len);
        }
        cbm_source_lex_line(&lexer, line, len, continues, blanking ? &out[start] : nullptr);
        continued = continues;
        start = end + 1;
    }
    return out;
}

static bool build_line_map(const std::string &expanded, const std::string &main_file,
                           uint32_t *original_line_by_expanded_line,
                           uint8_t *belongs_to_main_file) {
    std::string current_file = main_file;
    uint32_t current_line = 1;
    int expanded_line = 1;
    size_t line_start = 0;

    while (line_start <= expanded.size()) {
        size_t line_end = expanded.find('\n', line_start);
        if (line_end == std::string::npos) {
            line_end = expanded.size();
        }

        uint32_t directive_line = 0;
        std::string directive_file;
        if (parse_line_directive(expanded.c_str() + line_start, line_end - line_start,
                                 &directive_line, &directive_file)) {
            current_file = directive_file;
            current_line = directive_line;
            original_line_by_expanded_line[expanded_line] = 0;
            belongs_to_main_file[expanded_line] = 0;
        } else {
            original_line_by_expanded_line[expanded_line] = current_line;
            belongs_to_main_file[expanded_line] = current_file == main_file ? 1 : 0;
            if (current_line < UINT32_MAX) {
                current_line++;
            }
        }

        if (line_end == expanded.size()) {
            break;
        }
        line_start = line_end + 1;
        expanded_line++;
    }
    return true;
}

CBMPreprocessedSource *cbm_preprocess_with_map(const char *source, int source_len,
                                               const char *filename, const char **extra_defines,
                                               const char **include_paths, int cpp_mode) {
    // Run the second pass when there are directives to evaluate OR when the file
    // carries export-macro-shaped identifiers to predefine empty (#1989) — a UE
    // plugin header with only `#pragma once` + `#include` lines still needs it.
    if (!has_preprocessor_work(source, source_len) &&
        !has_export_macro_candidates(source, source_len)) {
        return NULL; // NULL = no expansion needed, use original
    }

    try {
        simplecpp::DUI dui;
        if (extra_defines) {
            for (int i = 0; extra_defines[i]; i++)
                dui.defines.push_back(extra_defines[i]);
        }
        // Predefine collected export-macro candidates as empty, mirroring the
        // real compile command line (`/D "MODULE_API="`). Names already provided
        // by the caller win — never override an explicit define.
        char export_cands[CBM_EXPORT_MACRO_MAX][CBM_EXPORT_MACRO_NAME_MAX];
        int export_cand_count =
            collect_export_macro_candidates(source, source_len, export_cands, CBM_EXPORT_MACRO_MAX);
        for (int i = 0; i < export_cand_count; i++) {
            bool provided = false;
            for (std::list<std::string>::const_iterator it = dui.defines.begin();
                 it != dui.defines.end(); ++it) {
                const std::string &def = *it;
                size_t eq = def.find('=');
                std::string name = (eq == std::string::npos) ? def : def.substr(0, eq);
                if (name == export_cands[i]) {
                    provided = true;
                    break;
                }
            }
            if (!provided) {
                dui.defines.push_back(std::string(export_cands[i]) + "=");
            }
        }
        if (include_paths) {
            for (int i = 0; include_paths[i]; i++)
                dui.includePaths.push_back(include_paths[i]);
        }
        // Headers come only from the directories the caller names. `filename`
        // is a name inside the indexed repository, not a path on this machine:
        // simplecpp's own lookup next to the including file resolved it against
        // the process's working directory, so the expansion -- its calls, their
        // lines, the defs it rescues -- followed wherever the indexer happened
        // to run and whatever that directory held at the time. An absolute
        // #include names a file outside the input just the same.
        dui.includePathsOnly = true;
        dui.std = cpp_mode ? "c++17" : "c11";

        std::string src = without_line_directives(source, source_len);
        std::istringstream istr(src);
        std::vector<std::string> files;
        files.push_back(filename ? filename : "<input>");

        simplecpp::TokenList rawtokens(istr, files, files[0]);
        simplecpp::TokenList output(files);
        simplecpp::FileDataCache filedata = simplecpp::load(rawtokens, files, dui);

        simplecpp::preprocess(output, rawtokens, files, filedata, dui);

        std::string result = stringify_source_spacing(output);

        // Clean up loaded file data
        simplecpp::cleanup(filedata);

        CBMPreprocessedSource *pp = (CBMPreprocessedSource *)calloc(1, sizeof(*pp));
        if (!pp) {
            return NULL;
        }
        int line_count = count_expanded_lines(result);
        pp->source = (char *)malloc(result.size() + 1);
        pp->original_line_by_expanded_line =
            (uint32_t *)calloc((size_t)line_count + 1u, sizeof(uint32_t));
        pp->belongs_to_main_file = (uint8_t *)calloc((size_t)line_count + 1u, sizeof(uint8_t));
        pp->expanded_line_count = line_count;
        if (!pp->source || !pp->original_line_by_expanded_line || !pp->belongs_to_main_file) {
            cbm_preprocessed_source_free(pp);
            return NULL;
        }
        memcpy(pp->source, result.c_str(), result.size() + 1);
        if (!build_line_map(result, files[0], pp->original_line_by_expanded_line,
                            pp->belongs_to_main_file)) {
            cbm_preprocessed_source_free(pp);
            return NULL;
        }
        return pp;
    } catch (...) {
        // Graceful fallback: return NULL = use original source
        return NULL;
    }
}

char *cbm_preprocess(const char *source, int source_len, const char *filename,
                     const char **extra_defines, const char **include_paths, int cpp_mode) {
    CBMPreprocessedSource *pp = cbm_preprocess_with_map(source, source_len, filename, extra_defines,
                                                        include_paths, cpp_mode);
    if (!pp) {
        return NULL;
    }
    char *out = pp->source;
    pp->source = NULL;
    cbm_preprocessed_source_free(pp);
    return out;
}

void cbm_preprocess_free(char *expanded) {
    free(expanded);
}

void cbm_preprocessed_source_free(CBMPreprocessedSource *pp) {
    if (!pp) {
        return;
    }
    free(pp->source);
    free(pp->original_line_by_expanded_line);
    free(pp->belongs_to_main_file);
    free(pp);
}

} // extern "C"
