
// BASIC Interpreter v5 Fix5 - Adds FOR/NEXT support
// Applesoft-like minimal interpreter with PRINT, ?, LET optional, INPUT, IF...THEN, FOR/NEXT, NEW, LIST, RUN
// This is a compact educational interpreter, not a full Applesoft clone.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINES 1000
#define MAX_LINE_LEN 256
#define MAX_VARS 1000
#define MAX_FOR_STACK 256

typedef struct {
    char name[32];
    double value;
    char svalue[128];
    int is_string;
} Variable;

typedef struct {
    int number;
    char text[MAX_LINE_LEN];
} ProgramLine;

static ProgramLine program[MAX_LINES];
static int num_lines = 0;

static Variable vars[MAX_VARS];
static int num_vars = 0;

static char *input_line = NULL;
static int pos = 0;
static int current_line_index = 0;

/* FOR stack */
static char for_varname[MAX_FOR_STACK][32];
static double for_end[MAX_FOR_STACK];
static double for_step[MAX_FOR_STACK];
static int for_line_idx[MAX_FOR_STACK];
static int for_sp = 0;

static void skip_ws() {
    if (!input_line) return;
    while (isspace((unsigned char)input_line[pos])) pos++;
}

static Variable* find_var(const char *name, int create) {
    if (!name || !*name) return NULL;
    for (int i = 0; i < num_vars; i++)
        if (strcasecmp(vars[i].name, name) == 0)
            return &vars[i];
    if (create && num_vars < MAX_VARS) {
        strncpy(vars[num_vars].name, name, sizeof(vars[num_vars].name)-1);
        vars[num_vars].name[sizeof(vars[num_vars].name)-1] = '\0';
        vars[num_vars].value = 0;
        vars[num_vars].svalue[0] = '\0';
        vars[num_vars].is_string = (name[strlen(name)-1] == '$');
        return &vars[num_vars++];
    }
    return NULL;
}

static double parse_expr();

static double parse_number() {
    skip_ws();
    double val = atof(&input_line[pos]);
    while (isdigit((unsigned char)input_line[pos]) || input_line[pos]=='.') pos++;
    return val;
}

static double parse_factor() {
    skip_ws();
    if (!input_line) return 0;
    if (isdigit((unsigned char)input_line[pos]) || input_line[pos]=='.') return parse_number();
    if (isalpha((unsigned char)input_line[pos])) {
        char name[32]; int i=0;
        while (isalnum((unsigned char)input_line[pos]) || input_line[pos]=='$') {
            if (i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]);
            pos++;
        }
        name[i]=0;
        Variable *v = find_var(name,0);
        return v ? v->value : 0;
    }
    if (input_line[pos]=='(') {
        pos++; double val = parse_expr(); skip_ws();
        if (input_line[pos]==')') pos++;
        return val;
    }
    return 0;
}

static double parse_term() {
    double val = parse_factor();
    for (;;) {
        skip_ws();
        char c = input_line[pos];
        if (c=='*' || c=='/') {
            pos++; double rhs = parse_factor();
            if (c=='*') val *= rhs; else val /= rhs;
        } else break;
    }
    return val;
}

static double parse_expr() {
    double val = parse_term();
    for (;;) {
        skip_ws();
        char c = input_line[pos];
        if (c=='+' || c=='-') {
            pos++; double rhs = parse_term();
            if (c=='+') val += rhs; else val -= rhs;
        } else break;
    }
    return val;
}

static void exec_print() {
    skip_ws();
    int first = 1;
    while (input_line && input_line[pos]) {
        if (!first && input_line[pos] == ',') { pos++; skip_ws(); printf(" "); }
        first = 0;
        if (input_line[pos]=='"') {
            pos++;
            while (input_line[pos] && input_line[pos]!='"') putchar(input_line[pos++]);
            if (input_line[pos]=='"') pos++;
        } else {
            double v = parse_expr();
            printf("%g", v);
        }
        skip_ws();
        if (input_line[pos]==0 || input_line[pos]==')') break;
    }
    printf("\n");
}

static void exec_input() {
    for (;;) {
        skip_ws();
        char name[32]; int i=0;
        if (!isalpha((unsigned char)input_line[pos])) { fprintf(stderr,"Syntax error in INPUT\n"); return; }
        while (isalnum((unsigned char)input_line[pos]) || input_line[pos]=='$') {
            if (i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]);
            pos++;
        }
        name[i]=0;
        Variable *v = find_var(name,1);
        printf("? ");
        char buf[128];
        if (!fgets(buf,sizeof(buf),stdin)) buf[0]=0;
        buf[strcspn(buf,"\n")] = 0;
        if (v->is_string) strncpy(v->svalue, buf, sizeof(v->svalue)-1);
        else v->value = atof(buf);
        skip_ws();
        if (input_line[pos] != ',') break;
        pos++; // consume comma
    }
}

static void exec_if() {
    double cond = parse_expr();
    skip_ws();
    // accept relational ops? For simplicity treat nonzero as true
    if (strncasecmp(&input_line[pos],"THEN",4)==0) {
        pos += 4; skip_ws();
        if (cond != 0.0) {
            // if THEN has a line number, jump; otherwise execute inline statement
            if (isdigit((unsigned char)input_line[pos])) {
                int ln = atoi(&input_line[pos]);
                for (int i=0;i<num_lines;i++) if (program[i].number==ln) { current_line_index = i - 1; return; }
            } else {
                exec_print(); // execute a single inline statement (simplified)
            }
        } else {
            // check for ELSE
            if (strncasecmp(&input_line[pos],"ELSE",4)==0) {
                pos += 4; skip_ws();
                exec_print();
            }
        }
    } else {
        fprintf(stderr,"Syntax error: missing THEN\n");
    }
}

/* FOR var = start TO end [STEP n] */
static void exec_for() {
    skip_ws();
    // parse variable name
    char name[32]; int i=0;
    if (!isalpha((unsigned char)input_line[pos])) { fprintf(stderr,"Syntax error in FOR\n"); return; }
    while (isalnum((unsigned char)input_line[pos]) || input_line[pos]=='$') {
        if (i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]);
        pos++;
    }
    name[i]=0;
    skip_ws();
    if (input_line[pos] != '=') { fprintf(stderr,"Syntax error in FOR (missing =)\n"); return; }
    pos++;
    double startv = parse_expr();
    skip_ws();
    // expect TO
    if (strncasecmp(&input_line[pos],"TO",2) != 0) { fprintf(stderr,"Syntax error in FOR (missing TO)\n"); return; }
    pos += 2; skip_ws();
    double endv = parse_expr();
    skip_ws();
    double stepv = 1.0;
    if (strncasecmp(&input_line[pos],"STEP",4) == 0) {
        pos += 4; skip_ws();
        stepv = parse_expr();
    }
    // set variable to start
    Variable *v = find_var(name,1);
    v->value = startv;
    // push for stack
    if (for_sp >= MAX_FOR_STACK) { fprintf(stderr,"FOR stack overflow\n"); return; }
    strncpy(for_varname[for_sp], name, sizeof(for_varname[for_sp])-1);
    for_varname[for_sp][sizeof(for_varname[for_sp])-1]=0;
    for_end[for_sp] = endv;
    for_step[for_sp] = stepv;
    for_line_idx[for_sp] = current_line_index;
    for_sp++;
}

/* NEXT [var] */
static void exec_next() {
    skip_ws();
    char name[32]; int i=0;
    if (isalpha((unsigned char)input_line[pos])) {
        while (isalnum((unsigned char)input_line[pos]) || input_line[pos]=='$') {
            if (i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]);
            pos++;
        }
        name[i]=0;
    } else name[0]=0;
    if (for_sp <= 0) { fprintf(stderr,"NEXT without FOR\n"); return; }
    int idx = for_sp - 1;
    // if name given, ensure matches top of stack (simple behavior)
    if (name[0] != 0 && strcasecmp(name, for_varname[idx]) != 0) {
        fprintf(stderr,"NEXT variable mismatch (expected %s)\n", for_varname[idx]);
        return;
    }
    // increment loop var
    Variable *v = find_var(for_varname[idx], 0);
    if (!v) { fprintf(stderr,"FOR variable not found\n"); return; }
    v->value += for_step[idx];
    // check continuation depending on sign of step
    int continue_loop = 0;
    if (for_step[idx] > 0.0) {
        if (v->value <= for_end[idx]) continue_loop = 1;
    } else {
        if (v->value >= for_end[idx]) continue_loop = 1;
    }
    if (continue_loop) {
        // jump back to line after FOR: set current_line_index so that run loop will increment to next line of FOR
        current_line_index = for_line_idx[idx];
    } else {
        // pop the stack
        for_sp--;
    }
}

static void exec_line(const char *line) {
    pos = 0; input_line = (char*)line;
    skip_ws();
    if (!input_line) return;
    if (strncasecmp(&input_line[pos],"PRINT",5)==0) { pos += 5; exec_print(); input_line = NULL; return; }
    if (input_line[pos] == '?') { pos++; exec_print(); input_line = NULL; return; }
    if (strncasecmp(&input_line[pos],"INPUT",5)==0) { pos += 5; exec_input(); input_line = NULL; return; }
    if (strncasecmp(&input_line[pos],"IF",2)==0) { pos += 2; exec_if(); input_line = NULL; return; }
    if (strncasecmp(&input_line[pos],"FOR",3)==0) { pos += 3; exec_for(); input_line = NULL; return; }
    if (strncasecmp(&input_line[pos],"NEXT",4)==0) { pos += 4; exec_next(); input_line = NULL; return; }
    // assignment A = expr
    if (isalpha((unsigned char)input_line[pos])) {
        char name[32]; int i=0;
        while (isalnum((unsigned char)input_line[pos]) || input_line[pos]=='$') {
            if (i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]);
            pos++;
        }
        name[i]=0;
        skip_ws();
        if (input_line[pos] == '=') {
            pos++;
            double val = parse_expr();
            Variable *v = find_var(name,1);
            v->value = val;
            input_line = NULL;
            return;
        }
    }
    input_line = NULL;
}

static void cmd_list() {
    for (int i=0;i<num_lines;i++) printf("%d %s\n", program[i].number, program[i].text);
}

static void cmd_new() { num_lines = 0; for_sp = 0; num_vars = 0; }

static void run_program() {
    for (current_line_index = 0; current_line_index < num_lines; current_line_index++) {
        exec_line(program[current_line_index].text);
    }
}

static void interactive_mode() {
    char line[MAX_LINE_LEN];
    while (1) {
        printf("] ");
        if (!fgets(line,sizeof(line),stdin)) break;
        line[strcspn(line,"\n")] = 0;
        if (strlen(line) == 0) continue;
        if (strcasecmp(line,"RUN")==0) { run_program(); continue; }
        if (strcasecmp(line,"LIST")==0) { cmd_list(); continue; }
        if (strcasecmp(line,"NEW")==0) { cmd_new(); continue; }
        if (isdigit((unsigned char)line[0])) {
            int num = atoi(line);
            char *txt = strchr(line,' ');
            if (!txt) {
                // delete line
                int found = -1;
                for (int i=0;i<num_lines;i++) if (program[i].number == num) { found = i; break; }
                if (found >= 0) memmove(&program[found], &program[found+1], (num_lines-found-1)*sizeof(ProgramLine));
                if (num_lines>0) num_lines--;
                continue;
            }
            txt++;
            int found = -1;
            for (int i=0;i<num_lines;i++) if (program[i].number == num) { found = i; break; }
            if (found < 0) {
                if (num_lines < MAX_LINES) {
                    program[num_lines].number = num;
                    strncpy(program[num_lines].text, txt, MAX_LINE_LEN-1);
                    program[num_lines].text[MAX_LINE_LEN-1] = '\0';
                    num_lines++;
                } else {
                    fprintf(stderr,"Program full\n");
                }
            } else {
                strncpy(program[found].text, txt, MAX_LINE_LEN-1);
                program[found].text[MAX_LINE_LEN-1] = '\0';
            }
            continue;
        }
        // immediate execution
        pos = 0; input_line = line;
        exec_line(line);
        input_line = NULL;
    }
}

int main() {
    printf("Applesoft-like BASIC Interpreter v5 Fix5\n");
    interactive_mode();
    return 0;
}
