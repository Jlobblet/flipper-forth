#include <ctype.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>

#define NAME_MAX 16
#define DICT_SIZE 32768
#define DSTACK_SIZE 256
#define RSTACK_SIZE 256
#define FSTACK_SIZE 32
#define STRING_PAD_SIZE 1024

const char prelude[] = {
#embed "prelude.f"
};

// Types
// -----

// We use uintptr_t as cell type because it means that we can store both
// pointers and integers in the same size, which is very convenient
typedef uintptr_t cell;

// Forth uses all-zeroes as false and all-ones as true
#define F_FALSE ((cell)0)
#define F_TRUE ((cell)(-1))
// Forth bool - convert a C-like bool to a Forth-like one
#define FBOOL(val) ((val) ? F_TRUE : F_FALSE)

// Signed cells are needed for some binary operations (subtraction, comparisons)
typedef intptr_t scell;

// code_t is used for storing pointers to code rather than a raw void *
typedef void *code_t;

// Type to use for the floating-point stack and operands
#ifdef DOUBLE_AS_REAL
typedef double real;
#define PRIREAL ".16g"
#else
typedef float real;
#define PRIREAL ".7g"
#endif

// Used by the outer interpreter to decide what to do
typedef enum state {
    INTERPRET = 0,
    COMPILE = 1
} state_t;

state_t state = INTERPRET;

// A word is an entry in the dictionary that can be executed
typedef struct word {
    // Link to the previous word. For the last word, this will be nullptr.
    struct word *link;
    // Flags are as follows:
    // Offset | Name      | Meaning
    //      0 | Immediate | Run this word as soon as encountered, even in compile mode
    //      1 | Hidden    | Hide this word from find
    uint8_t flags;
    // Length of the name. NB NAME_MAX may be less than UINT8_MAX
    uint8_t length;
    // The name. Not necessarily 0-terminated; use length instead.
    char name[NAME_MAX];
    // Address of (machine) code to run for code words, or e.g. DOCOL for colon words
    code_t code;
    // Address of code to run if this word has been patched by DOES>
    code_t does_code;
    // If present, addresses for the body of the word
    cell body[];
} word_t;

#define GET_FLAG(name, offset)\
    cell\
    name(word_t *w) {\
        return FBOOL(0 != (w->flags & 1 << offset));\
    }

#define SET_FLAG(name, offset)\
    void\
    name(word_t *w) {\
        w->flags |= 1 << offset;\
    }

#define CLEAR_FLAG(name, offset)\
    void\
    name(word_t *w) {\
        w->flags &= ~(1 << offset);\
    }

#define FLAGS(get_name, set_name, clear_name, offset)\
    GET_FLAG(get_name, offset)\
    SET_FLAG(set_name, offset)\
    CLEAR_FLAG(clear_name, offset)

FLAGS(w_is_immediate, w_set_immediate, w_clear_immediate, 0)
FLAGS(w_is_hidden, w_set_hidden, w_clear_hidden, 1)

typedef struct input_source {
    const char *data;  // characters to parse
    size_t pos;  // current parse position within data
    size_t len;  // total length of data
    cell from_stdin;  // if true, refill from stdin into buf when exhausted
    cell owned;  // if true, free(data) when popped
    char *buf;   // buffer backing data, for stdin sources
    size_t buf_size;
    struct input_source *next;
} input_source_t;

// Globals and stack operations
// ----------------------------

// The dictionary is initially all zeroed out and grows start-to-end in contiguous memory.
// Here always points to the next free byte to write to. latest points to the most recent word
// added to the dictionary.

alignas(cell) uint8_t dictionary[DICT_SIZE] = {0};
uint8_t *here = dictionary;
word_t *latest = nullptr;

// dstack is the data stack and rstack is the return stack. Similarly to here, their respective
// pointers point to the next free place on the stack to be written to.

cell dstack[DSTACK_SIZE];
cell rstack[RSTACK_SIZE];
real fstack[FSTACK_SIZE];
cell *dsp = dstack;
cell *rsp = rstack;
real *fsp = fstack;

// The string pad is used for storing strings that are currently in use
char string_pad_buf[STRING_PAD_SIZE];
char *string_pad = string_pad_buf;

cell
string_pad_write(char *s, size_t n) {
    // If we would go past the end of the string pad, reset to the beginning
    if (string_pad + n > string_pad_buf + STRING_PAD_SIZE)
        string_pad = string_pad_buf;

    const cell r = (cell)string_pad;
    memcpy(string_pad, s, n);
    string_pad += n;
    return r;
}

#ifdef BOUNDSCHECK
#define PUSH(name, p, stack, stack_type, size)\
    void\
    name(stack_type d) {\
        if (p - stack >= size) abort();\
        *p++ = d;\
    }

#define POP(name, p, stack, stack_type)\
    stack_type\
    name(void) {\
        p--;\
        if (p < stack) abort();\
        return *p;\
    }
#else
#define PUSH(name, p, stack, stack_type, size)\
    void\
        name(stack_type d) {\
        *p++ = d;\
    }

    #define POP(name, p, stack, stack_type)\
    stack_type\
    name(void) {\
        p--;\
        return *p;\
    }
#endif

#define STACK_OPS(push_name, pop_name, p, stack, stack_type, size)\
    PUSH(push_name, p, stack, stack_type, size)\
    POP(pop_name, p, stack, stack_type)

STACK_OPS(dpush, dpop, dsp, dstack, cell, DSTACK_SIZE)
STACK_OPS(rpush, rpop, rsp, rstack, cell, RSTACK_SIZE)
STACK_OPS(fpush, fpop, fsp, fstack, real, FSTACK_SIZE)

// I/O
// ---

// The input buffer stores each line before it is tokenised; input_pos is the current
// pointer into it.

input_source_t *input_top = nullptr;
char stdin_buf[1024];

void
push_string_input(const char *s, size_t len) {
    input_source_t *src = malloc(sizeof *src);
    *src = (input_source_t){
        .data = s,
        .pos = 0,
        .len = len,
        .from_stdin = F_FALSE,
        .owned = F_FALSE,
        .buf = nullptr,
        .next = input_top,
    };
    input_top = src;
}

void
push_stdin_input(void) {
    input_source_t *src = malloc(sizeof *src);
    *src = (input_source_t){
        .data = stdin_buf,
        .pos = 0,
        .len = 0,
        .from_stdin = F_TRUE,
        .owned = F_FALSE,
        .buf = stdin_buf,
        .buf_size = 1024,
        .next = input_top,
    };
    input_top = src;
}

cell
refill(void) {
    // Pop exhausted non-stdin sources
    while (input_top && !input_top->from_stdin && input_top->pos >= input_top->len) {
        input_source_t *done = input_top;
        input_top = done->next;
        if (done->owned) free((void *)done->data);
        free(done);
    }
    if (!input_top) return F_FALSE;  // shouldn't happen if stdin is at the bottom
 
    if (input_top->from_stdin && input_top->pos >= input_top->len) {
        // Need to read a fresh line from stdin
        printf("ff> ");
        fflush(stdout);
        if (!fgets(input_top->buf, (int)input_top->buf_size, stdin)) return F_FALSE;
        input_top->pos = 0;
        input_top->len = strlen(input_top->buf);
    }
    return F_TRUE;
}

cell
key(void) {
    if (!input_top) return F_FALSE;
    while (input_top->pos >= input_top->len) refill();
    return (cell)input_top->data[input_top->pos++];
}

cell
parse_token(char **out_start, uint8_t *out_len) {
    if (!input_top) return F_FALSE;
    const char *data = input_top->data;
    size_t pos = input_top->pos;
    size_t len = input_top->len;
 
    // Skip leading whitespace
    while (pos < len && isspace((unsigned char)data[pos])) pos++;
    if (pos >= len) {
        input_top->pos = pos;
        return F_FALSE;
    }
 
    size_t start = pos;
    while (pos < len && !isspace((unsigned char)data[pos])) pos++;
 
    size_t tok_len = pos - start;
    if (tok_len > 255) tok_len = 255;
 
    *out_start = (char *)(data + start);
    *out_len = (uint8_t)tok_len;
    input_top->pos = pos;
    return F_TRUE;
}

cell
parse_number(const char *s, uint8_t len, scell *out) {
    char buf[32];
    if (len >= sizeof(buf)) return F_FALSE;
    memcpy(buf, s, len);
    buf[len] = '\0';
    char *end;
    errno = 0;
    long n = strtol(buf, &end, 0);
    if (errno) return F_FALSE;
    if (*end != '\0') return F_FALSE;
    *out = (scell)n;
    return F_TRUE;
}

cell
parse_real(const char *s, uint8_t len, real *out) {
    char buf[32];
    if (len >= sizeof(buf)) return F_FALSE;
    memcpy(buf, s, len);
    buf[len] = '\0';
    char *end;
    errno = 0;
#ifdef DOUBLE_AS_REAL
    double r = strtod(buf, &end);
#else
    float r = strtof(buf, &end);
#endif
    if (errno) return F_FALSE;
    if (end == buf || *end != '\0') return F_FALSE;
    *out = (real)r;
    return F_TRUE;
}

cell
word(char **tok, uint8_t *tok_len, const char *error) {
    while (!parse_token(tok, tok_len)) {
        if (!refill()) {
            if (error) {
                fprintf(stderr, "%s", error);
                fflush(stderr);
            }
            return F_FALSE;
        }
    }
    return F_TRUE;
}

// The Forth word PARSE
void
parse(char delim, cell *addr, cell *length) {
    if (!input_top) {
        *addr = 0;
        *length = 0;
        return;
    }

    const char *data = input_top->data;
    size_t pos = input_top->pos;
    size_t len = input_top->len;
    size_t start = pos;
    while (pos < len && data[pos] != delim) pos++;
    *length = pos - start;
    // Advance past the delimiter if found
    if (pos < len) pos++;
    input_top->pos = pos;
    *addr = (cell)data + start;
}

// C helpers, no VM interactions
// -----------------------------

// Align here to cell (= pointer) size again
void
align(void) {
    while ((uintptr_t)here % sizeof(cell)) here++;
}

word_t *
create_header(const char *name, uint8_t name_length, code_t code) {
    align();
    word_t w = {
        .link = latest,
        .flags = 0,
        .length = name_length,
        .name = {0},
        .code = code,
        .does_code = nullptr,
    };
    memcpy(w.name, name, name_length);
    latest = (word_t *)here;
    memcpy(here, &w, sizeof(word_t));
    here += offsetof(word_t, body);
    return latest;
}

// Store cell-sized data into the dictionary and advance
void
comma(cell value) {
    align();
    *(cell *)here = value;
    here += sizeof(cell);
}

void
allot(cell n) {
    here += n;
}

// Store real-sized data into the dictionary and advance
void
fcomma(real value) {
    align();
    *(real *)here = value;
    here += sizeof(real);
}

cell
name_matches(const char *n1, const char *n2, uint8_t len) {
    for (int i = 0; i < len; i++) {
        if (n1[i] != n2[i]) return F_FALSE;
    }
    return F_TRUE;
}

word_t *
find(const char *name, uint8_t len) {
    word_t *current = latest;
    while (current != nullptr) {
        if (!w_is_hidden(current)
            && current->length == len
            && name_matches(name, current->name, len))
            return current;
        current = current->link;
    }
    return nullptr;
}

// Run the REPL
// ------------

void
run(void) {
#ifdef VERBOSE
    fprintf(stderr, "sizeof(word_t)=%zu offsetof(body)=%zu sizeof(cell)=%zu\n",
        sizeof(word_t), offsetof(word_t, body), sizeof(cell));
#endif

    cell *IP;
    cell *W;
    cell a, b, *p, *q;
    scell sa, sb;
    real fa, fb;
    char c, *tok;
    uint8_t tok_len, *ptr;
    word_t *w;

    // Interpreter
    word_t *w_halt = create_header("HALT", 4, &&do_halt);
    w_set_hidden(w_halt);
    create_header("EXECUTE", 7, &&do_execute);
    create_header("STATE", 5, &&do_state);
    create_header("HERE", 4, &&do_here);

    // Stack manipulation
    create_header(".", 1, &&do_dot);
    create_header(".X", 2, &&do_dot_x);
    create_header(".S", 2, &&do_print_stack);
    create_header("F.", 2, &&do_fdot);
    create_header("F.S", 3, &&do_print_fstack);
    create_header("DUP", 3, &&do_dup);
    create_header("DROP", 4, &&do_drop);
    create_header("SWAP", 4, &&do_swap);
    create_header("OVER", 4, &&do_over);
    create_header("?DUP", 4, &&do_qdup);
    create_header("2DUP", 4, &&do_2dup);
    create_header("2DROP", 5, &&do_2drop);
    create_header("2SWAP", 5, &&do_2swap);
    create_header("ROT", 3, &&do_rot);
    create_header("-ROT", 4, &&do_minus_rot);
    create_header("NIP", 3, &&do_nip);
    create_header("TUCK", 4, &&do_tuck);
    create_header(">R", 2, &&do_to_rstack);
    create_header("R>", 2, &&do_from_rstack);
    create_header("R@", 2, &&do_fetch_rstack);
    create_header("S>F", 3, &&do_to_fstack);
    create_header("F>S", 3, &&do_from_fstack);
    create_header("DEPTH", 5, &&do_depth);

    // Arithmetic
    create_header("+", 1, &&do_plus);
    create_header("-", 1, &&do_minus);
    create_header("*", 1, &&do_mul);
    create_header("/", 1, &&do_div);
    create_header("<", 1, &&do_lt);
    create_header("<=", 2, &&do_lte);
    create_header(">", 1, &&do_gt);
    create_header(">=", 2, &&do_gte);
    create_header("=", 1, &&do_eq);
    create_header("<>", 2, &&do_neq);
    create_header("1+", 2, &&do_1p);
    create_header("1-", 2, &&do_1m);
    create_header("2+", 2, &&do_2p);
    create_header("2-", 2, &&do_2m);
    create_header("0=", 2, &&do_0eq);
    create_header("0<", 2, &&do_is_neg);
    create_header("0>", 2, &&do_is_pos);
    create_header("2*", 2, &&do_mul2);
    create_header("2/", 2, &&do_div2);
    create_header("NEGATE", 6, &&do_negate);

    // Floating-point arithmetic
    create_header("F+", 2, &&do_fplus);
    create_header("F-", 2, &&do_fminus);
    create_header("F*", 2, &&do_fmul);
    create_header("F/", 2, &&do_fdiv);
    create_header("F<", 2, &&do_flt);
    create_header("F<=", 3, &&do_flte);
    create_header("F>", 2, &&do_fgt);
    create_header("F>=", 3, &&do_fgte);
    create_header("F=", 2, &&do_feq);
    create_header("F<>", 3, &&do_fneq);

    // Bitwise combinators
    create_header("AND", 3, &&do_and);
    create_header("OR", 2, &&do_or);
    create_header("XOR", 3, &&do_xor);
    create_header("INVERT", 6, &&do_invert);

    // Strings
    word_t *w_s_quo = create_header("S\"", 2, &&do_s_quo);
    w_set_immediate(w_s_quo);
    word_t *w_s_quo_runtime = create_header("(S\")", 4, &&do_s_quo_runtime);
    w_set_hidden(w_s_quo_runtime);
    create_header("EMIT", 4, &&do_emit);
    create_header("TYPE", 4, &&do_type);

    // Word defining
    word_t *w_docol = create_header("DOCOL", 5, &&do_docol);
    w_set_hidden(w_docol);
    word_t *w_exit = create_header("EXIT", 4, &&do_exit);
    word_t *w_lit = create_header("LIT", 3, &&do_lit);
    w_set_hidden(w_lit);
    word_t *w_flit = create_header("FLIT", 4, &&do_flit);
    w_set_hidden(w_flit);
    create_header("CHAR", 4, &&do_char);
    create_header("LIT-COMPILE", 11, &&do_lit_compile);
    create_header("'", 1, &&do_tick);
    word_t *w_tick_immediate = create_header("[']", 3, &&do_tick_immediate);
    w_set_immediate(w_tick_immediate);
    create_header(":", 1, &&do_colon);
    create_header(":NONAME", 7, &&do_colon_noname);
    word_t *w_semico = create_header(";", 1, &&do_semico);
    w_set_immediate(w_semico);
    create_header("CREATE", 6, &&do_create);
    word_t *w_does = create_header("DOES>", 5, &&do_does);
    w_set_immediate(w_does);
    word_t *w_does_runtime = create_header("(DOES>)", 7, &&do_does_runtime);
    w_set_hidden(w_does_runtime);
    word_t *w_postpone = create_header("POSTPONE", 8, &&do_postpone);
    w_set_immediate(w_postpone);
    create_header("IMMEDIATE", 9, &&do_immediate);
    create_header("IMMEDIATE?", 10, &&do_is_immediate);
    create_header("HIDE", 4, &&do_hide);
    create_header("FIND", 4, &&do_find);
    create_header("SEE", 3, &&do_see);
    create_header("WORDS", 5, &&do_words);

    // Memory
    create_header("ALIGN", 5, &&do_align);
    word_t *w_comma = create_header(",", 1, &&do_comma);
    create_header("CELL", 4, &&do_cell);
    create_header("ALLOT", 5, &&do_allot);
    create_header("@", 1, &&do_fetch);
    create_header("!", 1, &&do_store);
    create_header("C,", 2, &&do_char_comma);
    create_header("C@", 2, &&do_char_fetch);
    create_header("C!", 2, &&do_char_store);

    // Control flow
    word_t *w_branch = create_header("BRANCH", 6, &&do_branch);
    w_set_hidden(w_branch);
    word_t *w_0branch = create_header("0BRANCH", 7, &&do_0branch);
    w_set_hidden(w_0branch);
    word_t *w_if = create_header("IF", 2, &&do_if);
    w_set_immediate(w_if);
    word_t *w_then = create_header("THEN", 4, &&do_then);
    w_set_immediate(w_then);
    word_t *w_else = create_header("ELSE", 4, &&do_else);
    w_set_immediate(w_else);
    word_t *w_begin = create_header("BEGIN", 5, &&do_begin);
    w_set_immediate(w_begin);
    word_t *w_again = create_header("AGAIN", 5, &&do_again);
    w_set_immediate(w_again);
    word_t *w_until = create_header("UNTIL", 5, &&do_until);
    w_set_immediate(w_until);
    word_t *w_while = create_header("WHILE", 5, &&do_while);
    w_set_immediate(w_while);
    word_t *w_repeat = create_header("REPEAT", 6, &&do_repeat);
    w_set_immediate(w_repeat);

    // Parsing
    create_header("KEY", 3, &&do_key);
    create_header("PARSE-NAME", 10, &&do_parse_name);
    create_header("PARSE", 5, &&do_parse);
    create_header(">NUMBER", 7, &&do_to_number);
    create_header(">REAL", 5, &&do_to_real);

    static cell exec_buf[2];
    exec_buf[1] = (cell)w_halt;

    #define NEXT() do { \
        W = (cell *)*IP++; \
        goto *((word_t *)W)->code; \
    } while(0)

    push_stdin_input();
    push_string_input(prelude, sizeof(prelude));
    goto xt_interpret; // kick off

    #include "primitives.inc"

    xt_interpret: {
        char *tok;
        uint8_t tok_len;

        while (!parse_token(&tok, &tok_len)) {
            if (!refill()) return;
        }

        word_t *w = find(tok, tok_len);
        if (w) {
            // if we're compiling a regular word, add it to the current definition
            if (state == COMPILE && !w_is_immediate(w)) {
                comma((cell)w);
                goto xt_interpret;
            }
            // execute it
            exec_buf[0] = (cell)w;
            IP = exec_buf;
            NEXT();
        }

        // No word found, so try a number
        if (parse_number(tok, tok_len, &sa)) {
            if (state == COMPILE) {
                comma((cell)w_lit);
                comma((cell)sa);
            } else {
                dpush(sa);
            }
            goto xt_interpret;
        }

        if (parse_real(tok, tok_len, &fa)) {
            if (state == COMPILE) {
                comma((cell)w_flit);
                fcomma(fa);
            } else {
                fpush(fa);
            }
            goto xt_interpret;
        }

        // Unknown
        printf("? %.*s\n", tok_len, tok);
        goto xt_interpret;
    }
}

int
main(int argc, char *argv[]) {
    run();
    return EXIT_SUCCESS;
}
