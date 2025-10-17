/*
  intbasic.c -- small Integer-BASIC-like interpreter (integer arithmetic only)
  Compile: gcc -O2 -o intbasic intbasic.c
  Run: ./intbasic

  Features:
   - Line-numbered program storage
   - Immediate mode and program editing via entering lines prefixed by a number
   - RUN, LIST, NEW
   - LET var = expr
   - PRINT expr [, expr ...]
   - INPUT var
   - GOTO lineno
   - IF expr THEN lineno
   - FOR var = start TO end [STEP step] ... NEXT var
   - REM comment
   - END
   - Integer arithmetic only (32-bit)
   - Variables: A..Z only
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE_LEN 1024
#define MAX_TOKEN_LEN 128
#define MAX_FOR_STACK 256

/* --- Program storage as linked list of lines (sorted by line number) --- */
typedef struct Line {
    int lineno;
    char *text;         /* original line text (statements after the lineno) */
    struct Line *next;
} Line;

Line *program = NULL;

void free_program() {
    Line *p = program;
    while (p) {
        Line *n = p->next;
        free(p->text);
        free(p);
        p = n;
    }
    program = NULL;
}

/* Insert or replace a line into the program (if text==NULL or empty -> delete line) */
void insert_line(int lineno, const char *text) {
    if (lineno <= 0) return;
    Line **pp = &program;
    while (*pp && (*pp)->lineno < lineno) pp = &(*pp)->next;
    if (*pp && (*pp)->lineno == lineno) {
        /* replace or delete */
        if (!text || text[0] == '\0') {
            Line *tofree = *pp;
            *pp = tofree->next;
            free(tofree->text);
            free(tofree);
        } else {
            free((*pp)->text);
            (*pp)->text = strdup(text);
        }
        return;
    }
    if (!text || text[0] == '\0') return; /* nothing to insert */
    Line *n = malloc(sizeof(Line));
    n->lineno = lineno;
    n->text = strdup(text);
    n->next = *pp;
    *pp = n;
}

/* Find line by line number */
Line *find_line(int lineno) {
    Line *p = program;
    while (p) {
        if (p->lineno == lineno) return p;
        if (p->lineno > lineno) return NULL;
        p = p->next;
    }
    return NULL;
}

/* Find first line (lowest line number) */
Line *first_line() {
    return program;
}

/* Find next line after given line (or after a given lineno) */
Line *next_line(Line *cur) {
    if (!cur) return program;
    return cur->next;
}

/* --- Tokenizer / parser helpers --- */
const char *cp; /* current parse pointer */

void skip_spaces() {
    while (*cp && isspace((unsigned char)*cp)) cp++;
}

int match_keyword(const char *kw) {
    skip_spaces();
    const char *p = cp;
    while (*kw && toupper((unsigned char)*p) == *kw) { p++; kw++; }
    if (*kw == '\0' && (isspace((unsigned char)*p) || *p == '\0' || *p == ',' || *p == ':' || *p == '(' || *p == ')')) {
        return 1;
    }
    return 0;
}

int consume_keyword(const char *kw) {
    if (match_keyword(kw)) {
        while (*kw) { cp++; kw++; }
        return 1;
    }
    return 0;
}

/* Parse an identifier (single letter variables allowed) */
int peek_var() {
    skip_spaces();
    if (isalpha((unsigned char)*cp)) return toupper((unsigned char)*cp);
    return 0;
}

/* Parse integer number (decimal only) */
int parse_number(int *out) {
    skip_spaces();
    const char *p = cp;
    int sign = 1;
    if (*p == '+') { p++; }
    else if (*p == '-') { sign = -1; p++; }
    if (!isdigit((unsigned char)*p)) return 0;
    long val = 0;
    while (isdigit((unsigned char)*p)) { val = val * 10 + (*p - '0'); p++; }
    *out = (int)(sign * val);
    cp = p;
    return 1;
}

/* Forward declarations for expression parser */
int parse_expr(int *out);

/* Parse factor: number | variable | (expr) */
int parse_factor(int *out) {
    skip_spaces();
    if (*cp == '(') {
        cp++;
        if (!parse_expr(out)) return 0;
        skip_spaces();
        if (*cp != ')') return 0;
        cp++;
        return 1;
    }
    int v;
    if (parse_number(&v)) {
        *out = v;
        return 1;
    }
    skip_spaces();
    if (isalpha((unsigned char)*cp)) {
        char name = toupper((unsigned char)*cp);
        cp++;
        extern int vars[26];
        *out = vars[name - 'A'];
        return 1;
    }
    return 0;
}

/* Parse term: factor ((* or /) factor)* */
int parse_term(int *out) {
    int left;
    if (!parse_factor(&left)) return 0;
    while (1) {
        skip_spaces();
        if (*cp == '*') {
            cp++;
            int r;
            if (!parse_factor(&r)) return 0;
            left = left * r;
        } else if (*cp == '/') {
            cp++;
            int r;
            if (!parse_factor(&r)) return 0;
            if (r == 0) { printf("DIVIDE BY ZERO\n"); left = 0; } else left = left / r;
        } else break;
    }
    *out = left;
    return 1;
}

/* Parse expr: term ((+|-) term)* */
int parse_expr(int *out) {
    int left;
    if (!parse_term(&left)) return 0;
    while (1) {
        skip_spaces();
        if (*cp == '+') {
            cp++;
            int r;
            if (!parse_term(&r)) return 0;
            left = left + r;
        } else if (*cp == '-') {
            cp++;
            int r;
            if (!parse_term(&r)) return 0;
            left = left - r;
        } else break;
    }
    *out = left;
    return 1;
}

/* --- Variables --- */
int vars[26];

void clear_vars() {
    for (int i=0;i<26;i++) vars[i]=0;
}

/* --- FOR stack --- */
typedef struct ForEntry {
    char var;      /* 'A'..'Z' */
    int end;
    int step;
    Line *for_line; /* where the FOR is */
    const char *for_pos; /* pointer into for_line->text where next statement begins after FOR */
} ForEntry;

ForEntry forstack[MAX_FOR_STACK];
int for_sp = 0;

int push_for(char var, int end, int step, Line *for_line, const char *for_pos) {
    if (for_sp >= MAX_FOR_STACK) return 0;
    forstack[for_sp].var = var;
    forstack[for_sp].end = end;
    forstack[for_sp].step = step;
    forstack[for_sp].for_line = for_line;
    forstack[for_sp].for_pos = for_pos;
    for_sp++;
    return 1;
}

int pop_for(char var, ForEntry *out) {
    for (int i = for_sp-1; i >= 0; --i) {
        if (forstack[i].var == var) {
            *out = forstack[i];
            /* remove from stack (pop up to i inclusive) */
            for_sp = i;
            return 1;
        }
    }
    return 0;
}

/* --- Execution engine --- */

/* Helper: evaluate an expression string starting at cp (global), returns value and sets cp to first char after expression */
int eval_expr_from_cp(int *out) {
    return parse_expr(out);
}

/* Execute a single statement starting at cp (global). Returns:
   0 -> continue normally (advance to next statement/line)
   1 -> RUN terminated (END or error)
   special control signals via global variables: jump_to_line set to target lineno if GOTO/IF/NEXT transfers control.
*/
int jump_to_lineno = 0;
int end_program_flag = 0;
int error_flag = 0;

void set_jump_to_lineno(int lineno) {
    jump_to_lineno = lineno;
}

int exec_statement(Line **curlp, const char **curpos) {
    /* curlp: pointer to current Line* (so we can change it for GOTO), curpos: pointer to current parse pointer into the line text */
    cp = *curpos;
    skip_spaces();
    if (*cp == '\0') { *curpos = cp; return 0; }

    /* REM (comment) */
    if (match_keyword("REM")) {
        consume_keyword("REM");
        /* rest of line ignored */
        *curpos = strchr(cp, '\0'); /* move to end */
        return 0;
    }

    /* LET var = expr  (LET optional) or direct assignment: A = expr */
    if (match_keyword("LET")) {
        consume_keyword("LET");
    }
    skip_spaces();
    if (isalpha((unsigned char)*cp)) {
        /* could be assignment or a statement named by letter (rare). We'll parse assignment */
        const char *save = cp;
        char var = toupper((unsigned char)*cp);
        cp++;
        skip_spaces();
        if (*cp == '=') {
            cp++;
            int val;
            if (!eval_expr_from_cp(&val)) { printf("SYNTAX ERROR IN LET\n"); error_flag=1; return 1; }
            vars[var - 'A'] = val;
            skip_spaces();
            *curpos = cp;
            return 0;
        } else {
            /* Not assignment; rewind */
            cp = save;
        }
    }

    /* PRINT */
    if (match_keyword("PRINT")) {
        consume_keyword("PRINT");
        int first = 1;
        while (1) {
            skip_spaces();
            /* support strings in quotes */
            if (*cp == '"') {
                cp++;
                while (*cp && *cp != '"') {
                    putchar(*cp++);
                }
                if (*cp == '"') cp++;
            } else {
                int v;
                if (!eval_expr_from_cp(&v)) { printf("SYNTAX ERROR IN PRINT\n"); error_flag=1; return 1; }
                if (!first) printf(" ");
                printf("%d", v);
            }
            first = 0;
            skip_spaces();
            if (*cp == ',') { cp++; continue; }
            break;
        }
        putchar('\n');
        *curpos = cp;
        return 0;
    }

    /* INPUT var (only single variable) */
    if (match_keyword("INPUT")) {
        consume_keyword("INPUT");
        skip_spaces();
        if (!isalpha((unsigned char)*cp)) { printf("SYNTAX ERROR IN INPUT\n"); error_flag=1; return 1; }
        char var = toupper((unsigned char)*cp);
        cp++;
        skip_spaces();
        printf("? ");
        fflush(stdout);
        char buf[256];
        if (!fgets(buf, sizeof(buf), stdin)) { end_program_flag=1; return 1; }
        /* read integer from input */
        int val = atoi(buf);
        vars[var - 'A'] = val;
        *curpos = cp;
        return 0;
    }

    /* GOTO lineno */
    if (match_keyword("GOTO")) {
        consume_keyword("GOTO");
        skip_spaces();
        int lineno;
        if (!parse_number(&lineno)) { printf("SYNTAX ERROR IN GOTO\n"); error_flag=1; return 1; }
        set_jump_to_lineno(lineno);
        return 0;
    }

    /* IF expr THEN lineno */
    if (match_keyword("IF")) {
        consume_keyword("IF");
        int left;
        if (!eval_expr_from_cp(&left)) { printf("SYNTAX ERROR IN IF\n"); error_flag=1; return 1; }
        skip_spaces();
        /* Support relational operator: =, <>, <, >, <=, >= */
        int rel = 0; /* 1==,2<>,3<,4>,5<=,6>= */
        if (*cp == '=') { rel = 1; cp++; }
        else if (*cp == '<') {
            cp++;
            if (*cp == '>') { rel = 2; cp++; }
            else if (*cp == '=') { rel = 5; cp++; }
            else rel = 3;
        }
        else if (*cp == '>') {
            cp++;
            if (*cp == '=') { rel = 6; cp++; }
            else rel = 4;
        } else {
            printf("SYNTAX ERROR IN IF (no relation)\n"); error_flag=1; return 1;
        }
        int right;
        if (!eval_expr_from_cp(&right)) { printf("SYNTAX ERROR IN IF\n"); error_flag=1; return 1; }
        int cond = 0;
        switch (rel) {
            case 1: cond = (left == right); break;
            case 2: cond = (left != right); break;
            case 3: cond = (left < right); break;
            case 4: cond = (left > right); break;
            case 5: cond = (left <= right); break;
            case 6: cond = (left >= right); break;
        }
        skip_spaces();
        if (!match_keyword("THEN")) { printf("SYNTAX ERROR IN IF (expected THEN)\n"); error_flag=1; return 1; }
        consume_keyword("THEN");
        skip_spaces();
        if (!cond) { *curpos = cp; return 0; }
        /* THEN can be a line number (GOTO-style) or an immediate statement. We'll support numeric lineno GOTO behaviour */
        int lineno;
        const char *p_save = cp;
        if (parse_number(&lineno)) {
            set_jump_to_lineno(lineno);
            return 0;
        } else {
            /* Not a number: execute the remainder as a statement (simple approach) */
            /* We'll execute the rest of the line as a statement by setting cp to p_save and calling exec_statement recursively */
            cp = p_save;
            *curpos = cp;
            return exec_statement(curlp, curpos);
        }
    }

    /* FOR var = start TO end [STEP step] */
    if (match_keyword("FOR")) {
        consume_keyword("FOR");
        skip_spaces();
        if (!isalpha((unsigned char)*cp)) { printf("SYNTAX ERROR IN FOR\n"); error_flag=1; return 1; }
        char var = toupper((unsigned char)*cp); cp++;
        skip_spaces();
        if (*cp != '=') { printf("SYNTAX ERROR IN FOR (no =)\n"); error_flag=1; return 1; }
        cp++;
        int start;
        if (!eval_expr_from_cp(&start)) { printf("SYNTAX ERROR IN FOR (start)\n"); error_flag=1; return 1; }
        skip_spaces();
        if (!match_keyword("TO")) { printf("SYNTAX ERROR IN FOR (no TO)\n"); error_flag=1; return 1; }
        consume_keyword("TO");
        int endv;
        if (!eval_expr_from_cp(&endv)) { printf("SYNTAX ERROR IN FOR (end)\n"); error_flag=1; return 1; }
        int step = 1;
        skip_spaces();
        if (match_keyword("STEP")) {
            consume_keyword("STEP");
            if (!parse_number(&step)) { printf("SYNTAX ERROR IN FOR (STEP)\n"); error_flag=1; return 1; }
        }
        /* set variable to start */
        vars[var - 'A'] = start;
        /* push FOR onto stack. Save current line and pos after FOR statement to return to. */
        Line *fl = *curlp;
        const char *pos_after_for = cp; /* position after the FOR statement; when NEXT occurs we'll jump back to this */
        if (!push_for(var, endv, step, fl, pos_after_for)) { printf("FOR STACK OVERFLOW\n"); error_flag=1; return 1; }
        *curpos = cp;
        return 0;
    }

    /* NEXT var */
    if (match_keyword("NEXT")) {
        consume_keyword("NEXT");
        skip_spaces();
        if (!isalpha((unsigned char)*cp)) { printf("SYNTAX ERROR IN NEXT\n"); error_flag=1; return 1; }
        char var = toupper((unsigned char)*cp); cp++;
        ForEntry fe;
        if (!pop_for(var, &fe)) { printf("NEXT WITHOUT FOR\n"); error_flag=1; return 1; }
        /* increment variable */
        vars[var - 'A'] += fe.step;
        /* check loop end depending on step sign */
        int v = vars[var - 'A'];
        int cont = 0;
        if (fe.step > 0) {
            if (v <= fe.end) cont = 1;
        } else {
            if (v >= fe.end) cont = 1;
        }
        if (cont) {
            /* push the FOR back (we popped it) and set jump to the for_line (re-execute remainder after FOR) */
            push_for(fe.var, fe.end, fe.step, fe.for_line, fe.for_pos);
            /* set jump to that line number and set cp position */
            set_jump_to_lineno(fe.for_line->lineno);
            /* store position to resume at fe.for_pos by setting a special global pointer; a simple mechanism: we store in a global temp and when executing we will check it */
            /* We'll set *curpos to fe.for_pos after the jump resolution in RUN loop. We'll communicate via a global pointer. */
            /* But exec loop handles setting curpos when following jumps (see run loop). We'll store the resume pointer in a global. */
            extern const char *resume_pos;
            resume_pos = fe.for_pos;
        }
        *curpos = cp;
        return 0;
    }

    /* GOSUB/RETURN not implemented */
    /* END */
    if (match_keyword("END")) {
        consume_keyword("END");
        end_program_flag = 1;
        return 1;
    }

    /* GOSUB, RETURN (not implemented) -- could be added later */

    /* Unknown / syntax */
    printf("UNKNOWN STATEMENT: '%.20s'\n", cp);
    error_flag = 1;
    return 1;
}

/* A mechanism to pass a resume position after a GOTO created by NEXT: */
const char *resume_pos = NULL;

/* Run program */
void do_run() {
    Line *cur = first_line();
    if (!cur) return;
    jump_to_lineno = 0;
    end_program_flag = 0;
    error_flag = 0;
    resume_pos = NULL;

    /* set starting line */
    Line *start = cur;
    /* start execution at first line in program */
    cur = start;
    const char *pos = cur ? cur->text : NULL;

    while (cur) {
        if (pos == NULL) pos = cur->text;
        /* execute possibly multiple statements separated by ':' or end of text */
        while (1) {
            /* save next line pointer (advance by default) */
            Line *next = cur->next;
            const char *localpos = pos;
            if (!localpos) localpos = cur->text;
            /* exec_statement will update pos and may set jump_to_lineno or end_program_flag */
            int ret = exec_statement(&cur, &localpos);
            pos = localpos;
            if (end_program_flag) return;
            if (error_flag) return;
            if (jump_to_lineno) {
                /* find that line number */
                int target = jump_to_lineno;
                jump_to_lineno = 0;
                Line *t = find_line(target);
                if (!t) { printf("LINE %d NOT FOUND\n", target); error_flag=1; return; }
                cur = t;
                /* if resume_pos global set (from NEXT), set pos accordingly and clear resume_pos */
                if (resume_pos) {
                    pos = resume_pos;
                    resume_pos = NULL;
                } else pos = cur->text;
                break; /* break inner loop and continue at new cur */
            }
            /* check for inline ':' separator (multiple statements on single line) */
            skip_spaces();
            if (pos && *pos == ':') { pos++; continue; }
            /* otherwise advance to next line */
            cur = cur->next;
            pos = cur ? cur->text : NULL;
            break;
        }
        if (end_program_flag || error_flag) break;
    }
}

/* LIST: print program */
void do_list() {
    Line *p = program;
    while (p) {
        printf("%d %s\n", p->lineno, p->text);
        p = p->next;
    }
}

/* Simple immediate commands (no line number) */
void do_immediate(const char *line) {
    /* copy into a modifiable buffer */
    char buf[MAX_LINE_LEN];
    strncpy(buf, line, MAX_LINE_LEN-1);
    buf[MAX_LINE_LEN-1] = '\0';
    const char *t = buf;
    while (isspace((unsigned char)*t)) t++;
    if (isdigit((unsigned char)*t)) {
        /* line edit/insert: parse leading number */
        char *p = (char*)t;
        int lineno = strtol(p, &p, 10);
        while (isspace((unsigned char)*p)) p++;
        insert_line(lineno, p);
        return;
    }
    /* immediate command word */
    /* uppercase copy for checking but we should keep original for statements e.g. PRINT "text" */
    char up[MAX_LINE_LEN];
    for (int i=0; i<MAX_LINE_LEN && t[i]; ++i) up[i] = toupper((unsigned char)t[i]); up[strlen(t)] = '\0';

    if (strncmp(up, "RUN", 3) == 0) { do_run(); return; }
    if (strncmp(up, "LIST", 4) == 0) { do_list(); return; }
    if (strncmp(up, "NEW", 3) == 0) { free_program(); clear_vars(); for_sp = 0; return; }
    if (strncmp(up, "QUIT", 4) == 0 || strncmp(up, "BYE", 3) == 0 || strncmp(up,"EXIT",4)==0) { exit(0); }
    /* Allow immediate LET / PRINT / INPUT etc - execute using exec_statement by building a fake single-line and executing */
    /* Create a temporary line and run exec_statement on it */
    Line tmp;
    tmp.lineno = 0;
    tmp.text = strdup(t);
    Line *tp = &tmp;
    const char *pos = tmp.text;
    while (1) {
        cp = pos;
        jump_to_lineno = 0;
        int ret = exec_statement(&tp, &pos);
        if (end_program_flag || error_flag) { free(tmp.text); return; }
        if (jump_to_lineno) {
            printf("JUMP NOT ALLOWED IN IMMEDIATE MODE\n");
            break;
        }
        skip_spaces();
        if (!pos || *pos == '\0') break;
        if (*pos == ':') { pos++; continue; }
        break;
    }
    free(tmp.text);
}

/* REPL */
int main(int argc, char **argv) {
    printf("Tiny Integer-BASIC-like interpreter (C)\n");
    printf("Type lines with leading numbers to add program lines (e.g. 10 PRINT 1)\n");
    printf("Commands: RUN, LIST, NEW, QUIT\n");
    printf("Statements: LET, PRINT, INPUT, GOTO, IF...THEN, FOR...TO[STEP], NEXT, END, REM\n");
    printf("Variables: A..Z (integers)\n");
    printf("----\n");

    char linebuf[MAX_LINE_LEN];
    clear_vars();
    for_sp = 0;

    while (1) {
        printf("] ");
        if (!fgets(linebuf, sizeof(linebuf), stdin)) break;
        /* strip newline */
        size_t L = strlen(linebuf);
        if (L && linebuf[L-1] == '\n') linebuf[L-1] = '\0';
        /* empty line -> ignore */
        char *p = linebuf;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') continue;
        do_immediate(p);
    }

    return 0;
}
