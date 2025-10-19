/*
 * BASIC Interpreter v5_fix6
 * Adds GOTO, GOSUB, RETURN and relational/logical operators (AND/OR/NOT)
 * Interactive, line-numbered program editing: LIST, RUN, NEW, immediate execution
 * Supports PRINT/? INPUT, IF...THEN, FOR/NEXT (simple), variables (A, A$), optional LET
 *
 * This is an educational interpreter, not a full Applesoft implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINES 2000
#define MAX_LINE_LEN 512
#define MAX_VARS 1024
#define MAX_GOSUB 256
#define MAX_FOR_STACK 256

typedef struct {
    char name[32];
    double value;
    char svalue[256];
    int is_string;
} Variable;

typedef struct {
    int number;
    char text[MAX_LINE_LEN];
} ProgramLine;

/* Program storage */
static ProgramLine program[MAX_LINES];
static int num_lines = 0;

/* Variables */
static Variable vars[MAX_VARS];
static int num_vars = 0;

/* Parser state for current input line */
static const char *input_line = NULL;
static int pos = 0;

/* Execution control */
static int current_line_index = 0;

/* GOSUB stack */
static int gosub_stack[MAX_GOSUB];
static int gosub_sp = 0;

/* FOR stack */
static char for_varname[MAX_FOR_STACK][32];
static double for_end[MAX_FOR_STACK];
static double for_step[MAX_FOR_STACK];
static int for_line_idx[MAX_FOR_STACK];
static int for_sp = 0;

/* Helpers */
static void skip_ws(void) {
    if (!input_line) return;
    while (isspace((unsigned char)input_line[pos])) pos++;
}

static void str_upper(char *s) {
    for (; *s; ++s) *s = (char)toupper((unsigned char)*s);
}

static Variable* find_var(const char *name, int create) {
    if (!name) return NULL;
    for (int i=0;i<num_vars;i++) if (strcasecmp(vars[i].name, name)==0) return &vars[i];
    if (!create) return NULL;
    if (num_vars >= MAX_VARS) { fprintf(stderr,"Too many variables\n"); exit(1); }
    Variable *v = &vars[num_vars++];
    memset(v, 0, sizeof(*v));
    strncpy(v->name, name, sizeof(v->name)-1);
    v->name[sizeof(v->name)-1]=0;
    v->is_string = (name[strlen(name)-1] == '$');
    v->value = 0.0;
    v->svalue[0]=0;
    return v;
}

/* Forward declarations for expression parsing (recursive descent) */
static double parse_logical_or(void);
static double parse_logical_and(void);
static double parse_not(void);
static double parse_relational(void);
static double parse_add(void);
static double parse_mul(void);
static double parse_unary(void);
static double parse_primary(void);

/* Parse helpers */
static int match_keyword(const char *kw) {
    skip_ws();
    int len = (int)strlen(kw);
    if (input_line && strncasecmp(input_line + pos, kw, len) == 0) {
        pos += len;
        skip_ws();
        return 1;
    }
    return 0;
}

static int match_char(char c) {
    skip_ws();
    if (input_line && input_line[pos] == c) { pos++; skip_ws(); return 1; }
    return 0;
}

/* Parse primary: number, variable, parenthesis */
static double parse_primary(void) {
    skip_ws();
    if (!input_line) return 0;
    if (input_line[pos] == '(') { pos++; double v = parse_logical_or(); if (input_line[pos]==')') pos++; return v; }
    if (input_line[pos]=='"') {
        // string literal in numeric context: try to parse number from it
        pos++;
        char buf[256]; int i=0;
        while (input_line[pos] && input_line[pos] != '"' && i < (int)sizeof(buf)-1) buf[i++] = input_line[pos++];
        buf[i]=0;
        if (input_line[pos]=='"') pos++;
        skip_ws();
        return atof(buf);
    }
    if (isdigit((unsigned char)input_line[pos]) || input_line[pos]=='.' || (input_line[pos]=='-' && isdigit((unsigned char)input_line[pos+1]))) {
        char numbuf[128]; int i=0;
        if (input_line[pos]=='-') numbuf[i++] = input_line[pos++];
        while (isdigit((unsigned char)input_line[pos]) || input_line[pos]=='.' || input_line[pos]=='e' || input_line[pos]=='E' || input_line[pos]=='+' || input_line[pos]=='-') {
            numbuf[i++] = input_line[pos++];
            if (i >= (int)sizeof(numbuf)-1) break;
        }
        numbuf[i]=0;
        skip_ws();
        return atof(numbuf);
    }
    if (isalpha((unsigned char)input_line[pos])) {
        char name[32]; int i=0;
        while (isalnum((unsigned char)input_line[pos]) || input_line[pos]=='$' || input_line[pos]=='_') {
            if (i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]);
            pos++;
        }
        name[i]=0;
        skip_ws();
        Variable *v = find_var(name, 0);
        if (!v) return 0.0;
        if (v->is_string) return atof(v->svalue);
        return v->value;
    }
    return 0.0;
}

static double parse_unary(void) {
    skip_ws();
    if (match_char('+')) return parse_unary();
    if (match_char('-')) return -parse_unary();
    return parse_primary();
}

static double parse_mul(void) {
    double v = parse_unary();
    for (;;) {
        skip_ws();
        if (match_char('*')) v *= parse_unary();
        else if (match_char('/')) { double r = parse_unary(); if (r != 0.0) v /= r; else v = 0.0; }
        else break;
    }
    return v;
}

static double parse_add(void) {
    double v = parse_mul();
    for (;;) {
        skip_ws();
        if (match_char('+')) v += parse_mul();
        else if (match_char('-')) v -= parse_mul();
        else break;
    }
    return v;
}

static double parse_relational(void) {
    double left = parse_add();
    skip_ws();
    if (!input_line) return left;
    // check multi-char operators first
    if (input_line[pos] == '>' && input_line[pos+1] == '=') { pos+=2; double right = parse_add(); return left >= right ? 1.0 : 0.0; }
    if (input_line[pos] == '<' && input_line[pos+1] == '=') { pos+=2; double right = parse_add(); return left <= right ? 1.0 : 0.0; }
    if (input_line[pos] == '<' && input_line[pos+1] == '>') { pos+=2; double right = parse_add(); return left != right ? 1.0 : 0.0; }
    if (input_line[pos] == '>') { pos++; double right = parse_add(); return left > right ? 1.0 : 0.0; }
    if (input_line[pos] == '<') { pos++; double right = parse_add(); return left < right ? 1.0 : 0.0; }
    if (input_line[pos] == '=') { pos++; double right = parse_add(); return left == right ? 1.0 : 0.0; }
    return left;
}

static double parse_not(void) {
    skip_ws();
    if (match_keyword("NOT")) { double v = parse_not(); return (v == 0.0) ? 1.0 : 0.0; }
    return parse_relational();
}

static double parse_logical_and(void) {
    double v = parse_not();
    for (;;) {
        if (match_keyword("AND")) { double r = parse_not(); v = (v != 0.0 && r != 0.0) ? 1.0 : 0.0; }
        else break;
    }
    return v;
}

static double parse_logical_or(void) {
    double v = parse_logical_and();
    for (;;) {
        if (match_keyword("OR")) { double r = parse_logical_and(); v = (v != 0.0 || r != 0.0) ? 1.0 : 0.0; }
        else break;
    }
    return v;
}

/* Top-level expression evaluator returning numeric value */
static double eval_expression_numeric(const char *expr) {
    input_line = expr;
    pos = 0;
    double v = parse_logical_or();
    input_line = NULL;
    return v;
}

/* Execute commands */

static void cmd_print(const char *rest) {
    input_line = rest; pos = 0;
    // print can handle string literals or expressions
    int first = 1;
    skip_ws();
    while (input_line && input_line[pos]) {
        if (!first) {
            if (input_line[pos]==',') { pos++; printf(" "); skip_ws(); }
        }
        first = 0;
        if (input_line[pos] == '"') {
            pos++;
            while (input_line[pos] && input_line[pos] != '"') putchar(input_line[pos++]);
            if (input_line[pos] == '"') pos++;
        } else {
            double v = parse_logical_or();
            printf("%g", v);
        }
        skip_ws();
        if (input_line[pos]==0) break;
    }
    printf("\n");
    input_line = NULL;
}

static void cmd_input(const char *rest) {
    input_line = rest; pos = 0;
    while (1) {
        skip_ws();
        if (!isalpha((unsigned char)input_line[pos])) { fprintf(stderr,"INPUT: expected variable name\n"); input_line = NULL; return; }
        char name[32]; int i=0;
        while (isalnum((unsigned char)input_line[pos]) || input_line[pos]=='$' || input_line[pos]=='_') {
            if (i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]);
            pos++;
        }
        name[i]=0;
        Variable *v = find_var(name, 1);
        printf("? ");
        char buf[256];
        if (!fgets(buf, sizeof(buf), stdin)) buf[0]=0;
        buf[strcspn(buf,"\n")] = 0;
        if (v->is_string) strncpy(v->svalue, buf, sizeof(v->svalue)-1);
        else v->value = atof(buf);
        skip_ws();
        if (input_line[pos] != ',') break;
        pos++; // consume comma
    }
    input_line = NULL;
}

static void cmd_goto(const char *rest) {
    int target = (int)eval_expression_numeric(rest);
    int found = -1;
    for (int i=0;i<num_lines;i++) if (program[i].number == target) { found = i; break; }
    if (found >= 0) current_line_index = found - 1; // will be incremented by run loop
    else fprintf(stderr,"GOTO: line %d not found\n", target);
}

static void cmd_gosub(const char *rest) {
    int target = (int)eval_expression_numeric(rest);
    int found = -1;
    for (int i=0;i<num_lines;i++) if (program[i].number == target) { found = i; break; }
    if (found >= 0) {
        if (gosub_sp >= MAX_GOSUB) { fprintf(stderr,"GOSUB stack overflow\n"); return; }
        gosub_stack[gosub_sp++] = current_line_index + 1;
        current_line_index = found - 1;
    } else fprintf(stderr,"GOSUB: line %d not found\n", target);
}

static void cmd_return(void) {
    if (gosub_sp <= 0) { fprintf(stderr,"RETURN without GOSUB\n"); return; }
    int ret = gosub_stack[--gosub_sp];
    current_line_index = ret - 1;
}

/* FOR and NEXT handlers (reuse previous implementation) */
static void cmd_for(const char *rest) {
    input_line = rest; pos = 0;
    skip_ws();
    char name[32]; int i=0;
    if (!isalpha((unsigned char)input_line[pos])) { fprintf(stderr,"FOR: bad var\n"); input_line = NULL; return; }
    while (isalnum((unsigned char)input_line[pos]) || input_line[pos]=='$' || input_line[pos]=='_') {
        if (i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]);
        pos++;
    }
    name[i]=0;
    skip_ws();
    if (!match_char('=')) { fprintf(stderr,"FOR: missing '='\n"); input_line = NULL; return; }
    double startv = parse_logical_or();
    skip_ws();
    if (!match_keyword("TO")) { fprintf(stderr,"FOR: missing TO\n"); input_line = NULL; return; }
    double endv = parse_logical_or();
    skip_ws();
    double stepv = 1.0;
    if (match_keyword("STEP")) stepv = parse_logical_or();
    Variable *v = find_var(name,1);
    v->value = startv;
    if (for_sp >= MAX_FOR_STACK) { fprintf(stderr,"FOR stack overflow\n"); input_line = NULL; return; }
    strncpy(for_varname[for_sp], name, sizeof(for_varname[for_sp])-1);
    for_end[for_sp] = endv;
    for_step[for_sp] = stepv;
    for_line_idx[for_sp] = current_line_index;
    for_sp++;
    input_line = NULL;
}

static void cmd_next(const char *rest) {
    // rest may contain optional variable name
    input_line = rest; pos = 0;
    skip_ws();
    char name[32]; int i=0; name[0]=0;
    if (isalpha((unsigned char)input_line[pos])) {
        while (isalnum((unsigned char)input_line[pos]) || input_line[pos]=='$' || input_line[pos]=='_') {
            if (i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]);
            pos++;
        }
        name[i]=0;
    }
    if (for_sp <= 0) { fprintf(stderr,"NEXT without FOR\n"); input_line = NULL; return; }
    int idx = for_sp - 1;
    if (name[0] && strcasecmp(name, for_varname[idx]) != 0) {
        fprintf(stderr,"NEXT var mismatch (expected %s)\n", for_varname[idx]); input_line = NULL; return;
    }
    Variable *v = find_var(for_varname[idx], 0);
    if (!v) { fprintf(stderr,"FOR var not found\n"); input_line = NULL; return; }
    v->value += for_step[idx];
    int cont = 0;
    if (for_step[idx] > 0.0) { if (v->value <= for_end[idx]) cont = 1; }
    else { if (v->value >= for_end[idx]) cont = 1; }
    if (cont) current_line_index = for_line_idx[idx];
    else for_sp--;
    input_line = NULL;
}

/* Execute a single program line text (without the leading line number) */
static void execute_statement(const char *stmt) {
    // Identify the first keyword or token, dispatch accordingly
    // We compare uppercase for keywords; create a local copy
    char tmp[MAX_LINE_LEN];
    strncpy(tmp, stmt, sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
    char up[MAX_LINE_LEN]; strncpy(up, tmp, sizeof(up)-1); up[sizeof(up)-1]=0; str_upper(up);
    // find first token start
    const char *p = stmt;
    while (*p && isspace((unsigned char)*p)) p++;
    // check keywords
    if (strncasecmp(p, "PRINT", 5) == 0) { p += 5; skip_ws(); cmd_print(p); return; }
    if (*p == '?') { p++; cmd_print(p); return; }
    if (strncasecmp(p, "INPUT", 5) == 0) { p += 5; cmd_input(p); return; }
    if (strncasecmp(p, "GOTO", 4) == 0) { p += 4; cmd_goto(p); return; }
    if (strncasecmp(p, "GOSUB", 5) == 0) { p += 5; cmd_gosub(p); return; }
    if (strncasecmp(p, "RETURN", 6) == 0) { cmd_return(); return; }
    if (strncasecmp(p, "IF", 2) == 0) { p += 2; // evaluate condition and then handle THEN
        input_line = p; pos = 0; double cond = parse_logical_or();
        skip_ws();
        if (match_keyword("THEN")) {
            skip_ws();
            // THEN may be a line number or a statement
            if (isdigit((unsigned char)input_line[pos])) {
                int ln = (int)parse_relational(); // reuse to parse number
                int found = -1;
                for (int i=0;i<num_lines;i++) if (program[i].number == ln) { found = i; break; }
                if (found >= 0 && cond != 0.0) current_line_index = found - 1;
            } else {
                // execute inline statement after THEN if condition true
                if (cond != 0.0) execute_statement(input_line + pos);
            }
        } else fprintf(stderr,"IF: missing THEN\n");
        input_line = NULL;
        return;
    }
    if (strncasecmp(p, "FOR", 3) == 0) { p += 3; cmd_for(p); return; }
    if (strncasecmp(p, "NEXT", 4) == 0) { p += 4; cmd_next(p); return; }
    if (strncasecmp(p, "END", 3) == 0) { exit(0); }
    // Assignment A = expr or LET A = expr
    const char *q = p;
    // optional LET
    if (strncasecmp(q, "LET", 3) == 0) q += 3;
    while (*q && isspace((unsigned char)*q)) q++;
    if (isalpha((unsigned char)*q)) {
        // parse name
        char name[32]; int i=0;
        while (*q && (isalnum((unsigned char)*q) || *q=='$' || *q=='_')) { if (i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)*q); q++; }
        name[i]=0;
        const char *after = q;
        while (*after && isspace((unsigned char)*after)) after++;
        if (*after == '=') {
            // evaluate RHS expression
            after++; // skip =
            double val = eval_expression_numeric(after);
            Variable *v = find_var(name,1);
            if (v->is_string) {
                // store numeric as string
                snprintf(v->svalue, sizeof(v->svalue), "%g", val);
            } else v->value = val;
            return;
        }
    }
    // If no command matched, ignore or print error
    if (*p != 0) fprintf(stderr,"Unknown statement: %s\n", p);
}

/* Run program lines in numeric order of storage */
static void run_program(void) {
    for (current_line_index = 0; current_line_index < num_lines; current_line_index++) {
        execute_statement(program[current_line_index].text);
    }
}

/* Listing, storing, interactive loop */
static void list_program(void) {
    // sort by line number temporarily for listing
    int order[MAX_LINES];
    int n = 0;
    for (int i=0;i<num_lines;i++) order[n++]=i;
    // simple sort by line number (bubble-ish for small programs)
    for (int a=0;a<n;a++) for (int b=a+1;b<n;b++) if (program[order[a]].number > program[order[b]].number) { int t=order[a]; order[a]=order[b]; order[b]=t; }
    for (int i=0;i<n;i++) {
        int idx = order[i];
        printf("%d %s\n", program[idx].number, program[idx].text);
    }
}

static void store_line(int lineno, const char *text) {
    // replace if exists, else append
    for (int i=0;i<num_lines;i++) if (program[i].number == lineno) { strncpy(program[i].text, text, MAX_LINE_LEN-1); program[i].text[MAX_LINE_LEN-1]=0; return; }
    if (num_lines >= MAX_LINES) { fprintf(stderr,"Program too large\n"); return; }
    program[num_lines].number = lineno;
    strncpy(program[num_lines].text, text, MAX_LINE_LEN-1); program[num_lines].text[MAX_LINE_LEN-1]=0;
    num_lines++;
}

static void delete_line(int lineno) {
    for (int i=0;i<num_lines;i++) if (program[i].number == lineno) {
        memmove(&program[i], &program[i+1], (num_lines - i - 1) * sizeof(ProgramLine));
        num_lines--; return;
    }
}

static void interactive_mode(void) {
    char line[MAX_LINE_LEN];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue;
        // commands
        char up[MAX_LINE_LEN]; strncpy(up, line, sizeof(up)-1); up[sizeof(up)-1]=0; str_upper(up);
        if (strcmp(up, "RUN") == 0) { run_program(); continue; }
        if (strcmp(up, "LIST") == 0) { list_program(); continue; }
        if (strcmp(up, "NEW") == 0) { num_lines = 0; num_vars = 0; gosub_sp=0; for_sp=0; continue; }
        // line-numbered program editing
        const char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (isdigit((unsigned char)*p)) {
            int lineno = atoi(p);
            const char *rest = strchr(p, ' ');
            if (!rest) { delete_line(lineno); continue; } // delete on bare number
            rest++; // skip space
            // convert leading '?' to PRINT for storage
            while (*rest && isspace((unsigned char)*rest)) rest++;
            if (*rest == '?') {
                char tmp[MAX_LINE_LEN]; snprintf(tmp, sizeof(tmp), "PRINT %s", rest+1); store_line(lineno, tmp);
            } else store_line(lineno, rest);
            continue;
        }
        // immediate execution (ensure input_line set inside called functions)
        // handle immediate commands: LIST, RUN handled above
        // execute_statement expects the statement text
        execute_statement(line);
    }
}

int main(void) {
    printf("BASIC v5_fix6 — GOTO/GOSUB + relational/logical ops added\n");
    interactive_mode();
    return 0;
}
