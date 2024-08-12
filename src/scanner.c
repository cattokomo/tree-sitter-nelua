#include <stdbool.h>
#include <stdio.h>
#include <tree_sitter/parser.h>
#include <wctype.h>

/* Taken from tree-sitter-nelua */

enum TokenType {
    BLOCK_COMMENT_START,
    BLOCK_COMMENT_CONTENT,
    BLOCK_COMMENT_END,

    BLOCK_PREPROCESS_START,
    BLOCK_PREPROCESS_CONTENT,
    BLOCK_PREPROCESS_END,

    PREPROCESS_EXPR_START,
    PREPROCESS_EXPR_END,

    PREPROCESS_NAME_START,
    PREPROCESS_NAME_END,

    PREPROCESS_INLINE_CONTENT,

    STRING_START,
    STRING_CONTENT,
    STRING_END,
};

static inline void consume(TSLexer *lexer) { lexer->advance(lexer, false); }
static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static inline bool consume_char(char c, TSLexer *lexer) {
    if (lexer->lookahead != c) {
        return false;
    }

    consume(lexer);
    return true;
}

static inline uint8_t consume_and_count_char(char c, TSLexer *lexer) {
    uint8_t count = 0;
    while (lexer->lookahead == c) {
        ++count;
        consume(lexer);
    }
    return count;
}

static inline void skip_whitespaces(TSLexer *lexer) {
    while (iswspace(lexer->lookahead)) {
        skip(lexer);
    }
}

void *tree_sitter_nelua_external_scanner_create() { return NULL; }
void tree_sitter_nelua_external_scanner_destroy(void *payload) {}

char ending_char = 0;
uint8_t level_count = 0;
char preproc_end_char = 0;

static inline void reset_state() {
    ending_char = 0;
    level_count = 0;
    preproc_end_char = 0;
}

unsigned tree_sitter_nelua_external_scanner_serialize(void *payload,
                                                      char *buffer) {
    buffer[0] = ending_char;
    buffer[1] = level_count;
    buffer[2] = preproc_end_char;
    return 3;
}

void tree_sitter_nelua_external_scanner_deserialize(void *payload,
                                                    const char *buffer,
                                                    unsigned length) {
    if (length == 0)
        return;
    ending_char = buffer[0];
    if (length == 1)
        return;
    level_count = buffer[1];
    if (length == 2)
        return;
    preproc_end_char = buffer[2];
}

static bool scan_block_start(TSLexer *lexer) {
    if (consume_char('[', lexer)) {
        uint8_t level = consume_and_count_char('=', lexer);

        if (consume_char('[', lexer)) {
            level_count = level;
            return true;
        }
    }

    return false;
}

static bool scan_block_end(TSLexer *lexer) {
    if (consume_char(']', lexer)) {
        uint8_t level = consume_and_count_char('=', lexer);

        if (level_count == level && consume_char(']', lexer)) {
            return true;
        }
    }

    return false;
}

static bool scan_block_content(TSLexer *lexer) {
    while (lexer->lookahead != 0) {
        if (lexer->lookahead == ']') {
            lexer->mark_end(lexer);

            if (scan_block_end(lexer)) {
                return true;
            }
        } else {
            consume(lexer);
        }
    }

    return false;
}

static bool scan_comment_start(TSLexer *lexer) {
    if (consume_char('-', lexer) && consume_char('-', lexer)) {
        lexer->mark_end(lexer);

        if (scan_block_start(lexer)) {
            lexer->mark_end(lexer);
            lexer->result_symbol = BLOCK_COMMENT_START;
            return true;
        }
    }

    return false;
}

static bool scan_comment_content(TSLexer *lexer) {
    if (ending_char == 0) { // block comment
        if (scan_block_content(lexer)) {
            lexer->result_symbol = BLOCK_COMMENT_CONTENT;
            return true;
        }

        return false;
    }

    while (lexer->lookahead != 0) {
        if (lexer->lookahead == ending_char) {
            reset_state();
            lexer->result_symbol = BLOCK_COMMENT_CONTENT;
            return true;
        }

        consume(lexer);
    }

    return false;
}

static bool scan_preprocess_start(TSLexer *lexer) {
    if (consume_char('#', lexer) && consume_char('#', lexer)) {
        lexer->mark_end(lexer);

        if (scan_block_start(lexer)) {
            lexer->mark_end(lexer);
            lexer->result_symbol = BLOCK_PREPROCESS_START;
            return true;
        }
    }

    return false;
}

static bool scan_preprocess_content(TSLexer *lexer) {
    if (ending_char == 0) { // block preprocess
        if (scan_block_content(lexer)) {
            lexer->result_symbol = BLOCK_PREPROCESS_CONTENT;
            return true;
        }

        return false;
    }

    while (lexer->lookahead != 0) {
        if (lexer->lookahead == ending_char) {
            reset_state();
            lexer->result_symbol = BLOCK_PREPROCESS_CONTENT;
            return true;
        }

        consume(lexer);
    }

    return false;
}

static bool scan_string_start(TSLexer *lexer) {
    if (lexer->lookahead == '"' || lexer->lookahead == '\'') {
        ending_char = lexer->lookahead;
        consume(lexer);
        return true;
    }

    if (scan_block_start(lexer)) {
        return true;
    }

    return false;
}

static bool scan_string_end(TSLexer *lexer) {
    if (ending_char == 0) { // block string
        return scan_block_end(lexer);
    }

    if (consume_char(ending_char, lexer)) {
        return true;
    }

    return false;
}

static bool scan_string_content(TSLexer *lexer) {
    if (ending_char == 0) { // block string
        return scan_block_content(lexer);
    }

    while (lexer->lookahead != '\n' && lexer->lookahead != 0 &&
           lexer->lookahead != ending_char) {
        if (consume_char('\\', lexer) && consume_char('z', lexer)) {
            while (iswspace(lexer->lookahead)) {
                consume(lexer);
            }
            continue;
        };

        if (lexer->lookahead == 0) {
            return true;
        }

        consume(lexer);
    }

    return true;
}

static bool scan_preprocess_inline_start(TSLexer *lexer) {
    if (consume_char('#', lexer)) {
        if (consume_char('[', lexer)) {
            preproc_end_char = ']';
            inline_preproc = true;
            lexer->mark_end(lexer);
            lexer->result_symbol = PREPROCESS_EXPR_START;
            return true;
        } else if (consume_char('|', lexer)) {
            preproc_end_char = '|';
            inline_preproc = true;
            lexer->mark_end(lexer);
            lexer->result_symbol = PREPROCESS_NAME_START;
            return true;
        }
    }

    return false;
}

static bool scan_preprocess_inline_end(TSLexer *lexer) {
    if (preproc_end_char != 0 && consume_char(preproc_end_char, lexer) &&
        consume_char('#', lexer)) {
        return true;
    }

    return false;
}

static bool scan_preprocess_inline_content(TSLexer *lexer) {
    if (!inline_preproc)
        return false;

    while (lexer->lookahead != 0) {
        if (lexer->lookahead == preproc_end_char) {
            lexer->mark_end(lexer);

            if (scan_preprocess_inline_end(lexer)) {
                return true;
            }
        } else {
            consume(lexer);
        }
    }

    return false;
}

bool tree_sitter_nelua_external_scanner_scan(void *payload, TSLexer *lexer,
                                             const bool *valid_symbols) {
    if (valid_symbols[STRING_END] && scan_string_end(lexer)) {
        reset_state();
        lexer->result_symbol = STRING_END;
        return true;
    }

    if (valid_symbols[STRING_CONTENT] && scan_string_content(lexer)) {
        lexer->result_symbol = STRING_CONTENT;
        return true;
    }

    if ((valid_symbols[BLOCK_COMMENT_END] ||
         valid_symbols[BLOCK_PREPROCESS_END]) &&
        ending_char == 0 && scan_block_end(lexer)) {
        reset_state();
        lexer->result_symbol = valid_symbols[BLOCK_COMMENT_END]
                                   ? BLOCK_COMMENT_END
                                   : BLOCK_PREPROCESS_END;
        return true;
    }

    if (!inline_preproc && valid_symbols[BLOCK_COMMENT_CONTENT] && scan_comment_content(lexer)) {
        return true;
    }

    if (!inline_preproc && valid_symbols[BLOCK_PREPROCESS_CONTENT] &&
        scan_preprocess_content(lexer)) {
        return true;
    }

    if ((valid_symbols[PREPROCESS_EXPR_END] ||
         valid_symbols[PREPROCESS_NAME_END]) &&
        scan_preprocess_inline_end(lexer)) {
        reset_state();
        lexer->result_symbol = valid_symbols[PREPROCESS_EXPR_END]
                                   ? PREPROCESS_EXPR_END
                                   : PREPROCESS_NAME_END;
        return true;
    }

    if (valid_symbols[PREPROCESS_INLINE_CONTENT] &&
        scan_preprocess_inline_content(lexer)) {
        return true;
    }

    skip_whitespaces(lexer);

    if (valid_symbols[STRING_START] && scan_string_start(lexer)) {
        lexer->result_symbol = STRING_START;
        return true;
    }

    if (valid_symbols[BLOCK_COMMENT_START] && scan_comment_start(lexer)) {
        return true;
    }

    if (valid_symbols[BLOCK_PREPROCESS_START] && scan_preprocess_start(lexer)) {
        lexer->result_symbol = BLOCK_PREPROCESS_START;
        return true;
    }

    if ((valid_symbols[PREPROCESS_EXPR_START] ||
         valid_symbols[PREPROCESS_NAME_START]) &&
        scan_preprocess_inline_start(lexer)) {
        lexer->result_symbol = valid_symbols[PREPROCESS_EXPR_START]
                                   ? PREPROCESS_EXPR_START
                                   : PREPROCESS_NAME_START;
        return true;
    }

    return false;
}
