/*
  intbasic_plus_fixed.c -- Tiny BASIC+ interpreter (single-file)
  Features:
    - multi-char variable names, string vars end with $
    - 1-D numeric arrays via DIM NAME(n) (1-based)
    - string variables and functions: LEN, LEFT$, RIGHT$, MID$, CHR$, ASC
    - GOSUB/RETURN, FOR/NEXT, IF...THEN, GOTO, LET/assignment, PRINT, INPUT
    - expressions with + - * / and parentheses; + concatenates if either operand is string
    - fixed identifier parsing bug and correct array element evaluation

  Compile:
    gcc -O2 -o intbasic_plus_fixed intbasic_plus_fixed.c

  Run:
    ./intbasic_plus_fixed
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE_LEN 1024
#define MAX_TOKEN_LEN 256
#define MAX_FOR_STACK 256
#define MAX_GOSUB_DEPTH 256
#define DEFAULT_STR_MAX 255

/* ---------- Program storage ---------- */
typedef struct Line {
    int lineno;
    char *text;
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

void insert_line(int lineno, const char *text) {
    if (lineno <= 0) return;
    Line **pp = &program;
    while (*pp && (*pp)->lineno < lineno) pp = &(*pp)->next;
    if (*pp && (*pp)->lineno == lineno) {
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
    if (!text || text[0] == '\0') return;
    Line *n = malloc(sizeof(Line));
    n->lineno = lineno;
    n->text = strdup(text);
    n->next = *pp;
    *pp = n;
}

Line *find_line(int lineno) {
    Line *p = program;
    while (p) {
        if (p->lineno == lineno) return p;
        if (p->lineno > lineno) return NULL;
        p = p->next;
    }
    return NULL;
}
Line *first_line() { return program; }

/* ---------- Symbol table and values ---------- */
typedef enum { T_NUM, T_STR, T_ARRAY } ValType;

typedef struct Array {
    int size;   /* 1-based indexing, size elements */
    int *data;  /* allocated size integers */
} Array;

typedef struct Var {
    char *name;      /* uppercase name, includes trailing $ for string vars */
    ValType type;
    int num;         /* numeric value if T_NUM */
    char *str;       /* malloced string if T_STR */
    Array *arr;      /* array metadata if T_ARRAY */
    struct Var *next;
} Var;

Var *symtab = NULL;

Var *find_var(const char *name) {
    Var *p = symtab;
    while (p) {
        if (strcmp(p->name, name) == 0) return p;
        p = p->next;
    }
    return NULL;
}

Var *create_var(const char *name, ValType type) {
    Var *v = find_var(name);
    if (v) {
        /* convert if necessary */
        if (v->type != type) {
            if (v->type == T_STR && v->str) { free(v->str); v->str = NULL; }
            if (v->type == T_ARRAY && v->arr) {
                free(v->arr->data);
                free(v->arr);
                v->arr = NULL;
            }
            v->type = type;
        }
        return v;
    }
    v = malloc(sizeof(Var));
    v->name = strdup(name);
    v->type = type;
    v->num = 0;
    v->str = NULL;
    v->arr = NULL;
    v->next = symtab;
    symtab = v;
    return v;
}

void free_symtab() {
    Var *p = symtab;
    while (p) {
        Var *n = p->next;
        free(p->name);
        if (p->type == T_STR && p->str) free(p->str);
        if (p->type == T_ARRAY && p->arr) {
            free(p->arr->data);
            free(p->arr);
        }
        free(p);
        p = n;
    }
    symtab = NULL;
}

/* Normalizes and copies an identifier (uppercase) */
void normalize_name(const char *src, char *dst) {
    int i=0;
    while (*src && (isalnum((unsigned char)*src) || *src == '$') && i < MAX_TOKEN_LEN-1) {
        dst[i++] = toupper((unsigned char)*src++);
        if (dst[i-1] == '$') break;
    }
    dst[i] = '\0';
}

/* ---------- Parsing helpers ---------- */
const char *cp; /* global parse pointer for current statement */

void skip_spaces() {
    while (*cp && isspace((unsigned char)*cp)) cp++;
}

int isidentstart(char c) { return isalpha((unsigned char)c); }
int isidentchar(char c) { return isalnum((unsigned char)c) || c == '$'; }

/* Fixed parse_ident: copies characters into buf and advances cp exactly once per char.
   Accepts multi-char names; trailing $ ends the identifier. Returns 1 if an identifier was parsed. */
int parse_ident(char *buf) {
    skip_spaces();
    const char *p = cp;
    if (!isidentstart(*p)) return 0;
    int i = 0;
    while (*p && isidentchar(*p) && i < MAX_TOKEN_LEN-1) {
        char ch = *p;
        buf[i++] = toupper((unsigned char)ch);
        p++;
        if (buf[i-1] == '$') break; /* stop at trailing $ */
    }
    buf[i] = '\0';
    if (i == 0) return 0;
    cp = p; /* advance global pointer exactly to p */
    return 1;
}

/* parse integer number */
int parse_number(int *out) {
    skip_spaces();
    const char *p = cp;
    int sign = 1;
    if (*p == '+') { p++; }
    else if (*p == '-') { sign = -1; p++; }
    if (!isdigit((unsigned char)*p)) return 0;
    long val = 0;
    while (isdigit((unsigned char)*p)) { val = val * 10 + (*p - '0'); p++; }
    *out = (int)(val * sign);
    cp = p;
    return 1;
}

/* parse string literal "..." */
int parse_string_literal(char **out) {
    skip_spaces();
    if (*cp != '"') return 0;
    cp++; /* skip " */
    char tmp[MAX_LINE_LEN];
    int i = 0;
    while (*cp && *cp != '"' && i < MAX_LINE_LEN-1) {
        if (*cp == '\\' && cp[1]) {
            cp++;
            if (*cp == 'n') tmp[i++] = '\n';
            else tmp[i++] = *cp;
            cp++;
            continue;
        }
        tmp[i++] = *cp++;
    }
    tmp[i] = '\0';
    if (*cp == '"') cp++;
    *out = strdup(tmp);
    return 1;
}

/* ---------- Expression evaluation ---------- */

typedef struct Value {
    ValType type; /* T_NUM or T_STR */
    int num;
    char *str; /* allocated for strings; NULL for numbers */
} Value;

Value make_num(int n) { Value v; v.type = T_NUM; v.num = n; v.str = NULL; return v; }
Value make_str_take(char *s) { Value v; v.type = T_STR; v.num = 0; v.str = s ? s : strdup(""); return v; }
Value make_str_dup(const char *s) { Value v; v.type = T_STR; v.num = 0; v.str = strdup(s ? s : ""); return v; }
void free_value(Value *v) { if (v->type == T_STR && v->str) { free(v->str); v->str = NULL; } v->type = T_NUM; v->num = 0; }

/* helper: convert Value to newly allocated string */
char *value_to_string(const Value *v) {
    if (v->type == T_STR) return strdup(v->str ? v->str : "");
    char tmp[64]; snprintf(tmp, sizeof(tmp), "%d", v->num); return strdup(tmp);
}

/* forward declarations */
Value parse_expression();
Value parse_term();
Value parse_factor();

/* Array helpers (return 1 on success) */
int create_array(const char *name, int size) {
    if (size <= 0) return 0;
    Var *v = find_var(name);
    if (!v) v = create_var(name, T_ARRAY);
    if (v->type == T_STR && v->str) { free(v->str); v->str = NULL; }
    if (v->type == T_ARRAY && v->arr) { free(v->arr->data); free(v->arr); v->arr = NULL; }
    v->type = T_ARRAY;
    v->arr = malloc(sizeof(Array));
    v->arr->size = size;
    v->arr->data = calloc(size, sizeof(int));
    return 1;
}
int set_array_element(const char *name, int index, int value) {
    Var *v = find_var(name);
    if (!v) return 0;
    if (v->type != T_ARRAY) return 0;
    if (index < 1 || index > v->arr->size) return 0;
    v->arr->data[index-1] = value;
    return 1;
}
int get_array_element(const char *name, int index, int *out) {
    Var *v = find_var(name);
    if (!v) return 0;
    if (v->type != T_ARRAY) return 0;
    if (index < 1 || index > v->arr->size) return 0;
    *out = v->arr->data[index-1];
    return 1;
}

/* get variable numeric or string value (for non-array vars) */
Value get_var_value(const char *name) {
    Var *v = find_var(name);
    if (!v) {
        /* create numeric by default unless trailing $ */
        if (name[strlen(name)-1] == '$') v = create_var(name, T_STR);
        else v = create_var(name, T_NUM);
    }
    if (v->type == T_NUM) return make_num(v->num);
    else if (v->type == T_STR) return make_str_dup(v->str ? v->str : "");
    /* array shouldn't be returned here; caller should have used array access */
    return make_num(0);
}

/* parse factor:
   - number
   - string literal
   - parenthesis (expr)
   - identifier (variable or array element or function call)
*/
Value parse_factor() {
    skip_spaces();
    if (*cp == '(') {
        cp++;
        Value v = parse_expression();
        skip_spaces();
        if (*cp == ')') cp++;
        return v;
    }
    if (*cp == '"') {
        char *s = NULL;
        if (parse_string_literal(&s)) return make_str_take(s);
    }
    int n;
    if (parse_number(&n)) return make_num(n);

    /* identifier or function or array element */
    char name[MAX_TOKEN_LEN];
    const char *saved_cp = cp;
    if (parse_ident(name)) {
        skip_spaces();
        /* function calls: recognized by name + '(' and known name */
        if (toupper(name[0]) && *cp == '(') {
            /* Some functions (LEN, LEFT$, RIGHT$, MID$, CHR$, ASC) handled explicitly */
            /* Peek name uppercase */
            char upname[MAX_TOKEN_LEN]; strncpy(upname, name, MAX_TOKEN_LEN); for (int i=0;i<(int)strlen(upname);++i) upname[i]=toupper((unsigned char)upname[i]);
            if (strcmp(upname,"LEN")==0 || strcmp(upname,"LEN$")==0) {
                cp++; Value s = parse_expression(); skip_spaces(); if (*cp == ')') cp++;
                int res = (s.type == T_STR) ? (int)strlen(s.str ? s.str : "") : (int)strlen(value_to_string(&s));
                free_value(&s);
                return make_num(res);
            } else if (strcmp(upname,"LEFT$")==0 || strcmp(upname,"LEFT")==0) {
                cp++;
                Value s = parse_expression();
                skip_spaces(); if (*cp==',') cp++;
                int cnt=0; parse_number(&cnt);
                skip_spaces(); if (*cp==')') cp++;
                char *str = value_to_string(&s);
                int len = (int)strlen(str);
                int take = cnt < 0 ? 0 : (cnt > len ? len : cnt);
                char *out = malloc(take+1); memcpy(out, str, take); out[take]=0;
                free(str); free_value(&s);
                return make_str_take(out);
            } else if (strcmp(upname,"RIGHT$")==0 || strcmp(upname,"RIGHT")==0) {
                cp++;
                Value s = parse_expression();
                skip_spaces(); if (*cp==',') cp++;
                int cnt=0; parse_number(&cnt);
                skip_spaces(); if (*cp==')') cp++;
                char *str = value_to_string(&s);
                int len = (int)strlen(str);
                int take = cnt < 0 ? 0 : (cnt > len ? len : cnt);
                char *out = malloc(take+1);
                int start = len - take; if (start < 0) start = 0;
                memcpy(out, str+start, take); out[take]=0;
                free(str); free_value(&s);
                return make_str_take(out);
            } else if (strcmp(upname,"MID$")==0 || strcmp(upname,"MID")==0) {
                cp++;
                Value s = parse_expression();
                skip_spaces(); if (*cp==',') cp++;
                int st=0; parse_number(&st);
                skip_spaces(); if (*cp==',') cp++;
                int cnt=0; parse_number(&cnt);
                skip_spaces(); if (*cp==')') cp++;
                char *str = value_to_string(&s);
                int len = (int)strlen(str);
                int start = st-1; if (start < 0) start = 0;
                if (start > len) start = len;
                int take = cnt < 0 ? 0 : (cnt > len-start ? len-start : cnt);
                char *out = malloc(take+1);
                memcpy(out, str+start, take); out[take]=0;
                free(str); free_value(&s);
                return make_str_take(out);
            } else if (strcmp(upname,"CHR$")==0 || strcmp(upname,"CHR")==0) {
                cp++;
                int v = 0; parse_number(&v);
                skip_spaces(); if (*cp==')') cp++;
                char *o = malloc(2); o[0] = (char)(v & 0xFF); o[1]=0;
                return make_str_take(o);
            } else if (strcmp(upname,"ASC")==0) {
                cp++;
                Value s = parse_expression(); skip_spaces(); if (*cp==')') cp++;
                char *tmp = value_to_string(&s);
                int r = tmp[0] ? (unsigned char)tmp[0] : 0;
                free(tmp); free_value(&s);
                return make_num(r);
            }
            /* if not one of the above functions, fall through to array handling below */
            cp = saved_cp; /* reset; we'll handle variable/array or functionless case by identifier handling below */
            if (!parse_ident(name)) return make_num(0); /* should not happen */
        }

        /* After parsing identifier, check for array reference A(expr) */
        skip_spaces();
        if (*cp == '(') {
            /* array access */
            cp++; /* skip '(' */
            Value idxv = parse_expression();
            int idx = (idxv.type == T_NUM) ? idxv.num : atoi(idxv.str ? idxv.str : "0");
            free_value(&idxv);
            skip_spaces();
            if (*cp == ')') cp++;
            int outv = 0;
            if (!get_array_element(name, idx, &outv)) {
                printf("ARRAY ERROR: %s(%d)\n", name, idx);
                return make_num(0);
            }
            return make_num(outv);
        } else {
            /* plain variable */
            return get_var_value(name);
        }
    }

    /* unrecognized factor -> 0 */
    return make_num(0);
}

Value parse_term() {
    Value left = parse_factor();
    while (1) {
        skip_spaces();
        if (*cp == '*') {
            cp++;
            Value right = parse_factor();
            int a = (left.type==T_NUM) ? left.num : atoi(left.str ? left.str : "0");
            int b = (right.type==T_NUM) ? right.num : atoi(right.str ? right.str : "0");
            free_value(&left); free_value(&right);
            left = make_num(a * b);
        } else if (*cp == '/') {
            cp++;
            Value right = parse_factor();
            int a = (left.type==T_NUM) ? left.num : atoi(left.str ? left.str : "0");
            int b = (right.type==T_NUM) ? right.num : atoi(right.str ? right.str : "0");
            free_value(&left); free_value(&right);
            if (b == 0) { printf("DIVIDE BY ZERO\n"); left = make_num(0); }
            else left = make_num(a / b);
        } else break;
    }
    return left;
}

Value parse_expression() {
    Value left = parse_term();
    while (1) {
        skip_spaces();
        if (*cp == '+') {
            cp++;
            Value right = parse_term();
            if (left.type == T_STR || right.type == T_STR) {
                char *a = value_to_string(&left);
                char *b = value_to_string(&right);
                int la = (int)strlen(a), lb = (int)strlen(b);
                char *out = malloc(la+lb+1);
                memcpy(out, a, la); memcpy(out+la, b, lb); out[la+lb]=0;
                free(a); free(b);
                free_value(&left); free_value(&right);
                left = make_str_take(out);
            } else {
                int r = left.num + right.num;
                free_value(&left); free_value(&right);
                left = make_num(r);
            }
        } else if (*cp == '-') {
            cp++;
            Value right = parse_term();
            int a = (left.type == T_NUM) ? left.num : atoi(left.str ? left.str : "0");
            int b = (right.type == T_NUM) ? right.num : atoi(right.str ? right.str : "0");
            free_value(&left); free_value(&right);
            left = make_num(a - b);
        } else break;
    }
    return left;
}

/* ---------- Assignment helpers ---------- */
void set_var_num(const char *name, int v) {
    Var *var = find_var(name);
    if (!var) var = create_var(name, T_NUM);
    if (var->type == T_STR) {
        if (var->str) free(var->str);
        char tmp[64]; snprintf(tmp, sizeof(tmp), "%d", v);
        var->str = strdup(tmp);
    } else if (var->type == T_NUM) {
        var->num = v;
    } else if (var->type == T_ARRAY) {
        /* setting whole array unsupported here */
    }
}

void set_var_str(const char *name, const char *s) {
    Var *var = find_var(name);
    if (!var) var = create_var(name, T_STR);
    if (var->type == T_ARRAY) return;
    if (var->type == T_NUM) {
        /* convert to string variable */
        var->type = T_STR;
        if (var->str) free(var->str);
        var->str = strdup(s);
    } else {
        if (var->str) free(var->str);
        var->str = strdup(s);
    }
}

/* ---------- FOR stack ---------- */
typedef struct ForEntry {
    char varname[MAX_TOKEN_LEN];
    int end;
    int step;
    Line *for_line;
    const char *for_pos;
} ForEntry;
ForEntry forstack[MAX_FOR_STACK];
int for_sp = 0;
int push_for_entry(const char *varname, int end, int step, Line *fline, const char *fpos) {
    if (for_sp >= MAX_FOR_STACK) return 0;
    strncpy(forstack[for_sp].varname, varname, MAX_TOKEN_LEN-1);
    forstack[for_sp].varname[MAX_TOKEN_LEN-1] = '\0';
    forstack[for_sp].end = end;
    forstack[for_sp].step = step;
    forstack[for_sp].for_line = fline;
    forstack[for_sp].for_pos = fpos;
    for_sp++;
    return 1;
}
int pop_for_entry(const char *varname, ForEntry *out) {
    for (int i = for_sp-1; i >= 0; --i) {
        if (strcmp(forstack[i].varname, varname) == 0) {
            *out = forstack[i];
            for_sp = i;
            return 1;
        }
    }
    return 0;
}

/* ---------- GOSUB stack ---------- */
typedef struct GosubEntry {
    Line *ret_line;
    const char *ret_pos;
} GosubEntry;
GosubEntry gosub_stack[MAX_GOSUB_DEPTH];
int gosub_sp = 0;
int push_gosub(Line *ret_line, const char *ret_pos) {
    if (gosub_sp >= MAX_GOSUB_DEPTH) return 0;
    gosub_stack[gosub_sp].ret_line = ret_line;
    gosub_stack[gosub_sp].ret_pos = ret_pos;
    gosub_sp++;
    return 1;
}
int pop_gosub(Line **out_line, const char **out_pos) {
    if (gosub_sp <= 0) return 0;
    gosub_sp--;
    *out_line = gosub_stack[gosub_sp].ret_line;
    *out_pos = gosub_stack[gosub_sp].ret_pos;
    return 1;
}

/* ---------- Execution engine ---------- */
int jump_to_lineno = 0;
int end_program_flag = 0;
int error_flag = 0;
const char *resume_pos = NULL;

void set_jump(int lineno) { jump_to_lineno = lineno; }

#define MATCH_KW(k) (strncasecmp(cp, k, strlen(k))==0 && (isspace((unsigned char)cp[strlen(k)]) || cp[strlen(k)]==0 || cp[strlen(k)]==',' || cp[strlen(k)]==':' || cp[strlen(k)]=='('))
#define CONSUME_KW(k) do { cp += strlen(k); } while(0)

/* Try assignment (handles arrays) - returns 1 if assignment handled, 0 if not matched */
int parse_and_do_assignment() {
    const char *save = cp;
    char name[MAX_TOKEN_LEN];
    if (!parse_ident(name)) { cp = save; return 0; }
    skip_spaces();
    if (*cp == '(') {
        /* array element assignment: NAME(expr) = expr */
        cp++; /* skip '(' */
        Value idxv = parse_expression();
        int idx = (idxv.type == T_NUM) ? idxv.num : atoi(idxv.str ? idxv.str : "0");
        free_value(&idxv);
        skip_spaces();
        if (*cp == ')') cp++;
        skip_spaces();
        if (*cp != '=') { cp = save; return 0; }
        cp++; /* skip '=' */
        Value rhs = parse_expression();
        int rval = (rhs.type == T_NUM) ? rhs.num : atoi(rhs.str ? rhs.str : "0");
        if (!set_array_element(name, idx, rval)) {
            printf("ARRAY ASSIGN ERROR: %s(%d)\n", name, idx);
            error_flag = 1;
        }
        free_value(&rhs);
        return 1;
    } else {
        /* simple variable assignment NAME = expr */
        skip_spaces();
        if (*cp != '=') { cp = save; return 0; }
        cp++;
        Value rhs = parse_expression();
        if (name[strlen(name)-1] == '$') {
            char *s = value_to_string(&rhs);
            set_var_str(name, s);
            free(s);
        } else {
            int rv = (rhs.type == T_NUM) ? rhs.num : atoi(rhs.str ? rhs.str : "0");
            set_var_num(name, rv);
        }
        free_value(&rhs);
        return 1;
    }
}

/* Execute one statement starting at cp; curlp points to current Line* so statements can set jumps */
int exec_statement(Line **curlp, const char **curpos) {
    cp = *curpos;
    skip_spaces();
    if (*cp == '\0') { *curpos = cp; return 0; }
    const char *startcp = cp;

    if (MATCH_KW("REM")) { CONSUME_KW("REM"); cp = strchr(cp, '\0'); *curpos = cp; return 0; }

    if (MATCH_KW("LET")) { CONSUME_KW("LET"); }

    /* assignment first */
    if (parse_and_do_assignment()) { *curpos = cp; return 0; }

    /* PRINT */
    if (MATCH_KW("PRINT")) {
        CONSUME_KW("PRINT");
        skip_spaces();
        while (1) {
            if (*cp == '"') {
                char *s = NULL;
                parse_string_literal(&s);
                printf("%s", s);
                free(s);
            } else {
                Value v = parse_expression();
                if (v.type == T_STR) printf("%s", v.str ? v.str : "");
                else printf("%d", v.num);
                free_value(&v);
            }
            skip_spaces();
            if (*cp == ',') { printf(" "); cp++; continue; }
            if (*cp == ';') { cp++; continue; }
            break;
        }
        putchar('\n');
        *curpos = cp;
        return 0;
    }

    /* INPUT var */
    if (MATCH_KW("INPUT")) {
        CONSUME_KW("INPUT"); skip_spaces();
        char name[MAX_TOKEN_LEN];
        if (!parse_ident(name)) { printf("SYNTAX ERROR IN INPUT\n"); error_flag=1; return 1; }
        skip_spaces();
        printf("? "); fflush(stdout);
        char buf[1024];
        if (!fgets(buf, sizeof(buf), stdin)) { end_program_flag=1; return 1; }
        size_t L = strlen(buf); if (L && buf[L-1]=='\n') buf[L-1]=0;
        if (name[strlen(name)-1] == '$') set_var_str(name, buf);
        else set_var_num(name, atoi(buf));
        *curpos = cp;
        return 0;
    }

    /* GOTO */
    if (MATCH_KW("GOTO")) {
        CONSUME_KW("GOTO"); skip_spaces();
        int ln; if (!parse_number(&ln)) { printf("SYNTAX ERROR IN GOTO\n"); error_flag=1; return 1; }
        set_jump(ln); *curpos = cp; return 0;
    }

    /* GOSUB */
    if (MATCH_KW("GOSUB")) {
        CONSUME_KW("GOSUB"); skip_spaces();
        int ln; if (!parse_number(&ln)) { printf("SYNTAX ERROR IN GOSUB\n"); error_flag=1; return 1; }
        Line *curl = *curlp; const char *retpos = cp;
        if (!push_gosub(curl, retpos)) { printf("GOSUB STACK OVERFLOW\n"); error_flag=1; return 1; }
        set_jump(ln); *curpos = cp; return 0;
    }

    /* RETURN */
    if (MATCH_KW("RETURN")) {
        CONSUME_KW("RETURN");
        Line *rl; const char *rpos;
        if (!pop_gosub(&rl, &rpos)) { printf("RETURN WITHOUT GOSUB\n"); error_flag=1; return 1; }
        set_jump(rl->lineno);
        resume_pos = rpos;
        *curpos = cp;
        return 0;
    }

    /* IF ... THEN ... */
    if (MATCH_KW("IF")) {
        CONSUME_KW("IF");
        Value left = parse_expression();
        skip_spaces();
        int rel = 0;
        if (*cp == '=') { rel = 1; cp++; }
        else if (*cp == '<') { cp++; if (*cp == '>') { rel = 2; cp++; } else if (*cp == '=') { rel = 5; cp++; } else rel = 3; }
        else if (*cp == '>') { cp++; if (*cp == '=') { rel = 6; cp++; } else rel = 4; }
        else { printf("SYNTAX ERROR IN IF\n"); error_flag=1; free_value(&left); return 1; }
        Value right = parse_expression();
        int L = (left.type==T_NUM) ? left.num : atoi(left.str ? left.str : "0");
        int R = (right.type==T_NUM) ? right.num : atoi(right.str ? right.str : "0");
        free_value(&left); free_value(&right);
        int cond = 0;
        switch(rel) { case 1: cond = (L==R); break; case 2: cond = (L!=R); break; case 3: cond = (L<R); break; case 4: cond = (L>R); break; case 5: cond = (L<=R); break; case 6: cond = (L>=R); break; }
        skip_spaces();
        if (!MATCH_KW("THEN")) { printf("SYNTAX ERROR (expected THEN)\n"); error_flag=1; return 1; }
        CONSUME_KW("THEN"); skip_spaces();
        if (!cond) { *curpos = cp; return 0; }
        /* THEN: either a lineno or an inline statement */
        const char *sav = cp;
        int ln;
        if (parse_number(&ln)) { set_jump(ln); *curpos = cp; return 0; }
        else {
            if (exec_statement(curlp, &cp)) { /* may set jumps */ }
            *curpos = cp;
            return 0;
        }
    }

    /* FOR ... TO [STEP] */
    if (MATCH_KW("FOR")) {
        CONSUME_KW("FOR"); skip_spaces();
        char vname[MAX_TOKEN_LEN];
        if (!parse_ident(vname)) { printf("SYNTAX ERROR IN FOR\n"); error_flag=1; return 1; }
        skip_spaces();
        if (*cp != '=') { printf("SYNTAX ERROR IN FOR (no =)\n"); error_flag=1; return 1; }
        cp++;
        Value sv = parse_expression();
        int s = (sv.type==T_NUM) ? sv.num : atoi(sv.str ? sv.str : "0"); free_value(&sv);
        if (!MATCH_KW("TO")) { printf("SYNTAX ERROR IN FOR (no TO)\n"); error_flag=1; return 1; }
        CONSUME_KW("TO");
        Value ev = parse_expression(); int e = (ev.type==T_NUM) ? ev.num : atoi(ev.str ? ev.str : "0"); free_value(&ev);
        int step = 1; skip_spaces();
        if (MATCH_KW("STEP")) { CONSUME_KW("STEP"); parse_number(&step); }
        set_var_num(vname, s);
        Line *fl = *curlp; const char *pos_after = cp;
        if (!push_for_entry(vname, e, step, fl, pos_after)) { printf("FOR STACK OVERFLOW\n"); error_flag=1; return 1; }
        *curpos = cp; return 0;
    }

    /* NEXT var */
    if (MATCH_KW("NEXT")) {
        CONSUME_KW("NEXT"); skip_spaces();
        char vname[MAX_TOKEN_LEN];
        if (!parse_ident(vname)) { printf("SYNTAX ERROR IN NEXT\n"); error_flag=1; return 1; }
        ForEntry fe;
        if (!pop_for_entry(vname, &fe)) { printf("NEXT WITHOUT FOR\n"); error_flag=1; return 1; }
        Var *vv = find_var(fe.varname);
        if (!vv) { printf("LOOP VAR MISSING\n"); error_flag=1; return 1; }
        int val = (vv->type == T_NUM) ? vv->num : atoi(vv->str ? vv->str : "0");
        val += fe.step; set_var_num(fe.varname, val);
        int cont = 0;
        if (fe.step > 0) { if (val <= fe.end) cont = 1; } else { if (val >= fe.end) cont = 1; }
        if (cont) {
            push_for_entry(fe.varname, fe.end, fe.step, fe.for_line, fe.for_pos);
            set_jump(fe.for_line->lineno);
            resume_pos = fe.for_pos;
        }
        *curpos = cp; return 0;
    }

    /* DIM */
    if (MATCH_KW("DIM")) {
        CONSUME_KW("DIM"); skip_spaces();
        char name[MAX_TOKEN_LEN];
        if (!parse_ident(name)) { printf("SYNTAX ERROR IN DIM\n"); error_flag=1; return 1; }
        skip_spaces();
        if (*cp != '(') { printf("SYNTAX ERROR IN DIM\n"); error_flag=1; return 1; }
        cp++;
        Value idx = parse_expression();
        int size = (idx.type==T_NUM) ? idx.num : atoi(idx.str ? idx.str : "0");
        free_value(&idx);
        skip_spaces(); if (*cp == ')') cp++;
        if (!create_array(name, size)) { printf("DIM ERROR\n"); error_flag=1; return 1; }
        *curpos = cp; return 0;
    }

    /* END */
    if (MATCH_KW("END")) { CONSUME_KW("END"); end_program_flag = 1; return 1; }

    /* If none matched, try assignment again (without LET) */
    cp = startcp;
    if (parse_and_do_assignment()) { *curpos = cp; return 0; }

    printf("UNKNOWN STATEMENT: '%.40s'\n", cp);
    error_flag = 1;
    return 1;
}

/* ---------- Run loop ---------- */
void do_run() {
    Line *cur = first_line();
    if (!cur) return;
    jump_to_lineno = 0;
    end_program_flag = 0;
    error_flag = 0;
    resume_pos = NULL;

    cur = first_line();
    const char *pos = cur ? cur->text : NULL;

    while (cur) {
        if (!pos) pos = cur->text;
        while (1) {
            Line *next = cur->next;
            const char *localpos = pos;
            int ret = exec_statement(&cur, &localpos);
            pos = localpos;
            if (end_program_flag) return;
            if (error_flag) return;
            if (jump_to_lineno) {
                int tgt = jump_to_lineno; jump_to_lineno = 0;
                Line *t = find_line(tgt);
                if (!t) { printf("LINE %d NOT FOUND\n", tgt); error_flag=1; return; }
                cur = t;
                if (resume_pos) { pos = resume_pos; resume_pos = NULL; } else pos = cur->text;
                break;
            }
            skip_spaces();
            if (pos && *pos == ':') { pos++; continue; }
            cur = cur->next;
            pos = cur ? cur->text : NULL;
            break;
        }
        if (end_program_flag || error_flag) break;
    }
}

/* ---------- Immediate/REPL helpers ---------- */
void do_list() {
    Line *p = program;
    while (p) {
        printf("%d %s\n", p->lineno, p->text);
        p = p->next;
    }
}

void do_immediate(const char *line) {
    char buf[MAX_LINE_LEN];
    strncpy(buf, line, MAX_LINE_LEN-1);
    buf[MAX_LINE_LEN-1] = '\0';
    const char *t = buf;
    while (isspace((unsigned char)*t)) t++;
    if (isdigit((unsigned char)*t)) {
        char *p = (char*)t;
        int lineno = strtol(p, &p, 10);
        while (isspace((unsigned char)*p)) p++;
        insert_line(lineno, p);
        return;
    }
    char up[MAX_LINE_LEN]; int i=0;
    while (t[i] && i < MAX_LINE_LEN-1) { up[i] = toupper((unsigned char)t[i]); i++; }
    up[i]=0;
    if (strncmp(up, "RUN", 3) == 0) { do_run(); return; }
    if (strncmp(up, "LIST", 4) == 0) { do_list(); return; }
    if (strncmp(up, "NEW", 3) == 0) { free_program(); free_symtab(); for_sp = 0; gosub_sp = 0; return; }
    if (strncmp(up, "QUIT", 4) == 0 || strncmp(up,"EXIT",4)==0 || strncmp(up,"BYE",3)==0) exit(0);

    Line tmp; tmp.lineno = 0; tmp.text = strdup(t);
    Line *tp = &tmp;
    const char *pos = tmp.text;
    while (1) {
        cp = pos;
        jump_to_lineno = 0;
        int ret = exec_statement(&tp, &pos);
        if (end_program_flag || error_flag) break;
        skip_spaces();
        if (!pos || *pos == '\0') break;
        if (*pos == ':') { pos++; continue; }
        break;
    }
    free(tmp.text);
}

/* ---------- Main: REPL ---------- */
int main(int argc, char **argv) {
    printf("Tiny BASIC+ interpreter (fixed)\n");
    printf("Multi-char vars, $ strings, DIM arrays, GOSUB/RETURN, FOR/NEXT\n");
    printf("Functions: LEN, LEFT$, RIGHT$, MID$, CHR$, ASC\n");
    printf("Commands: RUN, LIST, NEW, QUIT\n");
    printf("----\n");

    char linebuf[MAX_LINE_LEN];
    for_sp = 0; gosub_sp = 0;

    while (1) {
        printf("] ");
        if (!fgets(linebuf, sizeof(linebuf), stdin)) break;
        size_t L = strlen(linebuf); if (L && linebuf[L-1]=='\n') linebuf[L-1]=0;
        char *p = linebuf;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') continue;
        do_immediate(p);
    }
    return 0;
}
