/*
  intbasic_extended.c -- Tiny Integer/Strings BASIC-like interpreter (readable version)

  Features:
    - multi-char variable names, string vars end with $
    - 1-D numeric arrays via DIM NAME(n) (1-based)
    - string variables and functions: LEN, LEFT$, RIGHT$, MID$, CHR$, ASC,
                                      STR$, VAL, INSTR, UCASE$, LCASE$
    - GOSUB/RETURN, FOR/NEXT, IF...THEN, GOTO, LET/assignment, PRINT, INPUT
    - "?" as a synonym for PRINT
    - expressions with + - * / and parentheses; + concatenates if either operand is string

  Build:
    gcc -O2 -o intbasic_extended intbasic_extended.c

  Run:
    ./intbasic_extended
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------------- Configuration ---------------- */
#define MAX_LINE_LEN 2048
#define MAX_TOKEN_LEN 256
#define MAX_FOR_STACK 512
#define MAX_GOSUB_DEPTH 512

/* ---------------- Program storage ---------------- */
typedef struct Line {
    int lineno;
    char *text;
    struct Line *next;
} Line;

Line *program = NULL;

void free_program(void) {
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
Line *first_line(void) { return program; }

/* ---------------- Symbol table and data types ---------------- */

typedef enum { T_NUM, T_STR, T_ARRAY } ValType;

/* Array structure for numeric arrays */
typedef struct Array {
    int size;   /* 1-based indexing, size elements */
    int *data;  /* allocated int[size] */
} Array;

/* Variable entry in symbol table */
typedef struct Var {
    char *name;      /* uppercase name, includes trailing $ for string vars */
    ValType type;
    int num;         /* numeric value if T_NUM */
    char *str;       /* malloced string if T_STR */
    Array *arr;      /* if T_ARRAY */
    struct Var *next;
} Var;

Var *symtab = NULL;

static Var *find_var(const char *name) {
    for (Var *p = symtab; p; p = p->next) {
        if (strcmp(p->name, name) == 0) return p;
    }
    return NULL;
}

static Var *create_var(const char *name, ValType type) {
    Var *v = find_var(name);
    if (v) {
        /* convert if necessary (free resources of old type) */
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

static void free_symtab(void) {
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

/* Normalize name: copy uppercase up to trailing $ (if any) */
static void normalize_name(const char *src, char *dst) {
    int i = 0;
    while (*src && (isalnum((unsigned char)*src) || *src == '$') && i < MAX_TOKEN_LEN - 1) {
        char c = *src++;
        dst[i++] = toupper((unsigned char)c);
        if (dst[i-1] == '$') break; /* stop at trailing $ */
    }
    dst[i] = '\0';
}

/* ---------------- Parser state and helpers ---------------- */

static const char *cp; /* current parse pointer into statement text */

/* skip spaces at cp */
static void skip_spaces(void) {
    while (*cp && isspace((unsigned char)*cp)) cp++;
}

/* identifier character tests */
static int isidentstart(char c) { return isalpha((unsigned char)c); }
static int isidentchar(char c) { return isalnum((unsigned char)c) || c == '$'; }

/* Fixed parse_ident: copies chars into buf and advances cp exactly once per char.
   Accepts names like A, A1, COUNT, NAME$ (trailing $ marks strings).
   Returns 1 if identifier parsed, 0 otherwise. */
static int parse_ident(char *buf) {
    skip_spaces();
    const char *p = cp;
    if (!isidentstart(*p)) return 0;
    int i = 0;
    while (*p && isidentchar(*p) && i < MAX_TOKEN_LEN - 1) {
        char ch = *p;
        buf[i++] = toupper((unsigned char)ch);
        p++;
        if (buf[i-1] == '$') break; /* end on trailing $ */
    }
    buf[i] = '\0';
    if (i == 0) return 0;
    cp = p; /* advance global pointer */
    return 1;
}

/* parse integer number (decimal), supports optional + or - */
static int parse_number(int *out) {
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

/* parse string literal "..." into newly allocated string (out) */
static int parse_string_literal(char **out) {
    skip_spaces();
    if (*cp != '"') return 0;
    cp++; /* skip starting quote */
    char tmp[MAX_LINE_LEN];
    int i = 0;
    while (*cp && *cp != '"' && i < MAX_LINE_LEN - 1) {
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
    if (*cp == '"') cp++; /* skip closing quote */
    *out = strdup(tmp);
    return 1;
}

/* ---------------- Value type for expressions ---------------- */
typedef struct Value {
    ValType type;   /* T_NUM or T_STR (arrays are handled at parse time) */
    int num;
    char *str;      /* malloced for strings */
} Value;

static Value make_num(int n) { Value v; v.type = T_NUM; v.num = n; v.str = NULL; return v; }
static Value make_str_take(char *s) { Value v; v.type = T_STR; v.num = 0; v.str = s ? s : strdup(""); return v; }
static Value make_str_dup(const char *s) { Value v; v.type = T_STR; v.num = 0; v.str = strdup(s ? s : ""); return v; }

static void free_value(Value *v) {
    if (v->type == T_STR && v->str) { free(v->str); v->str = NULL; }
    v->type = T_NUM;
    v->num = 0;
}

/* convert value to newly allocated string (caller frees) */
static char *value_to_string(const Value *v) {
    if (v->type == T_STR) return strdup(v->str ? v->str : "");
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%d", v->num);
    return strdup(tmp);
}

/* ---------------- Arrays helpers ---------------- */

static int create_array(const char *name, int size) {
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

static int set_array_element(const char *name, int index, int value) {
    Var *v = find_var(name);
    if (!v) return 0;
    if (v->type != T_ARRAY) return 0;
    if (index < 1 || index > v->arr->size) return 0;
    v->arr->data[index-1] = value;
    return 1;
}

static int get_array_element(const char *name, int index, int *out) {
    Var *v = find_var(name);
    if (!v) return 0;
    if (v->type != T_ARRAY) return 0;
    if (index < 1 || index > v->arr->size) return 0;
    *out = v->arr->data[index-1];
    return 1;
}

/* ---------------- Variable get/set ---------------- */

static Value get_var_value(const char *name) {
    Var *v = find_var(name);
    if (!v) {
        /* if name ends with $, create string else numeric default */
        if (name[strlen(name)-1] == '$') v = create_var(name, T_STR);
        else v = create_var(name, T_NUM);
    }
    if (v->type == T_NUM) return make_num(v->num);
    if (v->type == T_STR) return make_str_dup(v->str ? v->str : "");
    /* arrays shouldn't be returned here */
    return make_num(0);
}

static void set_var_num(const char *name, int val) {
    Var *v = find_var(name);
    if (!v) v = create_var(name, T_NUM);
    if (v->type == T_STR) {
        /* numeric assigned to string -> convert to string */
        if (v->str) free(v->str);
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%d", val);
        v->str = strdup(tmp);
    } else if (v->type == T_NUM) {
        v->num = val;
    } else {
        /* if array, ignore */
    }
}

static void set_var_str(const char *name, const char *s) {
    Var *v = find_var(name);
    if (!v) v = create_var(name, T_STR);
    if (v->type == T_ARRAY) return;
    if (v->type == T_NUM) {
        /* convert to string variable */
        v->type = T_STR;
        if (v->str) free(v->str);
        v->str = strdup(s ? s : "");
    } else {
        if (v->str) free(v->str);
        v->str = strdup(s ? s : "");
    }
}

/* ---------------- Expression parser ---------------- */

static Value parse_expression(void); /* forward */
static Value parse_term(void);
static Value parse_factor(void);

/* Helper: convert cstring to uppercase/lowercase in new malloced string */
static char *str_to_upper(const char *s) {
    char *r = strdup(s ? s : "");
    for (char *p = r; *p; ++p) *p = toupper((unsigned char)*p);
    return r;
}
static char *str_to_lower(const char *s) {
    char *r = strdup(s ? s : "");
    for (char *p = r; *p; ++p) *p = tolower((unsigned char)*p);
    return r;
}

/* Parse factor:
   - parentheses ( ... )
   - number literal
   - string literal "..."
   - identifier: variable, array reference NAME(expr), or function call NAME(expr,...)
*/
static Value parse_factor(void) {
    skip_spaces();

    /* ( expr ) */
    if (*cp == '(') {
        cp++;
        Value v = parse_expression();
        skip_spaces();
        if (*cp == ')') cp++;
        return v;
    }

    /* string literal */
    if (*cp == '"') {
        char *s = NULL;
        if (parse_string_literal(&s)) return make_str_take(s);
    }

    /* number literal */
    int n;
    if (parse_number(&n)) return make_num(n);

    /* identifier / function / array */
    char name[MAX_TOKEN_LEN];
    const char *saved_cp = cp;
    if (parse_ident(name)) {
        skip_spaces();

        /* If identifier followed by '(' then it's either a function call we support, or an array reference */
        if (*cp == '(') {
            /* We'll check common function names (case-insensitive) first. */
            char upname[MAX_TOKEN_LEN];
            strncpy(upname, name, MAX_TOKEN_LEN);
            for (int i = 0; upname[i]; ++i) upname[i] = toupper((unsigned char)upname[i]);

            /* Recognize functions that take parentheses (we'll implement many) */
            if (strcmp(upname, "LEN") == 0 || strcmp(upname, "LEN$") == 0) {
                cp++; /* skip '(' */
                Value a = parse_expression();
                skip_spaces(); if (*cp == ')') cp++;
                int len = 0;
                if (a.type == T_STR) len = (int)strlen(a.str ? a.str : "");
                else {
                    char *tmp = value_to_string(&a);
                    len = (int)strlen(tmp);
                    free(tmp);
                }
                free_value(&a);
                return make_num(len);
            } else if (strcmp(upname, "LEFT$") == 0 || strcmp(upname, "LEFT") == 0) {
                cp++;
                Value s = parse_expression();
                skip_spaces(); if (*cp == ',') cp++;
                int cnt = 0; parse_number(&cnt);
                skip_spaces(); if (*cp == ')') cp++;
                char *src = value_to_string(&s);
                int sl = (int)strlen(src);
                int take = cnt < 0 ? 0 : (cnt > sl ? sl : cnt);
                char *out = malloc(take + 1); memcpy(out, src, take); out[take] = '\0';
                free(src); free_value(&s);
                return make_str_take(out);
            } else if (strcmp(upname, "RIGHT$") == 0 || strcmp(upname, "RIGHT") == 0) {
                cp++;
                Value s = parse_expression();
                skip_spaces(); if (*cp == ',') cp++;
                int cnt = 0; parse_number(&cnt);
                skip_spaces(); if (*cp == ')') cp++;
                char *src = value_to_string(&s);
                int sl = (int)strlen(src);
                int take = cnt < 0 ? 0 : (cnt > sl ? sl : cnt);
                int start = sl - take; if (start < 0) start = 0;
                char *out = malloc(take + 1); memcpy(out, src + start, take); out[take] = '\0';
                free(src); free_value(&s);
                return make_str_take(out);
            } else if (strcmp(upname, "MID$") == 0 || strcmp(upname, "MID") == 0) {
                cp++;
                Value s = parse_expression();
                skip_spaces(); if (*cp == ',') cp++;
                int pos = 1; parse_number(&pos);
                skip_spaces();
                int count_given = 0;
                int cnt = 0;
                if (*cp == ',') {
                    cp++;
                    count_given = 1;
                    parse_number(&cnt);
                }
                skip_spaces(); if (*cp == ')') cp++;
                char *src = value_to_string(&s);
                int sl = (int)strlen(src);
                int start = pos - 1; if (start < 0) start = 0;
                if (start > sl) start = sl;
                int take = 0;
                if (count_given) take = cnt < 0 ? 0 : (cnt > sl - start ? sl - start : cnt);
                else take = sl - start;
                char *out = malloc(take + 1);
                memcpy(out, src + start, take); out[take] = '\0';
                free(src); free_value(&s);
                return make_str_take(out);
            } else if (strcmp(upname, "CHR$") == 0 || strcmp(upname, "CHR") == 0) {
                cp++;
                int v = 0; parse_number(&v);
                skip_spaces(); if (*cp == ')') cp++;
                char *o = malloc(2); o[0] = (char)(v & 0xFF); o[1] = '\0';
                return make_str_take(o);
            } else if (strcmp(upname, "ASC") == 0) {
                cp++;
                Value s = parse_expression();
                skip_spaces(); if (*cp == ')') cp++;
                char *tmp = value_to_string(&s);
                int r = tmp[0] ? (unsigned char)tmp[0] : 0;
                free(tmp); free_value(&s);
                return make_num(r);
            } else if (strcmp(upname, "STR$") == 0 || strcmp(upname, "STR") == 0) {
                /* STR$(number) -> string */
                cp++;
                Value a = parse_expression();
                skip_spaces(); if (*cp == ')') cp++;
                char *tmp = value_to_string(&a);
                free_value(&a);
                return make_str_take(tmp); /* tmp already malloc'd by value_to_string */
            } else if (strcmp(upname, "VAL") == 0) {
                /* VAL(string) -> integer (atoi) */
                cp++;
                Value a = parse_expression();
                skip_spaces(); if (*cp == ')') cp++;
                char *tmp = value_to_string(&a);
                int v = atoi(tmp);
                free(tmp); free_value(&a);
                return make_num(v);
            } else if (strcmp(upname, "INSTR") == 0) {
                /* INSTR(s$, t$) -> 1-based position or 0 */
                cp++;
                Value s = parse_expression();
                skip_spaces(); if (*cp == ',') cp++;
                Value t = parse_expression();
                skip_spaces(); if (*cp == ')') cp++;
                char *ss = value_to_string(&s);
                char *tt = value_to_string(&t);
                char *p = strstr(ss, tt);
                int pos = p ? (int)(p - ss) + 1 : 0;
                free(ss); free(tt); free_value(&s); free_value(&t);
                return make_num(pos);
            } else if (strcmp(upname, "UCASE$") == 0 || strcmp(upname, "UCASE") == 0) {
                cp++;
                Value s = parse_expression();
                skip_spaces(); if (*cp == ')') cp++;
                char *tmp = value_to_string(&s);
                char *out = str_to_upper(tmp);
                free(tmp); free_value(&s);
                return make_str_take(out);
            } else if (strcmp(upname, "LCASE$") == 0 || strcmp(upname, "LCASE") == 0) {
                cp++;
                Value s = parse_expression();
                skip_spaces(); if (*cp == ')') cp++;
                char *tmp = value_to_string(&s);
                char *out = str_to_lower(tmp);
                free(tmp); free_value(&s);
                return make_str_take(out);
            }
            /* Not one of the recognized functions -> could be array access. Fall through to array handling below. */
        }

        /* array access NAME(expr) */
        skip_spaces();
        if (*cp == '(') {
            cp++; /* skip '(' */
            Value idxv = parse_expression();
            int idx = (idxv.type == T_NUM) ? idxv.num : atoi(idxv.str ? idxv.str : "0");
            free_value(&idxv);
            skip_spaces(); if (*cp == ')') cp++;
            int outv = 0;
            if (!get_array_element(name, idx, &outv)) {
                printf("ARRAY ERROR: %s(%d)\n", name, idx);
                return make_num(0);
            }
            return make_num(outv);
        }

        /* plain variable */
        return get_var_value(name);
    }

    /* nothing matched -> return 0 */
    return make_num(0);
}

/* parse term: handles * and / */
static Value parse_term(void) {
    Value left = parse_factor();
    while (1) {
        skip_spaces();
        if (*cp == '*') {
            cp++;
            Value right = parse_factor();
            int a = (left.type == T_NUM) ? left.num : atoi(left.str ? left.str : "0");
            int b = (right.type == T_NUM) ? right.num : atoi(right.str ? right.str : "0");
            free_value(&left); free_value(&right);
            left = make_num(a * b);
        } else if (*cp == '/') {
            cp++;
            Value right = parse_factor();
            int a = (left.type == T_NUM) ? left.num : atoi(left.str ? left.str : "0");
            int b = (right.type == T_NUM) ? right.num : atoi(right.str ? right.str : "0");
            free_value(&left); free_value(&right);
            if (b == 0) { printf("DIVIDE BY ZERO\n"); left = make_num(0); }
            else left = make_num(a / b);
        } else break;
    }
    return left;
}

/* parse expression: handles + and - ; + concatenates if either side string */
static Value parse_expression(void) {
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
                char *out = malloc(la + lb + 1);
                memcpy(out, a, la); memcpy(out + la, b, lb); out[la+lb] = '\0';
                free(a); free(b);
                free_value(&left); free_value(&right);
                left = make_str_take(out);
            } else {
                int res = left.num + right.num;
                free_value(&left); free_value(&right);
                left = make_num(res);
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

/* ---------------- FOR and GOSUB stacks ---------------- */

typedef struct ForEntry {
    char varname[MAX_TOKEN_LEN];
    int end;
    int step;
    Line *for_line;
    const char *for_pos;
} ForEntry;

static ForEntry forstack[MAX_FOR_STACK];
static int for_sp = 0;

static int push_for_entry(const char *varname, int end, int step, Line *fl, const char *pos) {
    if (for_sp >= MAX_FOR_STACK) return 0;
    strncpy(forstack[for_sp].varname, varname, MAX_TOKEN_LEN-1);
    forstack[for_sp].varname[MAX_TOKEN_LEN-1] = '\0';
    forstack[for_sp].end = end;
    forstack[for_sp].step = step;
    forstack[for_sp].for_line = fl;
    forstack[for_sp].for_pos = pos;
    for_sp++;
    return 1;
}

static int pop_for_entry(const char *varname, ForEntry *out) {
    for (int i = for_sp - 1; i >= 0; --i) {
        if (strcmp(forstack[i].varname, varname) == 0) {
            *out = forstack[i];
            for_sp = i;
            return 1;
        }
    }
    return 0;
}

/* GOSUB stack */
typedef struct GosubEntry { Line *ret_line; const char *ret_pos; } GosubEntry;
static GosubEntry gosub_stack[MAX_GOSUB_DEPTH];
static int gosub_sp = 0;

static int push_gosub(Line *rl, const char *rp) {
    if (gosub_sp >= MAX_GOSUB_DEPTH) return 0;
    gosub_stack[gosub_sp].ret_line = rl;
    gosub_stack[gosub_sp].ret_pos = rp;
    gosub_sp++;
    return 1;
}
static int pop_gosub(Line **rl, const char **rp) {
    if (gosub_sp <= 0) return 0;
    gosub_sp--;
    *rl = gosub_stack[gosub_sp].ret_line;
    *rp = gosub_stack[gosub_sp].ret_pos;
    return 1;
}

/* ---------------- Execution engine ---------------- */

/* control flags */
static int jump_to_lineno = 0;
static int end_program_flag = 0;
static int error_flag = 0;
static const char *resume_pos = NULL; /* used when NEXT/RETURN set a resume position */

static void set_jump(int lineno) { jump_to_lineno = lineno; }

/* convenience macros for keyword matching (case-insensitive) */
#define MATCH_KW(k) (strncasecmp(cp, k, strlen(k))==0 && (isspace((unsigned char)cp[strlen(k)]) || cp[strlen(k)]==0 || cp[strlen(k)]==',' || cp[strlen(k)]==':' || cp[strlen(k)]=='('))
#define CONSUME_KW(k) do { cp += strlen(k); } while(0)

/* Forward */
static int exec_statement(Line **curlp, const char **curpos);

/* Try to parse an assignment: NAME = expr or NAME(expr) = expr (array); returns 1 if assignment handled else 0 */
static int parse_and_do_assignment(void) {
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
        skip_spaces(); if (*cp == ')') cp++;
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
        /* variable assignment NAME = expr */
        skip_spaces();
        if (*cp != '=') { cp = save; return 0; }
        cp++; /* skip = */
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

/* exec_statement: execute a single statement starting at cp; updates cp to after statement.
   curlp is current line pointer so we can push return points, etc.
*/
static int exec_statement(Line **curlp, const char **curpos) {
    cp = *curpos;
    skip_spaces();
    if (*cp == '\0') { *curpos = cp; return 0; }

    const char *startcp = cp;

    /* REM */
    if (MATCH_KW("REM")) { CONSUME_KW("REM"); cp = strchr(cp, '\0'); *curpos = cp; return 0; }

    /* LET optional */
    if (MATCH_KW("LET")) { CONSUME_KW("LET"); }

    /* allow '?' as synonym for PRINT */
    if (*cp == '?') {
        cp++;
        /* fall through to PRINT handling below by setting a flag or jumping into the print code */
        /* we'll set a temporary marker and reuse the PRINT handling: */
        /* treat as if MATCH_KW("PRINT") consumed */
        /* no change to cp needed beyond skipping '?' */
        /* handle below: go to print_label */
        goto handle_print;
    }

    /* Assignment attempt first (NAME or NAME(expr) = ... ) */
    if (parse_and_do_assignment()) { *curpos = cp; return 0; }

    /* PRINT */
handle_print:
    if (MATCH_KW("PRINT") || (*(startcp) == '?' && cp != startcp)) {
        /* If we jumped here via '?', MATCH_KW("PRINT") may not be true; treat both cases */
        if (MATCH_KW("PRINT")) CONSUME_KW("PRINT");
        skip_spaces();
        while (1) {
            if (*cp == '"') {
                char *s = NULL;
                parse_string_literal(&s);
                fputs(s, stdout);
                free(s);
            } else {
                Value v = parse_expression();
                if (v.type == T_STR) fputs(v.str ? v.str : "", stdout);
                else printf("%d", v.num);
                free_value(&v);
            }
            skip_spaces();
            if (*cp == ',') { putchar(' '); cp++; continue; }
            if (*cp == ';') { cp++; continue; }
            break;
        }
        putchar('\n');
        *curpos = cp;
        return 0;
    }

    /* INPUT */
    if (MATCH_KW("INPUT")) {
        CONSUME_KW("INPUT"); skip_spaces();
        char name[MAX_TOKEN_LEN];
        if (!parse_ident(name)) { printf("SYNTAX ERROR IN INPUT\n"); error_flag=1; return 1; }
        skip_spaces();
        printf("? "); fflush(stdout);
        char buf[1024];
        if (!fgets(buf, sizeof(buf), stdin)) { end_program_flag = 1; return 1; }
        size_t L = strlen(buf); if (L && buf[L-1] == '\n') buf[L-1] = '\0';
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
        Line *rl; const char *rp;
        if (!pop_gosub(&rl, &rp)) { printf("RETURN WITHOUT GOSUB\n"); error_flag=1; return 1; }
        set_jump(rl->lineno);
        resume_pos = rp;
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
        else { printf("SYNTAX ERROR IN IF\n"); error_flag = 1; free_value(&left); return 1; }
        Value right = parse_expression();
        int Lval = (left.type == T_NUM) ? left.num : atoi(left.str ? left.str : "0");
        int Rval = (right.type == T_NUM) ? right.num : atoi(right.str ? right.str : "0");
        free_value(&left); free_value(&right);
        int cond = 0;
        switch (rel) {
            case 1: cond = (Lval == Rval); break;
            case 2: cond = (Lval != Rval); break;
            case 3: cond = (Lval < Rval); break;
            case 4: cond = (Lval > Rval); break;
            case 5: cond = (Lval <= Rval); break;
            case 6: cond = (Lval >= Rval); break;
        }
        skip_spaces();
        if (!MATCH_KW("THEN")) { printf("SYNTAX ERROR (expected THEN)\n"); error_flag=1; return 1; }
        CONSUME_KW("THEN"); skip_spaces();
        if (!cond) { *curpos = cp; return 0; }
        /* THEN may be lineno or inline statement */
        const char *sav = cp;
        int ln;
        if (parse_number(&ln)) { set_jump(ln); *curpos = cp; return 0; }
        else {
            if (exec_statement(curlp, &cp)) { /* possibly changed jump */ }
            *curpos = cp;
            return 0;
        }
    }

    /* FOR var = start TO end [STEP n] */
    if (MATCH_KW("FOR")) {
        CONSUME_KW("FOR"); skip_spaces();
        char vname[MAX_TOKEN_LEN];
        if (!parse_ident(vname)) { printf("SYNTAX ERROR IN FOR\n"); error_flag=1; return 1; }
        skip_spaces();
        if (*cp != '=') { printf("SYNTAX ERROR IN FOR (no '=')\n"); error_flag=1; return 1; }
        cp++; /* skip '=' */
        Value s = parse_expression();
        int start = (s.type == T_NUM) ? s.num : atoi(s.str ? s.str : "0");
        free_value(&s);
        if (!MATCH_KW("TO")) { printf("SYNTAX ERROR IN FOR (no TO)\n"); error_flag=1; return 1; }
        CONSUME_KW("TO");
        Value e = parse_expression();
        int end = (e.type == T_NUM) ? e.num : atoi(e.str ? e.str : "0");
        free_value(&e);
        int step = 1; skip_spaces();
        if (MATCH_KW("STEP")) { CONSUME_KW("STEP"); parse_number(&step); }
        set_var_num(vname, start);
        Line *fl = *curlp;
        const char *pos_after = cp;
        if (!push_for_entry(vname, end, step, fl, pos_after)) { printf("FOR STACK OVERFLOW\n"); error_flag=1; return 1; }
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
        val += fe.step;
        set_var_num(fe.varname, val);
        int cont = 0;
        if (fe.step > 0) { if (val <= fe.end) cont = 1; }
        else { if (val >= fe.end) cont = 1; }
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
        cp++; /* skip '(' */
        Value idx = parse_expression();
        int size = (idx.type == T_NUM) ? idx.num : atoi(idx.str ? idx.str : "0");
        free_value(&idx);
        skip_spaces(); if (*cp == ')') cp++;
        if (!create_array(name, size)) { printf("DIM ERROR\n"); error_flag=1; return 1; }
        *curpos = cp; return 0;
    }

    /* END */
    if (MATCH_KW("END")) { CONSUME_KW("END"); end_program_flag = 1; return 1; }

    /* If we didn't recognize anything, try assignment again (without LET) */
    cp = startcp;
    if (parse_and_do_assignment()) { *curpos = cp; return 0; }

    /* Unknown statement */
    printf("UNKNOWN STATEMENT: '%.60s'\n", cp);
    error_flag = 1;
    return 1;
}

/* ---------------- Run loop ---------------- */

static void do_run(void) {
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
                if (!t) { printf("LINE %d NOT FOUND\n", tgt); error_flag = 1; return; }
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

/* ---------------- Immediate mode / REPL ---------------- */

static void do_list(void) {
    for (Line *p = program; p; p = p->next) {
        printf("%d %s\n", p->lineno, p->text);
    }
}

static void do_immediate(const char *line) {
    char buf[MAX_LINE_LEN];
    strncpy(buf, line, MAX_LINE_LEN - 1);
    buf[MAX_LINE_LEN - 1] = '\0';
    const char *t = buf;
    while (isspace((unsigned char)*t)) t++;
    /* If line starts with a digit, it's a program line insertion */
    if (isdigit((unsigned char)*t)) {
        char *p = (char*)t;
        int lineno = (int)strtol(p, &p, 10);
        while (isspace((unsigned char)*p)) p++;
        insert_line(lineno, p);
        return;
    }
    /* Recognize commands RUN, LIST, NEW, QUIT */
    char up[MAX_LINE_LEN];
    int i = 0;
    while (t[i] && i < MAX_LINE_LEN - 1) { up[i] = toupper((unsigned char)t[i]); i++; }
    up[i] = '\0';
    if (strncmp(up, "RUN", 3) == 0) { do_run(); return; }
    if (strncmp(up, "LIST", 4) == 0) { do_list(); return; }
    if (strncmp(up, "NEW", 3) == 0) { free_program(); free_symtab(); for_sp = 0; gosub_sp = 0; return; }
    if (strncmp(up, "QUIT", 4) == 0 || strncmp(up, "EXIT", 4) == 0 || strncmp(up, "BYE", 3) == 0) exit(0);

    /* Otherwise execute the immediate statement(s) */
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

/* ---------------- Main REPL ---------------- */

int main(int argc, char **argv) {
    puts("Tiny BASIC+ Extended (readable)");
    puts("Features: multi-char vars, $ strings, DIM arrays, GOSUB/RETURN, FOR/NEXT");
    puts("Functions: LEN, LEFT$, RIGHT$, MID$, CHR$, ASC, STR$, VAL, INSTR, UCASE$, LCASE$");
    puts("Commands: RUN, LIST, NEW, QUIT");
    puts("Type lines with leading numbers to add program lines (e.g. 10 PRINT \"HI\")");
    puts("----");

    char linebuf[MAX_LINE_LEN];
    for_sp = 0; gosub_sp = 0;

    while (1) {
        printf("] ");
        if (!fgets(linebuf, sizeof(linebuf), stdin)) break;
        size_t L = strlen(linebuf);
        if (L && linebuf[L-1] == '\n') linebuf[L-1] = '\0';
        char *p = linebuf;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') continue;
        do_immediate(p);
    }

    return 0;
}
