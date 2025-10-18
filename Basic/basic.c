/* Integer BASIC–style interpreter
   Features:
   - Interactive line entry (numbered lines stored, immediate commands executed)
   - LIST, RUN, NEW
   - Optional LET (A = 5)
   - ? shorthand stored as PRINT
   - FOR / NEXT
   - GOSUB / RETURN
   - ON <expr> GOTO/GOSUB <list>
   - PRINT supports arithmetic expressions, string literals, and simple string functions (CHR$, LEN, LEFT$, RIGHT$, MID$, ASC)
   - Expression evaluation tolerates whitespace
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>

#define MAX_LINES 1000
#define MAX_LINE_LEN 256
#define MAX_VARS 256
#define MAX_SUBSTACK 64
#define MAX_FORSTACK 64
#define MAX_ARRAY 100

typedef enum { TYPE_INT, TYPE_STRING, TYPE_INT_ARRAY } VarType;

typedef struct {
    char name[32];
    VarType type;
    int int_val;
    char *str_val;
    int int_array[MAX_ARRAY];
    int array_size;
} Variable;

typedef struct {
    int line_no;
    char text[MAX_LINE_LEN];
} Line;

typedef struct {
    int line_index;
} GOSUBEntry;

typedef struct {
    char varname[32];
    int end_val;
    int step;
    int line_index;
} FOREntry;

// ---------------- Global State ----------------

Line program[MAX_LINES];
int program_lines = 0;

Variable vars[MAX_VARS];
int var_count = 0;

jmp_buf jump_buffer;
int current_line_index = 0;
GOSUBEntry gosub_stack[MAX_SUBSTACK];
int gosub_stack_ptr = 0;

FOREntry for_stack[MAX_FORSTACK];
int for_stack_ptr = 0;

// ---------------- Utility helpers ----------------

static void str_toupper_inplace(char *s) {
    for(; *s; ++s) *s = (char)toupper((unsigned char)*s);
}

static char *strcasestr_local(const char *haystack, const char *needle) {
    if(!*needle) return (char*)haystack;
    for(; *haystack; ++haystack) {
        const char *h = haystack;
        const char *n = needle;
        while(*h && *n && (toupper((unsigned char)*h) == toupper((unsigned char)*n))) { h++; n++; }
        if(!*n) return (char*)haystack;
    }
    return NULL;
}

static void trim(char *s) {
    // trim leading and trailing spaces
    char *p = s;
    while(isspace((unsigned char)*p)) p++;
    if(p != s) memmove(s, p, strlen(p) + 1);
    int L = strlen(s);
    while(L > 0 && isspace((unsigned char)s[L-1])) s[--L] = '\0';
}

static void remove_spaces(char *dest, const char *src) {
    while (*src) {
        if (!isspace((unsigned char)*src)) *dest++ = *src;
        src++;
    }
    *dest = '\0';
}

// ---------------- Variable Management ----------------

Variable* find_var(const char *name) {
    for(int i=0;i<var_count;i++)
        if(strcasecmp(vars[i].name,name)==0) return &vars[i];
    return NULL;
}

Variable* make_var(const char *name, VarType type) {
    Variable *v = find_var(name);
    if(!v) {
        if(var_count>=MAX_VARS) { fprintf(stderr,"Too many variables\n"); longjmp(jump_buffer,1);}
        v = &vars[var_count++];
        strncpy(v->name, name, sizeof(v->name)-1);
        v->name[sizeof(v->name)-1] = '\0';
        v->type=type;
        v->int_val=0;
        v->str_val=NULL;
        if(type==TYPE_INT_ARRAY) memset(v->int_array,0,sizeof(v->int_array));
        v->array_size=0;
    }
    return v;
}

// ---------------- Expression Evaluation ----------------

int parse_expr(const char **s);

int parse_factor(const char **s) {
    while(isspace((unsigned char)**s)) (*s)++;
    if(**s=='(') {
        (*s)++;
        int val = parse_expr(s);
        if(**s==')') (*s)++;
        return val;
    }
    // variable or number
    char buf[64]; int i=0;
    while(isalnum((unsigned char)**s) || **s=='_') {
        if(i < (int)sizeof(buf)-1) buf[i++] = *(*s);
        (*s)++;
    }
    buf[i]='\0';
    if(i>0) {
        Variable *v = find_var(buf);
        if(v && v->type==TYPE_INT) return v->int_val;
        return atoi(buf);
    }
    int val=0;
    int sign = 1;
    if(**s == '+') { (*s)++; }
    else if(**s == '-') { sign = -1; (*s)++; }
    while(isdigit((unsigned char)**s)) { val = val*10 + (**s - '0'); (*s)++; }
    return sign * val;
}

int parse_term(const char **s) {
    int val = parse_factor(s);
    for(;;) {
        while(isspace((unsigned char)**s)) (*s)++;
        if(**s=='*' || **s=='/') {
            char op = **s; (*s)++;
            int rhs = parse_factor(s);
            if(op=='*') val *= rhs;
            else val /= rhs;
        } else break;
    }
    return val;
}

int parse_expr(const char **s) {
    int val = parse_term(s);
    for(;;) {
        while(isspace((unsigned char)**s)) (*s)++;
        if(**s=='+' || **s=='-') {
            char op = **s; (*s)++;
            int rhs = parse_term(s);
            if(op=='+') val += rhs;
            else val -= rhs;
        } else break;
    }
    return val;
}

int eval_arith_expr(const char *expr) {
    char tmp[512];
    remove_spaces(tmp, expr);
    const char *p = tmp;
    return parse_expr(&p);
}

// ---------------- String Functions ----------------

char* eval_str_func(const char *func, const char *arg1, const char *arg2, const char *arg3) {
    static char buf[512];
    buf[0] = '\0';
    if(func==NULL) return buf;
    if(strcmp(func,"LEN")==0) {
        Variable *v = find_var(arg1);
        if(v && v->type==TYPE_STRING) { snprintf(buf,sizeof(buf), "%lu", (unsigned long)strlen(v->str_val)); return buf; }
    } else if(strcmp(func,"LEFT$")==0) {
        Variable *v = find_var(arg1);
        int n = eval_arith_expr(arg2?arg2:"0");
        if(v && v->type==TYPE_STRING) { strncpy(buf, v->str_val, n); buf[n]='\0'; return buf; }
    } else if(strcmp(func,"RIGHT$")==0) {
        Variable *v = find_var(arg1);
        int n = eval_arith_expr(arg2?arg2:"0");
        if(v && v->type==TYPE_STRING) { int len = (int)strlen(v->str_val); if(n>len) n=len; strcpy(buf, v->str_val + len - n); return buf; }
    } else if(strcmp(func,"MID$")==0) {
        Variable *v = find_var(arg1);
        int start = eval_arith_expr(arg2?arg2:"1") - 1;
        int n = eval_arith_expr(arg3?arg3:"0");
        if(v && v->type==TYPE_STRING) { if(start<0) start=0; strncpy(buf, v->str_val + start, n); buf[n]='\0'; return buf; }
    } else if(strcmp(func,"CHR$")==0) {
        int c = eval_arith_expr(arg1?arg1:"0");
        buf[0] = (char)c; buf[1] = '\0'; return buf;
    } else if(strcmp(func,"ASC")==0) {
        Variable *v = find_var(arg1);
        if(v && v->type==TYPE_STRING && v->str_val && v->str_val[0]) { snprintf(buf,sizeof(buf), "%d", (int)v->str_val[0]); return buf; }
    }
    return buf;
}

// ---------------- Execution ----------------

void execute_line(int index) {
    current_line_index = index;
    char line_orig[MAX_LINE_LEN];
    strncpy(line_orig, program[index].text, MAX_LINE_LEN-1);
    line_orig[MAX_LINE_LEN-1] = '\0';

    char linebuf[MAX_LINE_LEN];
    strcpy(linebuf, line_orig);

    char *tokens[64];
    int n=0;
    char *p = strtok(linebuf," \t\n");
    while(p && n<64) { tokens[n++]=p; p=strtok(NULL," \t\n"); }
    if(n==0) return;

    // uppercase command token for identification
    char cmd_upper[64];
    strncpy(cmd_upper, tokens[0], sizeof(cmd_upper)-1);
    cmd_upper[sizeof(cmd_upper)-1] = '\0';
    str_toupper_inplace(cmd_upper);

    // PRINT
    if(strcmp(cmd_upper,"PRINT")==0) {
        char exprbuf[512]="";
        for(int i=1;i<n;i++) { strncat(exprbuf, tokens[i], sizeof(exprbuf)-strlen(exprbuf)-1); if(i<n-1) strncat(exprbuf, " ", sizeof(exprbuf)-strlen(exprbuf)-1); }
        trim(exprbuf);
        if(exprbuf[0]=='"' && exprbuf[strlen(exprbuf)-1]=='"') {
            exprbuf[strlen(exprbuf)-1] = '\0';
            printf("%s\n", exprbuf+1);
        } else if(strchr(exprbuf,'(')) {
            // attempt to parse function-like expression FUNC(arg,...)
            char func[64], a1[128], a2[128], a3[128];
            func[0]=a1[0]=a2[0]=a3[0]='\0';
            sscanf(exprbuf, "%63[^'(](%127[^,],%127[^,],%127[^)])", func, a1, a2, a3);
            trim(func); trim(a1); trim(a2); trim(a3);
            // uppercase function name for matching
            char funcup[64]; strncpy(funcup, func, sizeof(funcup)-1); funcup[sizeof(funcup)-1]='\0'; str_toupper_inplace(funcup);
            char *res = eval_str_func(funcup, a1[0]?a1:NULL, a2[0]?a2:NULL, a3[0]?a3:NULL);
            if(res && res[0]) printf("%s\n", res);
            else {
                // fallback to arithmetic evaluation if function not found
                printf("%d\n", eval_arith_expr(exprbuf));
            }
        } else {
            printf("%d\n", eval_arith_expr(exprbuf));
        }
        return;
    }

    // LET
    if(strcmp(cmd_upper,"LET")==0) {
        char *eq = strchr(line_orig,'=');
        if(!eq) { fprintf(stderr,"Syntax error at line %d\n", program[index].line_no); longjmp(jump_buffer,1); }
        *eq = '\0';
        char varname[64]; varname[0]='\0';
        sscanf(line_orig+3, "%63s", varname); // after "LET"
        trim(varname);
        if(varname[0]=='\0') { fprintf(stderr,"Syntax error at line %d\n", program[index].line_no); longjmp(jump_buffer,1); }
        Variable *v = make_var(varname, TYPE_INT);
        char *valstr = eq+1;
        trim(valstr);
        v->int_val = eval_arith_expr(valstr);
        return;
    }

    // GOTO
    if(strcmp(cmd_upper,"GOTO")==0) {
        // evaluate expression after GOTO
        char argbuf[256] = "";
        for(int i=1;i<n;i++) { strncat(argbuf, tokens[i], sizeof(argbuf)-strlen(argbuf)-1); if(i<n-1) strncat(argbuf," ", sizeof(argbuf)-strlen(argbuf)-1); }
        trim(argbuf);
        int target = eval_arith_expr(argbuf);
        int found=-1;
        for(int i=0;i<program_lines;i++) if(program[i].line_no==target) { found=i; break; }
        if(found>=0) { current_line_index = found - 1; return; }
        fprintf(stderr,"GOTO target %d not found\n", target);
        longjmp(jump_buffer,1);
    }

    // GOSUB
    if(strcmp(cmd_upper,"GOSUB")==0) {
        char argbuf[256] = "";
        for(int i=1;i<n;i++) { strncat(argbuf, tokens[i], sizeof(argbuf)-strlen(argbuf)-1); if(i<n-1) strncat(argbuf," ", sizeof(argbuf)-strlen(argbuf)-1); }
        trim(argbuf);
        int target = eval_arith_expr(argbuf);
        int found=-1;
        for(int i=0;i<program_lines;i++) if(program[i].line_no==target) { found=i; break; }
        if(found>=0) {
            if(gosub_stack_ptr>=MAX_SUBSTACK) { fprintf(stderr,"GOSUB stack overflow\n"); longjmp(jump_buffer,1); }
            gosub_stack[gosub_stack_ptr++].line_index = current_line_index + 1;
            current_line_index = found - 1;
            return;
        }
        fprintf(stderr,"GOSUB target %d not found\n", target);
        longjmp(jump_buffer,1);
    }

    // RETURN
    if(strcmp(cmd_upper,"RETURN")==0) {
        if(gosub_stack_ptr<=0) { fprintf(stderr,"RETURN without GOSUB\n"); longjmp(jump_buffer,1); }
        current_line_index = gosub_stack[--gosub_stack_ptr].line_index - 1;
        return;
    }

    // END
    if(strcmp(cmd_upper,"END")==0) {
        longjmp(jump_buffer,2);
    }

    // NEW
    if(strcmp(cmd_upper,"NEW")==0) {
        program_lines = 0;
        var_count = 0;
        gosub_stack_ptr = 0;
        for_stack_ptr = 0;
        return;
    }

    // ON <expr> GOTO/GOSUB <list>
    if(strcmp(cmd_upper,"ON")==0) {
        // Work with original stored text to preserve spacing and commas
        char up[MAX_LINE_LEN];
        strncpy(up, program[index].text, sizeof(up)-1);
        up[sizeof(up)-1] = '\0';
        // make uppercase copy to locate GOTO/GOSUB
        char up_upper[MAX_LINE_LEN];
        strncpy(up_upper, up, sizeof(up_upper)-1);
        up_upper[sizeof(up_upper)-1] = '\0';
        str_toupper_inplace(up_upper);

        char *posGoto = strcasestr_local(up_upper, " GOTO ");
        char *posGosub = strcasestr_local(up_upper, " GOSUB ");

        char *posCmd = NULL;
        int isGosub = 0;
        if(posGoto && posGosub) {
            if(posGoto < posGosub) { posCmd = posGoto; isGosub = 0; }
            else { posCmd = posGosub; isGosub = 1; }
        } else if(posGoto) { posCmd = posGoto; isGosub = 0; }
        else if(posGosub) { posCmd = posGosub; isGosub = 1; }
        else {
            fprintf(stderr,"Syntax error in ON ... (missing GOTO/GOSUB) at line %d\n", program[index].line_no);
            longjmp(jump_buffer,1);
        }

        // expr is between "ON" and posCmd
        char expr_part[MAX_LINE_LEN];
        memset(expr_part,0,sizeof(expr_part));
        {
            const char *start = up;
            // skip "ON"
            start += 2;
            // skip spaces
            while(*start && isspace((unsigned char)*start)) start++;
            const char *end = up + (posCmd - up_upper); // get position in original string
            int len = (int)(end - start);
            if(len < 0) len = 0;
            if(len >= (int)sizeof(expr_part)) len = (int)sizeof(expr_part)-1;
            strncpy(expr_part, start, len);
            expr_part[len] = '\0';
            trim(expr_part);
        }

        // list part is after the command keyword
        char list_part[MAX_LINE_LEN];
        memset(list_part,0,sizeof(list_part));
        {
            const char *after = up + (posCmd - up_upper);
            // skip the keyword (either " GOTO " or " GOSUB ")
            if(!isGosub) after += strlen(" GOTO ");
            else after += strlen(" GOSUB ");
            // copy rest
            strncpy(list_part, after, sizeof(list_part)-1);
            list_part[sizeof(list_part)-1] = '\0';
            trim(list_part);
        }

        // evaluate selector expr
        int sel = eval_arith_expr(expr_part);
        if(sel < 1) return; // out of range -> do nothing

        // split list_part by commas and pick sel-th
        char list_copy[MAX_LINE_LEN];
        strncpy(list_copy, list_part, sizeof(list_copy)-1);
        list_copy[sizeof(list_copy)-1] = '\0';
        int idx = 0;
        char *tok = strtok(list_copy, ",");
        char chosen[128]; chosen[0] = '\0';
        while(tok) {
            idx++;
            if(idx == sel) {
                strncpy(chosen, tok, sizeof(chosen)-1);
                chosen[sizeof(chosen)-1] = '\0';
                break;
            }
            tok = strtok(NULL, ",");
        }
        if(chosen[0] == '\0') {
            // selection out of range -> do nothing
            return;
        }
        trim(chosen);
        // chosen may be an expression; evaluate it
        int target = eval_arith_expr(chosen);
        // find line index
        int found = -1;
        for(int i=0;i<program_lines;i++) if(program[i].line_no == target) { found = i; break; }
        if(found < 0) {
            fprintf(stderr,"ON ... target %d not found\n", target);
            longjmp(jump_buffer,1);
        }
        if(isGosub) {
            if(gosub_stack_ptr >= MAX_SUBSTACK) { fprintf(stderr,"GOSUB stack overflow\n"); longjmp(jump_buffer,1); }
            gosub_stack[gosub_stack_ptr++].line_index = current_line_index + 1;
            current_line_index = found - 1;
        } else {
            current_line_index = found - 1;
        }
        return;
    }

    // FOR
    if(strcmp(cmd_upper,"FOR")==0) {
        if(n<6) { fprintf(stderr,"Syntax error in FOR at line %d\n", program[index].line_no); longjmp(jump_buffer,1); }
        if(strcmp(tokens[2],"=")!=0) { fprintf(stderr,"Syntax error in FOR at line %d\n", program[index].line_no); longjmp(jump_buffer,1); }
        if(strcasecmp(tokens[4],"TO")!=0) { fprintf(stderr,"Syntax error in FOR at line %d\n", program[index].line_no); longjmp(jump_buffer,1); }
        char *varname = tokens[1];
        int start_val = eval_arith_expr(tokens[3]);
        int end_val = eval_arith_expr(tokens[5]);
        int step = 1;
        if(n >= 8 && strcasecmp(tokens[6],"STEP")==0) step = eval_arith_expr(tokens[7]);
        Variable *v = make_var(varname, TYPE_INT);
        v->int_val = start_val;
        if(for_stack_ptr >= MAX_FORSTACK) { fprintf(stderr,"FOR stack overflow\n"); longjmp(jump_buffer,1); }
        FOREntry *fe = &for_stack[for_stack_ptr++];
        strncpy(fe->varname, varname, sizeof(fe->varname)-1);
        fe->varname[sizeof(fe->varname)-1] = '\0';
        fe->line_index = current_line_index;
        fe->end_val = end_val;
        fe->step = step;
        return;
    }

    // NEXT
    if(strcmp(cmd_upper,"NEXT")==0) {
        if(for_stack_ptr <= 0) { fprintf(stderr,"NEXT without FOR\n"); longjmp(jump_buffer,1); }
        FOREntry *fe = &for_stack[for_stack_ptr-1];
        Variable *v = find_var(fe->varname);
        if(!v) { fprintf(stderr,"FOR variable not found\n"); longjmp(jump_buffer,1); }
        v->int_val += fe->step;
        if(v->int_val <= fe->end_val) {
            current_line_index = fe->line_index;
            return;
        } else {
            for_stack_ptr--;
            return;
        }
    }

    // Optional assignment without LET: var = expr
    if(n >= 3 && strcmp(tokens[1],"=") == 0) {
        char *varname = tokens[0];
        Variable *v = make_var(varname, TYPE_INT);
        char valbuf[512] = "";
        for(int i=2;i<n;i++) { strncat(valbuf, tokens[i], sizeof(valbuf)-strlen(valbuf)-1); if(i<n-1) strncat(valbuf," ", sizeof(valbuf)-strlen(valbuf)-1); }
        trim(valbuf);
        v->int_val = eval_arith_expr(valbuf);
        return;
    }

    fprintf(stderr,"Unknown command at line %d: %s\n", program[index].line_no, program[index].text);
    longjmp(jump_buffer,1);
}

// ---------------- LIST & RUN ----------------

void list_program() {
    // Print stored lines in insertion order (user's storage)
    for(int i=0;i<program_lines;i++) {
        if(program[i].line_no > 0) {
            printf("%d %s\n", program[i].line_no, program[i].text);
        }
    }
}

void run_program() {
    int status = setjmp(jump_buffer);
    if(status == 0) {
        for(current_line_index = 0; current_line_index < program_lines; current_line_index++) {
            if(program[current_line_index].line_no > 0) {
                execute_line(current_line_index);
            }
        }
    } else {
        // error or END: return to interactive prompt
    }
}

// ---------------- Interactive Loop ----------------

void interactive_mode() {
    char line[512];
    while(1) {
        printf("] "); fflush(stdout);
        if(!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';
        if(strlen(line) == 0) continue;

        // Make uppercase copy for simple full-command checks
        char upline[512];
        strncpy(upline, line, sizeof(upline)-1);
        upline[sizeof(upline)-1] = '\0';
        str_toupper_inplace(upline);
        trim(upline);

        if(strcmp(upline, "LIST") == 0) { list_program(); continue; }
        if(strcmp(upline, "RUN") == 0) { run_program(); continue; }
        if(strcmp(upline, "NEW") == 0) { program_lines = 0; var_count = 0; gosub_stack_ptr = 0; for_stack_ptr = 0; continue; }

        // If the line begins with a line number, treat as program editing
        int lineno = 0;
        char *rest = NULL;
        {
            // parse optional leading number
            char *p = line;
            while(*p && isspace((unsigned char)*p)) p++;
            if(isdigit((unsigned char)*p)) {
                char numbuf[32];
                int ni=0;
                while(*p && isdigit((unsigned char)*p) && ni < (int)sizeof(numbuf)-1) { numbuf[ni++] = *p++; }
                numbuf[ni] = '\0';
                lineno = atoi(numbuf);
                // skip spaces
                while(*p && isspace((unsigned char)*p)) p++;
                if(*p) rest = p; else rest = NULL;
            }
        }

        if(lineno > 0) {
            if(rest == NULL) {
                // delete the line
                int found = -1;
                for(int i=0;i<program_lines;i++) if(program[i].line_no == lineno) { found = i; break; }
                if(found >= 0) {
                    for(int j = found; j < program_lines-1; j++) program[j] = program[j+1];
                    program_lines--;
                }
                continue;
            } else {
                // convert ? at start of rest to PRINT for storage
                char storebuf[MAX_LINE_LEN];
                strncpy(storebuf, rest, sizeof(storebuf)-1);
                storebuf[sizeof(storebuf)-1] = '\0';
                trim(storebuf);
                if(storebuf[0] == '?') {
                    // replace leading '?' with PRINT
                    char tmp[ MAX_LINE_LEN ];
                    strncpy(tmp, storebuf+1, sizeof(tmp)-2);
                    tmp[sizeof(tmp)-1] = '\0';
                    snprintf(storebuf, sizeof(storebuf), "PRINT%s", tmp);
                    trim(storebuf);
                }
                // store or replace
                int found = -1;
                for(int i=0;i<program_lines;i++) if(program[i].line_no == lineno) { found = i; break; }
                if(found >= 0) {
                    strncpy(program[found].text, storebuf, MAX_LINE_LEN-1);
                    program[found].text[MAX_LINE_LEN-1] = '\0';
                } else {
                    if(program_lines >= MAX_LINES) { fprintf(stderr,"Too many lines\n"); continue; }
                    program[program_lines].line_no = lineno;
                    strncpy(program[program_lines].text, storebuf, MAX_LINE_LEN-1);
                    program[program_lines].text[MAX_LINE_LEN-1] = '\0';
                    program_lines++;
                }
                continue;
            }
        } else {
            // Immediate execution
            char execbuf[MAX_LINE_LEN];
            strncpy(execbuf, line, sizeof(execbuf)-1);
            execbuf[sizeof(execbuf)-1] = '\0';
            trim(execbuf);
            if(execbuf[0] == '?') {
                // convert to PRINT
                char tmp[ MAX_LINE_LEN ];
                strncpy(tmp, execbuf+1, sizeof(tmp)-2);
                tmp[sizeof(tmp)-1] = '\0';
                snprintf(execbuf, sizeof(execbuf), "PRINT%s", tmp);
                trim(execbuf);
            }
            // store as temporary line with line_no = 0
            if(program_lines >= MAX_LINES) { fprintf(stderr,"Too many lines for immediate execution\n"); continue; }
            program[program_lines].line_no = 0;
            strncpy(program[program_lines].text, execbuf, MAX_LINE_LEN-1);
            program[program_lines].text[MAX_LINE_LEN-1] = '\0';
            int status = setjmp(jump_buffer);
            if(status == 0) {
                execute_line(program_lines);
            } else {
                // error or END; continue
            }
            // remove temporary
            program_lines--;
            continue;
        }
    }
}

// ---------------- Main ----------------

int main(void) {
    printf("Integer BASIC Interpreter (interactive)\n");
    printf("Features: LIST RUN NEW, optional LET, ? -> PRINT, FOR/NEXT, GOSUB/RETURN, ON...GOTO/GOSUB, expressions, CHR$/LEN/LEFT$/RIGHT$/MID$/ASC\n");
    interactive_mode();
    return 0;
}
