/*
 * AppleSoft-style BASIC Interpreter (v5 fix3)
 * Now compiles cleanly: added current_line_index global.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>

#define MAX_LINES 1000
#define MAX_VARS 256
#define MAX_NAME 32
#define MAX_STR 256

typedef enum { VAR_NUM, VAR_STR } VarType;

typedef struct {
    char name[MAX_NAME];
    VarType type;
    double value;
    char *sval;
    double *array;
    char **sarray;
    int array_size;
} Variable;

typedef struct {
    int number;
    char text[256];
} Line;

static Line program[MAX_LINES];
static int num_lines = 0;
static Variable vars[MAX_VARS];
static int num_vars = 0;
static int current_line_index = 0;

static char *input_line = NULL;
static int pos = 0;

static Variable *find_var(const char *name, int create);
static double eval_expr(void);
static char *eval_string_expr(void);
static void exec_line(const char *line);

static void skip_ws() { while (input_line && isspace((unsigned char)input_line[pos])) pos++; }

static int match(const char *kw) {
    skip_ws();
    int len = strlen(kw);
    if (!input_line) return 0;
    if (strncasecmp(input_line + pos, kw, len) == 0) {
        pos += len;
        skip_ws();
        return 1;
    }
    return 0;
}

static char *parse_name(void) {
    skip_ws();
    static char name[MAX_NAME];
    int i = 0;
    if (!input_line) return NULL;
    if (!isalpha((unsigned char)input_line[pos])) return NULL;
    while (input_line[pos] && (isalnum((unsigned char)input_line[pos]) || input_line[pos]=='$')) {
        if (i < MAX_NAME-1) name[i++] = (char)toupper((unsigned char)input_line[pos]);
        pos++;
    }
    name[i] = 0;
    skip_ws();
    return name;
}

static Variable *find_var(const char *name, int create) {
    if (!name) return NULL;
    for (int i=0; i<num_vars; i++) {
        if (strcmp(vars[i].name, name)==0)
            return &vars[i];
    }
    if (!create) return NULL;
    if (num_vars >= MAX_VARS) { fprintf(stderr,"Too many variables\n"); exit(1); }
    Variable *v = &vars[num_vars++];
    memset(v, 0, sizeof(Variable));
    strncpy(v->name, name, MAX_NAME-1);
    v->name[MAX_NAME-1] = '\0';
    v->type = (strchr(name, '$') ? VAR_STR : VAR_NUM);
    v->value = 0.0;
    v->sval = NULL;
    v->array = NULL;
    v->sarray = NULL;
    v->array_size = 0;
    return v;
}

static double parse_number() {
    skip_ws();
    double val = 0.0;
    if (!input_line) { fprintf(stderr,"Internal error: no input_line in parse_number\n"); exit(1); }
    if (isdigit((unsigned char)input_line[pos]) || input_line[pos]=='.') {
        val = atof(&input_line[pos]);
        while (isdigit((unsigned char)input_line[pos]) || input_line[pos]=='.') pos++;
    } else {
        fprintf(stderr,"Syntax error (expected number)\n");
        exit(1);
    }
    skip_ws();
    return val;
}

static double eval_factor(void);

static double eval_term(void) {
    double v = eval_factor();
    while (1) {
        if (match("*")) v *= eval_factor();
        else if (match("/")) v /= eval_factor();
        else return v;
    }
}

static double eval_expr(void) {
    double v = eval_term();
    while (1) {
        if (match("+")) v += eval_term();
        else if (match("-")) v -= eval_term();
        else return v;
    }
}

static double eval_factor(void) {
    skip_ws();
    if (!input_line) { fprintf(stderr,"Internal error: no input_line in eval_factor\n"); exit(1); }
    if (match("(")) {
        double v = eval_expr();
        if (!match(")")) { fprintf(stderr,"Missing )\n"); exit(1); }
        return v;
    }
    if (isdigit((unsigned char)input_line[pos]) || input_line[pos]=='.')
        return parse_number();
    if (isalpha((unsigned char)input_line[pos])) {
        char *n = parse_name();
        if(!n) { fprintf(stderr,"Syntax error: expected name\n"); exit(1); }
        Variable *v = find_var(n, 0);
        if (!v) { fprintf(stderr,"Undefined variable %s\n", n); exit(1); }
        if (v->type == VAR_STR) { fprintf(stderr,"Type mismatch: %s\n", n); exit(1); }
        return v->value;
    }
    fprintf(stderr,"Syntax error in expression\n");
    exit(1);
}

static char *eval_string_expr(void) {
    skip_ws();
    static char buf[MAX_STR];
    buf[0]=0;
    if (!input_line) { fprintf(stderr,"Internal error: no input_line in eval_string_expr\n"); exit(1); }
    if (input_line[pos]=='"') {
        pos++;
        int i=0;
        while (input_line[pos] && input_line[pos]!='"' && i<MAX_STR-1) buf[i++]=input_line[pos++];
        buf[i]=0;
        if (input_line[pos]=='"') pos++;
        skip_ws();
    } else {
        char *n=parse_name();
        if(!n) { fprintf(stderr,"Syntax error: expected string name or literal\n"); exit(1); }
        Variable *v=find_var(n,0);
        if (!v||v->type!=VAR_STR||!v->sval){ fprintf(stderr,"Undefined string variable %s\n", n); exit(1);}
        strncpy(buf,v->sval,MAX_STR-1);
    }
    while (match("+")) {
        char *right = eval_string_expr();
        strncat(buf, right, MAX_STR - strlen(buf) - 1);
    }
    return buf;
}

static void exec_print(void) {
    int first = 1;
    while (1) {
        skip_ws();
        if (!first && !match(",")) break;
        first = 0;
        if (!input_line) break;
        if (input_line[pos]=='"') {
            char *s = eval_string_expr();
            printf("%s", s);
        } else if (isalpha((unsigned char)input_line[pos])) {
            char *n=parse_name();
            if(!n) { fprintf(stderr,"Syntax error in PRINT\n"); exit(1); }
            Variable *v=find_var(n,0);
            if (!v){fprintf(stderr,"Undefined var %s\n",n);exit(1);}
            if (v->type==VAR_STR) printf("%s",v->sval?v->sval:"");
            else printf("%g",v->value);
        } else {
            printf("%g", eval_expr());
        }
        skip_ws();
        if (input_line[pos]==0 || input_line[pos]==')') break;
    }
    printf("\n");
}

static void exec_input(void) {
    while (1) {
        char *n = parse_name();
        if(!n) { fprintf(stderr,"Syntax error in INPUT\n"); exit(1); }
        Variable *v = find_var(n, 1);
        printf("? ");
        char linebuf[128]; if(!fgets(linebuf,sizeof(linebuf),stdin)) linebuf[0]=0;
        if (v->type == VAR_STR) {
            linebuf[strcspn(linebuf,"\n")]=0;
            if(v->sval) free(v->sval);
            v->sval = strdup(linebuf);
        } else {
            v->value = atof(linebuf);
        }
        if (!match(",")) break;
    }
}

static void exec_if(void) {
    double cond = eval_expr();
    if (!match("THEN")) { fprintf(stderr,"Syntax error: missing THEN\n"); exit(1); }
    if (cond != 0.0) {
        skip_ws();
        if (isdigit((unsigned char)input_line[pos])) {
            int line_num = (int)eval_expr();
            int found = -1;
            for (int i=0;i<num_lines;i++) if (program[i].number==line_num){ found = i; break; }
            if(found>=0){ pos = 0; current_line_index = found - 1; return; }
            fprintf(stderr,"Undefined line %d\n", line_num); exit(1);
        } else {
            exec_line(&input_line[pos]);
        }
    } else if (match("ELSE")) {
        exec_line(&input_line[pos]);
    }
}

static void exec_line(const char *line) {
    if(input_line) { free(input_line); input_line = NULL; }
    input_line = strdup(line);
    pos = 0;
    skip_ws();

    if (match("?") || match("PRINT")) {
        exec_print();
        free(input_line); input_line = NULL;
        return;
    }

    int saved_pos = pos;
    if (match("LET")) {
        char *name = parse_name();
        if (!name) { fprintf(stderr,"Syntax error after LET\n"); exit(1); }
        char namebuf[MAX_NAME]; strncpy(namebuf, name, MAX_NAME-1); namebuf[MAX_NAME-1]=0;
        if (!match("=")) { fprintf(stderr,"Missing '=' after variable\n"); exit(1); }
        Variable *v = find_var(namebuf, 1);
        skip_ws();
        if (v->type == VAR_STR) {
            char *s = eval_string_expr();
            if(v->sval) free(v->sval);
            v->sval = strdup(s);
        } else {
            v->value = eval_expr();
        }
        free(input_line); input_line = NULL;
        return;
    } else {
        int temp_pos = pos;
        char *nm = parse_name();
        if (nm) {
            char namebuf[MAX_NAME]; strncpy(namebuf, nm, MAX_NAME-1); namebuf[MAX_NAME-1]=0;
            int after = pos;
            skip_ws();
            if (input_line[pos] == '=') {
                pos++; skip_ws();
                Variable *v = find_var(namebuf, 1);
                if (v->type == VAR_STR) {
                    char *s = eval_string_expr();
                    if(v->sval) free(v->sval);
                    v->sval = strdup(s);
                } else {
                    v->value = eval_expr();
                }
                free(input_line); input_line = NULL;
                return;
            } else {
                pos = temp_pos;
            }
        } else {
            pos = saved_pos;
        }
    }

    if (match("INPUT")) { exec_input(); free(input_line); input_line = NULL; return; }
    if (match("IF")) { exec_if(); free(input_line); input_line = NULL; return; }
    if (match("END")) { free(input_line); input_line = NULL; exit(0); }
    if (strlen(line)==0) { free(input_line); input_line = NULL; return; }

    fprintf(stderr,"Unknown or unsupported command: %s\n", line);
    free(input_line); input_line = NULL;
}

static void run_program(void) {
    for (int i=0; i<num_lines; i++) {
        current_line_index = i;
        exec_line(program[i].text);
    }
}

static void list_program(void) {
    for (int i=0;i<num_lines;i++) printf("%d %s\n",program[i].number,program[i].text);
}

static void new_program(void) { num_lines=0; }

int main(void) {
    char line[256];
    printf("APPLE BASIC v5 READY.\n");
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line,sizeof(line),stdin)) break;
        line[strcspn(line,"\n")] = 0;
        if (strlen(line)==0) continue;
        if (isdigit((unsigned char)line[0])) {
            int num=atoi(line);
            char *txt=strchr(line,' ');
            if (!txt) { for (int i=0;i<num_lines;i++) if (program[i].number==num){ for (int j=i;j<num_lines-1;j++) program[j]=program[j+1]; num_lines--; } continue; }
            txt++;
            int i; for (i=0;i<num_lines;i++) if (program[i].number==num) break;
            if (i<num_lines && program[i].number==num) strncpy(program[i].text,txt,sizeof(program[i].text)-1);
            else { if(num_lines>=MAX_LINES){fprintf(stderr,"Program too large\n"); continue;} program[num_lines].number=num; strncpy(program[num_lines].text,txt,sizeof(program[num_lines].text)-1); num_lines++; }
        } else if (strcasecmp(line,"RUN")==0) run_program();
        else if (strcasecmp(line,"LIST")==0) list_program();
        else if (strcasecmp(line,"NEW")==0) new_program();
        else exec_line(line);
    }
    return 0;
}
