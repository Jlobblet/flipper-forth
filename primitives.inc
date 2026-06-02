#define BINOP(op)\
        b = dpop();\
        a = dpop();\
        dpush(a op b);\
        NEXT();

#define SBINOP(op)\
        sb = (scell)dpop();\
        sa = (scell)dpop();\
        dpush((cell)(sa op sb));\
        NEXT();

#define CMPBINOP(op)\
        sb = (scell)dpop();\
        sa = (scell)dpop();\
        dpush(FBOOL(sa op sb));\
        NEXT();

#define WORD(e) do { if (!word(&tok, &tok_len, e)) goto xt_interpret; } while(0)
#define PTR_ALIGN() do { while ((uintptr_t)ptr % sizeof(cell)) ptr++; } while(0)

    // Interpreter
    // -----------
    do_halt:
        goto xt_interpret;
    do_execute:
        W = (cell *)dpop();
        goto *((word_t *)W)->code;
    do_state:
        dpush((cell)&state);
        NEXT();
    do_here:
        dpush((cell)here);
        NEXT();

    // Stack manipulation
    // ------------------
    // ( a -- )
    do_dot:
        sa = (scell)dpop();
        printf("%" PRIdPTR "\n", sa);
        NEXT();
    // ( a -- )
    do_dot_x:
        a = dpop();
        printf("0X%" PRIXPTR "\n", a);
    // ( -- )
    do_print_stack:
        for (cell *p = dstack; p < dsp; p++) printf("%" PRIdPTR "\n", (scell)*p);
        NEXT();
    // ( a -- a a )
    do_dup:
        *dsp = *(dsp - 1);
        dsp++;
        NEXT();
    // ( a -- )
    do_drop:
        dsp--;
        NEXT();
    // ( a b -- b a )
    do_swap:
        a = *(dsp - 2);
        *(dsp - 2) = *(dsp - 1);
        *(dsp - 1) = a;
        NEXT();
    // ( a b -- a b a )
    do_over:
        *dsp = *(dsp - 2);
        dsp++;
        NEXT();
    // ( a -- 0 | a a )
    do_qdup:
        if (*(dsp - 1) != 0) dpush(*(dsp - 1));
        NEXT();
    // ( a b -- a b a b )
    do_2dup:
        dpush(*(dsp - 2));
        dpush(*(dsp - 2));
        NEXT();
    // ( a b -- )
    do_2drop:
        dsp -= 2;
        NEXT();
    // ( a b c -- b c a )
    do_rot:
        a = *(dsp - 3);
        *(dsp - 3) = *(dsp - 2);
        *(dsp - 2) = *(dsp  -1);
        *(dsp - 1) = a;
        NEXT();
    // ( a b c -- c a b )
    do_minus_rot:
        a = *(dsp - 3);
        *(dsp - 3) = *(dsp - 1);
        *(dsp - 1) = *(dsp - 2);
        *(dsp - 2) = a;
        NEXT();
    // ( a b -- b )
    do_nip:
        *(dsp - 2) = *(dsp - 1);
        dsp--;
        NEXT();
    // ( a b -- b a b )
    do_tuck:
        a = *(dsp - 2);
        *(dsp - 2) = *(dsp - 1);
        *(dsp - 1) = a;
        dpush(*(dsp - 2));
        NEXT();
    // ( a -- ) (R: -- a )
    do_to_rstack:
        rpush(dpop());
        NEXT();
    // ( -- a ) (R: a -- )
    do_from_rstack:
        dpush(rpop());
        NEXT();
    // ( -- a ) (R: a -- a)
    do_fetch_rstack:
        dpush(*(rsp - 1));
        NEXT();
    // ( -- a )
    do_depth:
        dpush(dsp - dstack);
        NEXT();

    // Arithmetic
    // ----------
    do_plus: SBINOP(+)
    do_minus: SBINOP(-)
    do_mul: SBINOP(*)
    do_div: SBINOP(/)
    do_lt: CMPBINOP(<)
    do_lte: CMPBINOP(<=)
    do_gt: CMPBINOP(>)
    do_gte: CMPBINOP(>=)
    do_eq: CMPBINOP(==)
    do_neq: CMPBINOP(!=)

    // Strings
    // -------
    do_s_quo:
        parse('"', &a, &b);
        if (b > 0 && isspace(*(char *)a)) { a++; b--; }
        if (state == COMPILE) {
            // Compile the (S") word, then inline the data as a counted string
            comma((cell)w_s_quo_runtime);
            comma(b);
            memcpy(here, (char *)a, b);
            here += b;
            align();
        } else {
            // Copy into the string buffer and push addr/len
            if (string_pad + b > string_pad_buf + STRING_PAD_SIZE)
                string_pad = string_pad_buf;

            memcpy(string_pad, (char *)a, b);
            dpush((cell)string_pad);
            dpush(b);
            string_pad += b;
        }
        NEXT();
    do_s_quo_runtime:
        b = *IP++;
        dpush((cell)IP);
        dpush(b);
        // Align IP
        ptr =  (uint8_t *)IP + b;
        PTR_ALIGN();
        IP = (cell *)ptr;
        NEXT();
    // ( c -- )
    do_emit:
        putchar((int)(unsigned char)dpop());
        NEXT();
    // ( addr length -- )
    do_type:
        a = dpop();
        b = dpop();
        printf("%.*s", (int)a, (char *)b);
        NEXT();

    // Word defining
    // -------------
    do_docol:
#ifdef VERBOSE
        fprintf(stderr, "docol for '%.*s': W=%p body=%p body[0]=%p\n",
                    ((word_t*)W)->length, ((word_t*)W)->name,
                    (void*)W, (void*)((word_t*)W)->body, (void*)((word_t*)W)->body[0]);
        fflush(stderr);
#endif
        rpush((cell)IP);
        IP = (cell *)(((word_t *)W)->body);
        NEXT();
    do_exit:
        IP = (cell *)rpop();
        NEXT();
    do_lit:
        dpush(*IP++);
        NEXT();
    do_char:
        WORD(nullptr);
        dpush((cell)(unsigned char)tok[0]);
        NEXT();
    do_lit_compile:
        comma((cell)w_lit);
        comma(dpop());
        NEXT();
    do_tick:
        WORD("Expected token after '\n");
        w = find(tok, tok_len);
        dpush((cell)w);
        NEXT();
    do_tick_immediate:
        WORD("Expcted token after [']\n");
        w = find(tok, tok_len);
        comma((cell)w_lit);
        comma((cell)w);
        NEXT();
    do_colon:
        WORD("Expected name after :\n");
        w = create_header(tok, tok_len, w_docol->code);
        w_set_hidden(w);
        state = COMPILE;
        NEXT();
    do_colon_noname:
        w = create_header("", 0, w_docol->code);
        w_set_hidden(w);
        state = COMPILE;
        dpush((cell)w);
        NEXT();
    do_semico:
        comma((cell)w_exit);
        w_clear_hidden(latest);
        #ifdef VERBOSE
        fprintf(stderr, "after ; for word '%.*s': body[0]=%p body[1]=%p body[2]=%p\n",
                    latest->length, latest->name,
                    (void*)latest->body[0], (void*)latest->body[1], (void*)latest->body[2]);
        fprintf(stderr, "  w_at=%p w_dot=%p w_exit=%p\n",
                (void*)find("@",1), (void*)find(".",1), (void*)w_exit);
        fflush(stderr);
        #endif
        state = INTERPRET;
        NEXT();
    do_create:
        WORD("Expected name after CREATE\n");

        create_header(tok, tok_len, &&do_create_runtime);
        NEXT();
    do_create_runtime:
        // Push the created address (default behaviour)
        dpush((cell)((word_t *)W)->body);
        if (((word_t *)W)->does_code) {
            rpush((cell)IP);
            IP = (cell *)((word_t *)W)->does_code;
        }
        NEXT();
    do_does:
        comma((cell)w_does_runtime);
        NEXT();
    do_does_runtime:
        // Patch latest's does_code to point at the code following this primitive
        // in the defining word. IP currently points just past us.
        latest->does_code = (code_t)IP;
        // Now return from the defining word, since the post-DOES> code shouldn't run
        // during the defining word's execution.
        IP = (cell *)rpop();
        NEXT();
    do_postpone:
        WORD("Expected name after POSTPONE\n");
        w = find(tok, tok_len);
        if (!w) { printf("? %.*s\n", tok_len, tok); NEXT(); }
        if (w_is_immediate(w)) {
            // compile a call to it now: it'll run when the enclosing word runs
            comma((cell)w);
        } else {
            // compile code that, when the enclosing word runs, compiles a call to w
            comma((cell)w_lit);
            comma((cell)w);
            comma((cell)w_comma);
        }
        NEXT();
    do_immediate:
        w_set_immediate(latest);
        NEXT();
    do_forget:
        WORD("Expected name after FORGET\n");
        w = find(tok, tok_len);
        if (!w) { printf("? %.*s\n", tok_len, tok); NEXT(); }
        w_set_hidden(w);
        NEXT();
    do_see:
        WORD("Expected name after SEE\n");
        w = find(tok, tok_len);
        if (!w) { printf("? %.*s\n", tok_len, tok); NEXT(); }

        printf(": %.*s", w->length, w->name);
        if (w->code != w_docol->code) {
            // Not a colon word: nothing threaded to walk.
            if (w->does_code)
                printf("  ( CREATE/DOES> word )");
            else
                printf("  ( primitive )");
            if (w_is_immediate(w))
                printf(" IMMEDIATE");
            if (w_is_hidden(w))
                printf(" ( hidden )");
            printf("\n");
            NEXT();
        }

        // Walk the threaded body until EXIT.
        p = q = (cell *)w->body;
        while (*p != (cell)w_exit) {
            w = (word_t *)*p++;
            if (!w) {
                printf(" <null>");
                continue;
            }
            printf(" %.*s", w->length, w->name);
            if (w == w_lit)
                printf(" %" PRIdPTR, (scell)*p++);
            else if (w == w_branch || w == w_0branch)
                printf(" [%+ld]", (long)((cell *)*p++ - q));
            else if (w == w_s_quo_runtime) {
                a = *p++;
                printf(" \"%.*s\"", (int)a, (char *)p);
                ptr = (uint8_t *)p + a;
                PTR_ALIGN();
                p = (cell *)ptr;
            }
        }
        printf(" ;");
        if (w_is_immediate(w)) printf(" IMMEDIATE");
        printf("\n");
        NEXT();
    do_words:
        w = latest;
        while (w) {
            printf("%.*s", w->length, w->name);
            if (w_is_immediate(w)) printf(" (IMMEDIATE)");
            if (w_is_hidden(w)) printf(" ( hidden )");
            printf("\n");
            w = w->link;
        }
        NEXT();

    // Memory
    // ------
    do_align:
        align();
        NEXT();
    do_comma:
        comma(*(dsp - 1));
        dsp--;
        NEXT();
    do_cell:
        *dsp = (cell)sizeof(cell);
        dsp++;
        NEXT();
    do_allot:
        allot(*(dsp - 1));
        dsp++;
        NEXT();
    do_fetch:
        *(dsp - 1) = *((cell *)*(dsp - 1));
        NEXT();
    do_store:
        *(cell *)*(dsp - 1) = *(dsp - 2);
        dsp -= 2;
        NEXT();
    do_char_comma:
        *here = (char)*(dsp - 1);
        here++;
        dsp--;
        NEXT();
    do_char_fetch:
        *(dsp - 1) = (cell)*((uint8_t *)*(dsp - 1));
        NEXT();
    do_char_store:
        *(uint8_t *)*(dsp - 1) = (char)*(dsp - 1);
        dsp -= 2;
        NEXT();

    // Control flow
    // ------------
    do_branch:
        // Update IP to cell location
        IP = (cell *)*IP;
        NEXT();
    do_0branch:
        // Only branch if dpop is zero
        if (dpop() == F_FALSE) goto do_branch;
        IP++;
        NEXT();
    do_if:
        // Store 0branch word, push here to stack, and then save placeholder branch target
        comma((cell)w_0branch);
        dpush((cell)here);
        comma(0);
        NEXT();
    do_then:
        // Retrieve the location to patch from stack and then update it with the new here
        a = dpop();
        *(cell *)a = (cell)here;
        NEXT();
    do_else:
        // Store new branch word and placeholder
        comma((cell)w_branch);
        b = (cell)here;
        comma(0);
        // Update previous IF to jump to here
        a = dpop();
        *(cell *)a = (cell)here;
        // Finally, push the new patch location
        dpush(b);
        NEXT();
    do_begin:
        // Push the location to branch to
        dpush((cell)here);
        NEXT();
    do_again:
        // Insert a branch to the stored location
        a = dpop();
        comma((cell)w_branch);
        comma(a);
        NEXT();
    do_until:
        a = dpop();
        comma((cell)w_0branch);
        comma(a);
        NEXT();
    do_while:
        comma((cell)w_0branch);
        dpush((cell)here);
        comma(0);
        NEXT();
    do_repeat:
        // While patch
        a = dpop();
        // Begin target
        b = dpop();
        comma((cell)w_branch);
        comma(b);
        *(cell *)a = (cell)here;
        NEXT();

    // Parsing
    // -------
    // ( delim -- addr len )
    do_parse:
        c = (char)dpop();
        parse(c, &a, &b);
        dpush(a);
        dpush(b);
        NEXT();
