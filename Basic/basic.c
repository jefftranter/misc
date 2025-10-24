/*
 * BASIC Interpreter v5_fix8
 * - Adds arrays via DIM (numeric and string arrays)
 * - Adds string variables and functions: LEN, LEFT$, RIGHT$, MID$, CHR$, ASC, STR$, VAL
 * - String concatenation using +
 * - Expressions are evaluated as typed Values (numeric or string)
 *
 * Note: Still an educational interpreter approximating Applesoft behavior.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINES 2000
#define MAX_LINE_LEN 512
#define MAX_VARS 2048
#define MAX_GOSUB 512
#define MAX_FOR_STACK 256

typedef struct {
    int number;
    char text[MAX_LINE_LEN];
} ProgramLine;

/* Value type for expression evaluation */
typedef struct {
    int is_string; /* 1 = string, 0 = numeric */
    double num;
    char *str;
} Value;

/* Variable representation */
typedef struct {
    char name[32];
    int is_string; /* name ends with $ */
    double value;
    char svalue[512];
    /* arrays */
    double *array;
    char **sarray;
    int array_size;
} Variable;

/* Globals */
static ProgramLine program[MAX_LINES];
static int num_lines = 0;

static Variable vars[MAX_VARS];
static int num_vars = 0;

static const char *input_line = NULL;
static int pos = 0;

static int current_line_index = 0;
static int gosub_stack[MAX_GOSUB]; static int gosub_sp = 0;

/* FOR stack */
static char for_varname[MAX_FOR_STACK][32];
static double for_end[MAX_FOR_STACK];
static double for_step[MAX_FOR_STACK];
static int for_line_idx[MAX_FOR_STACK];
static int for_sp = 0;

/* Helpers */
static void skip_ws(void) { if(!input_line) return; while (isspace((unsigned char)input_line[pos])) pos++; }
static void str_upper(char *s){ for(;*s;++s) *s = (char)toupper((unsigned char)*s); }

static Variable *find_var(const char *name, int create) {
    if (!name) return NULL;
    for (int i=0;i<num_vars;i++) if (strcasecmp(vars[i].name, name)==0) return &vars[i];
    if (!create) return NULL;
    if (num_vars >= MAX_VARS) { fprintf(stderr,"Too many variables\n"); exit(1); }
    Variable *v = &vars[num_vars++];
    memset(v,0,sizeof(Variable));
    strncpy(v->name, name, sizeof(v->name)-1);
    v->name[sizeof(v->name)-1]=0;
    v->is_string = (name[strlen(name)-1] == '$');
    v->value = 0.0;
    v->svalue[0]=0;
    v->array = NULL; v->sarray = NULL; v->array_size = 0;
    return v;
}

/* Forward declarations for expression parser returning Value */
static Value parse_logical_or(void);
static Value parse_logical_and(void);
static Value parse_not(void);
static Value parse_relational(void);
static Value parse_add(void);
static Value parse_mul(void);
static Value parse_unary(void);
static Value parse_primary(void);

/* Utilities for Value */
static Value make_num(double n){ Value v; v.is_string=0; v.num=n; v.str=NULL; return v; }
static Value make_str(const char *s){ Value v; v.is_string=1; v.num=0; v.str = strdup(s? s:""); return v; }
static void free_value(Value *v){ if(v && v->is_string && v->str){ free(v->str); v->str=NULL; } }

/* parse helpers */
static int match_keyword(const char *kw){ skip_ws(); int len=(int)strlen(kw); if(input_line && strncasecmp(input_line+pos,kw,len)==0){ pos+=len; skip_ws(); return 1;} return 0; }
static int match_char(char c){ skip_ws(); if(input_line && input_line[pos]==c){ pos++; skip_ws(); return 1;} return 0; }

/* Evaluate string functions */
static char *str_func_LEN(const char *arg){ if(!arg) return strdup("0"); char buf[32]; snprintf(buf,sizeof(buf),"%lu",(unsigned long)strlen(arg)); return strdup(buf); }
static char *str_func_LEFT(const char *s, int n){ if(!s) return strdup(""); if(n<0) n=0; int len=(int)strlen(s); if(n>len) n=len; char *r=malloc(n+1); memcpy(r,s,n); r[n]=0; return r; }
static char *str_func_RIGHT(const char *s, int n){ if(!s) return strdup(""); int len=(int)strlen(s); if(n<0) n=0; if(n>len) n=len; char *r=malloc(n+1); memcpy(r,s+len-n,n); r[n]=0; return r; }
static char *str_func_MID(const char *s, int start, int n){ if(!s) return strdup(""); int len=(int)strlen(s); if(start<1) start=1; int idx=start-1; if(idx>len) return strdup(""); if(n<0) n=0; if(idx+n>len) n=len-idx; char *r=malloc(n+1); memcpy(r,s+idx,n); r[n]=0; return r; }
static char *str_func_CHR(int n){ char buf[2]; buf[0]=(char)n; buf[1]=0; return strdup(buf); }
static char *str_func_ASC(const char *s){ if(!s || s[0]==0) return strdup("0"); char buf[32]; snprintf(buf,sizeof(buf),"%d",(unsigned char)s[0]); return strdup(buf); }
static char *str_func_STR(double n){ char buf[64]; snprintf(buf,sizeof(buf),"%g",n); return strdup(buf); }
static double str_func_VAL(const char *s){ if(!s) return 0.0; return atof(s); }

/* parse primary implementation (numbers, vars, arrays, strings) */
static Value parse_primary(void){
    skip_ws();
    if(!input_line) return make_num(0);
    if (input_line[pos]=='('){ pos++; Value v = parse_logical_or(); match_char(')'); return v; }
    if (input_line[pos]=='"'){
        pos++;
        char buf[1024]; int i=0;
        while(input_line[pos] && input_line[pos]!='"' && i<(int)sizeof(buf)-1) buf[i++]=input_line[pos++];
        buf[i]=0; if(input_line[pos]=='"') pos++; skip_ws();
        return make_str(buf);
    }
    if (isdigit((unsigned char)input_line[pos]) || (input_line[pos]=='-' && isdigit((unsigned char)input_line[pos+1]))){
        char nb[128]; int i=0;
        if(input_line[pos]=='-') nb[i++]=input_line[pos++];
        while(isdigit((unsigned char)input_line[pos])||input_line[pos]=='.'||input_line[pos]=='e'||input_line[pos]=='E'||input_line[pos]=='+'||input_line[pos]=='-'){
            if(i < (int)sizeof(nb)-1) nb[i++]=input_line[pos++]; else pos++;
        }
        nb[i]=0; skip_ws();
        return make_num(atof(nb));
    }
    if (isalpha((unsigned char)input_line[pos])){
        char name[32]; int i=0;
        while(isalnum((unsigned char)input_line[pos]) || input_line[pos]=='$' || input_line[pos]=='_'){
            if(i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]);
            pos++;
        }
        name[i]=0;
        skip_ws();
        /* Array reference? */
        if (input_line[pos]=='('){
            pos++; /* parse index expression */
            Value idxv = parse_logical_or();
            int idx = (int)idxv.num;
            match_char(')');
            Variable *v = find_var(name, 0);
            if(!v){ fprintf(stderr,"Undefined array %s\n", name); return make_num(0); }
            if(idx < 1 || idx > v->array_size){ fprintf(stderr,"BAD SUBSCRIPT\n"); return make_num(0); }
            if (v->is_string){
                char *s = v->sarray[idx-1] ? strdup(v->sarray[idx-1]) : strdup("");
                Value rv = make_str(s); free(s); return rv;
            } else {
                double val = v->array ? v->array[idx-1] : 0.0;
                return make_num(val);
            }
        } else {
            Variable *v = find_var(name, 0);
            if(!v) return make_num(0);
            if (v->is_string) return make_str(v->svalue);
            else return make_num(v->value);
        }
    }
    return make_num(0);
}

/* The parser functions are long; implement key ones supporting string/numeric ops */

/* forward */
static Value parse_unary(void){ skip_ws(); if(match_char('+')) return parse_unary(); if(match_char('-')){ Value v=parse_unary(); double nv = v.is_string? -str_func_VAL(v.str): -v.num; free_value(&v); return make_num(nv);} return parse_primary(); }
static Value parse_mul(void){ Value v = parse_unary(); for(;;){ skip_ws(); if(match_char('*')){ Value r=parse_unary(); double a=v.is_string?str_func_VAL(v.str):v.num; double b=r.is_string?str_func_VAL(r.str):r.num; free_value(&v); free_value(&r); v = make_num(a*b); } else if(match_char('/')){ Value r=parse_unary(); double a=v.is_string?str_func_VAL(v.str):v.num; double b=r.is_string?str_func_VAL(r.str):r.num; free_value(&v); free_value(&r); v = make_num(b!=0? a/b: 0); } else break; } return v; }
static Value parse_add(void){ Value v = parse_mul(); for(;;){ skip_ws(); if(match_char('+')){ Value r = parse_mul(); if(v.is_string || r.is_string){ char *as = v.is_string? v.str : (char*)({ char tmp[64]; snprintf(tmp,sizeof(tmp),"%g",v.num); strdup(tmp); }); char *bs = r.is_string? r.str : (char*)({ char tmp[64]; snprintf(tmp,sizeof(tmp),"%g",r.num); strdup(tmp); }); char *res = malloc(strlen(as)+strlen(bs)+1); strcpy(res, as); strcat(res, bs); free_value(&v); free_value(&r); if(!v.is_string) free(as); if(!r.is_string) free(bs); v = make_str(res); free(res); } else { v.num += r.num; free_value(&r); } } else if(match_char('-')){ Value r = parse_mul(); double a = v.is_string? str_func_VAL(v.str): v.num; double b = r.is_string? str_func_VAL(r.str): r.num; free_value(&v); free_value(&r); v = make_num(a - b); } else break; } return v; }
static Value parse_relational(void){ Value left = parse_add(); skip_ws(); if(!input_line) return left; if(input_line[pos]=='>' && input_line[pos+1]=='='){ pos+=2; Value right=parse_add(); if(left.is_string && right.is_string){ int ok = strcmp(left.str,right.str) >= 0; free_value(&left); free_value(&right); return make_num(ok?1.0:0.0);} double l=left.is_string?str_func_VAL(left.str):left.num; double r=right.is_string?str_func_VAL(right.str):right.num; free_value(&left); free_value(&right); return make_num(l>=r?1.0:0.0);} if(input_line[pos]=='<' && input_line[pos+1]=='='){ pos+=2; Value right=parse_add(); if(left.is_string && right.is_string){ int ok = strcmp(left.str,right.str) <= 0; free_value(&left); free_value(&right); return make_num(ok?1.0:0.0);} double l=left.is_string?str_func_VAL(left.str):left.num; double r=right.is_string?str_func_VAL(right.str):right.num; free_value(&left); free_value(&right); return make_num(l<=r?1.0:0.0);} if(input_line[pos]=='<' && input_line[pos+1]=='>'){ pos+=2; Value right=parse_add(); if(left.is_string && right.is_string){ int ok = strcmp(left.str,right.str) != 0; free_value(&left); free_value(&right); return make_num(ok?1.0:0.0);} double l=left.is_string?str_func_VAL(left.str):left.num; double r=right.is_string?str_func_VAL(right.str):right.num; free_value(&left); free_value(&right); return make_num(l!=r?1.0:0.0);} if(input_line[pos]=='>'){ pos++; Value right=parse_add(); double l=left.is_string?str_func_VAL(left.str):left.num; double r=right.is_string?str_func_VAL(right.str):right.num; free_value(&left); free_value(&right); return make_num(l>r?1.0:0.0);} if(input_line[pos]=='<'){ pos++; Value right=parse_add(); double l=left.is_string?str_func_VAL(left.str):left.num; double r=right.is_string?str_func_VAL(right.str):right.num; free_value(&left); free_value(&right); return make_num(l<r?1.0:0.0);} if(input_line[pos]=='='){ pos++; Value right=parse_add(); if(left.is_string && right.is_string){ int ok=strcmp(left.str,right.str)==0; free_value(&left); free_value(&right); return make_num(ok?1.0:0.0);} double l=left.is_string?str_func_VAL(left.str):left.num; double r=right.is_string?str_func_VAL(right.str):right.num; free_value(&left); free_value(&right); return make_num(l==r?1.0:0.0);} return left; }
static Value parse_not(void){ skip_ws(); if(match_keyword("NOT")){ Value v = parse_not(); double r = v.is_string? str_func_VAL(v.str): v.num; free_value(&v); return make_num(r==0.0?1.0:0.0);} return parse_relational(); }
static Value parse_logical_and(void){ Value v = parse_not(); for(;;){ if(match_keyword("AND")){ Value r = parse_not(); double a = v.is_string? str_func_VAL(v.str): v.num; double b = r.is_string? str_func_VAL(r.str): r.num; free_value(&v); free_value(&r); v = make_num((a!=0.0 && b!=0.0)?1.0:0.0); } else break; } return v; }
static Value parse_logical_or(void){ Value v = parse_logical_and(); for(;;){ if(match_keyword("OR")){ Value r = parse_logical_and(); double a = v.is_string? str_func_VAL(v.str): v.num; double b = r.is_string? str_func_VAL(r.str): r.num; free_value(&v); free_value(&r); v = make_num((a!=0.0 || b!=0.0)?1.0:0.0); } else break; } return v; }

/* Top-level evaluators */
static double eval_expression_numeric(const char *expr){ input_line = expr; pos = 0; Value v = parse_logical_or(); double r = v.is_string? str_func_VAL(v.str): v.num; free_value(&v); input_line = NULL; return r; }
static char *eval_expression_string(const char *expr){ input_line = expr; pos = 0; Value v = parse_logical_or(); char *s = NULL; if(v.is_string) s = strdup(v.str); else { char tmp[64]; snprintf(tmp,sizeof(tmp),"%g", v.num); s = strdup(tmp); } free_value(&v); input_line = NULL; return s; }

/* DIM command */
static void cmd_dim(const char *rest){ input_line = rest; pos = 0; skip_ws(); while(input_line[pos]){ skip_ws(); char name[32]; int i=0; if(!isalpha((unsigned char)input_line[pos])){ fprintf(stderr,"DIM: expected name\n"); input_line=NULL; return; } while(isalnum((unsigned char)input_line[pos])||input_line[pos]=='$'||input_line[pos]=='_'){ if(i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]); pos++; } name[i]=0; skip_ws(); if(!match_char('(')){ fprintf(stderr,"DIM: expected (\n"); input_line=NULL; return; } Value s = parse_logical_or(); int size = (int)s.num; free_value(&s); if(size<1) size=1; match_char(')'); Variable *v = find_var(name,1); if(v->array){ free(v->array); v->array=NULL; } if(v->sarray){ for(int k=0;k<v->array_size;k++) if(v->sarray[k]) free(v->sarray[k]); free(v->sarray); v->sarray=NULL; } v->array_size = size; if(v->is_string){ v->sarray = calloc(size, sizeof(char*)); for(int k=0;k<size;k++) v->sarray[k]=NULL; } else { v->array = calloc(size, sizeof(double)); for(int k=0;k<size;k++) v->array[k]=0.0; } skip_ws(); if(input_line[pos]==',') pos++; } input_line=NULL; }

/* PRINT, INPUT, GOTO/GOSUB/RETURN, FOR/NEXT, ON... implemented using earlier logic */

static void cmd_print(const char *rest){ input_line = rest; pos = 0; skip_ws(); int first=1; while(input_line && input_line[pos]){ if(!first){ if(input_line[pos]==','){ pos++; putchar(' '); skip_ws(); } } first=0; if(input_line[pos]=='"'){ pos++; while(input_line[pos] && input_line[pos]!='"') putchar(input_line[pos++]); if(input_line[pos]=='"') pos++; } else { Value v = parse_logical_or(); if(v.is_string) { printf("%s", v.str); free_value(&v); } else { printf("%g", v.num); free_value(&v); } } skip_ws(); if(input_line[pos]==0) break; } printf("\n"); input_line=NULL; }

static void cmd_input(const char *rest){ input_line = rest; pos = 0; while(1){ skip_ws(); if(!isalpha((unsigned char)input_line[pos])){ fprintf(stderr,"INPUT: expected var\n"); input_line=NULL; return; } char name[32]; int i=0; while(isalnum((unsigned char)input_line[pos])||input_line[pos]=='$'||input_line[pos]=='_'){ if(i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]); pos++; } name[i]=0; Variable *v = find_var(name,1); printf("? "); char buf[512]; if(!fgets(buf,sizeof(buf),stdin)) buf[0]=0; buf[strcspn(buf,"\n")]=0; if(v->is_string){ strncpy(v->svalue, buf, sizeof(v->svalue)-1); v->svalue[sizeof(v->svalue)-1]=0; } else v->value = atof(buf); skip_ws(); if(input_line[pos]!=',') break; pos++; } input_line=NULL; }

static void cmd_goto(const char *rest){ int target=(int)eval_expression_numeric(rest); int found=-1; for(int i=0;i<num_lines;i++) if(program[i].number==target){ found=i; break;} if(found>=0) current_line_index=found-1; else fprintf(stderr,"GOTO: line %d not found\n", target); }
static void cmd_gosub(const char *rest){ int target=(int)eval_expression_numeric(rest); int found=-1; for(int i=0;i<num_lines;i++) if(program[i].number==target){ found=i; break;} if(found>=0){ if(gosub_sp>=MAX_GOSUB){ fprintf(stderr,"GOSUB stack overflow\n"); return;} gosub_stack[gosub_sp++]=current_line_index+1; current_line_index=found-1; } else fprintf(stderr,"GOSUB: line %d not found\n", target); }
static void cmd_return(void){ if(gosub_sp<=0){ fprintf(stderr,"RETURN without GOSUB\n"); return;} int ret = gosub_stack[--gosub_sp]; current_line_index = ret - 1; }

static void cmd_for(const char *rest){ input_line=rest; pos=0; skip_ws(); char name[32]; int i=0; if(!isalpha((unsigned char)input_line[pos])){ fprintf(stderr,"FOR: bad var\n"); input_line=NULL; return; } while(isalnum((unsigned char)input_line[pos])||input_line[pos]=='$'||input_line[pos]=='_'){ if(i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]); pos++; } name[i]=0; skip_ws(); if(!match_char('=')){ fprintf(stderr,"FOR: missing '='\n"); input_line=NULL; return; } double startv = parse_logical_or().num; skip_ws(); if(!match_keyword("TO")){ fprintf(stderr,"FOR: missing TO\n"); input_line=NULL; return; } double endv = parse_logical_or().num; skip_ws(); double stepv=1.0; if(match_keyword("STEP")) stepv = parse_logical_or().num; Variable *v=find_var(name,1); v->value=startv; if(for_sp>=MAX_FOR_STACK){ fprintf(stderr,"FOR stack overflow\n"); input_line=NULL; return; } strncpy(for_varname[for_sp], name, sizeof(for_varname[for_sp])-1); for_end[for_sp]=endv; for_step[for_sp]=stepv; for_line_idx[for_sp]=current_line_index; for_sp++; input_line=NULL; }
static void cmd_next(const char *rest){ input_line=rest; pos=0; skip_ws(); char name[32]; int i=0; name[0]=0; if(isalpha((unsigned char)input_line[pos])){ while(isalnum((unsigned char)input_line[pos])||input_line[pos]=='$'||input_line[pos]=='_'){ if(i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)input_line[pos]); pos++; } name[i]=0; } if(for_sp<=0){ fprintf(stderr,"NEXT without FOR\n"); input_line=NULL; return; } int idx = for_sp - 1; if(name[0] && strcasecmp(name, for_varname[idx])!=0){ fprintf(stderr,"NEXT var mismatch (expected %s)\n", for_varname[idx]); input_line=NULL; return; } Variable *v = find_var(for_varname[idx],0); if(!v){ fprintf(stderr,"FOR var not found\n"); input_line=NULL; return; } v->value += for_step[idx]; int cont=0; if(for_step[idx] > 0.0){ if(v->value <= for_end[idx]) cont=1; } else { if(v->value >= for_end[idx]) cont=1; } if(cont) current_line_index = for_line_idx[idx]; else for_sp--; input_line=NULL; }

static void cmd_on_goto_gosub(const char *rest, int prefer_gosub){ input_line=rest; pos=0; double sel=parse_logical_or().num; skip_ws(); const char *r=rest; int kwpos=-1; for(int i=0;r[i];i++){ if(strncasecmp(&r[i],"GOTO",4)==0){ kwpos=i; break;} if(strncasecmp(&r[i],"GOSUB",5)==0){ kwpos=i; break;} } if(kwpos<0){ fprintf(stderr,"ON: missing GOTO/GOSUB\n"); input_line=NULL; return; } int is_gosub_kw = (strncasecmp(&r[kwpos],"GOSUB",5)==0); const char *list = rest + kwpos; while(*list && !isspace((unsigned char)*list)) list++; while(*list && isspace((unsigned char)*list)) list++; int choice=(int)sel; if(choice<1){ input_line=NULL; return; } int idx=0; const char *ptr=list; char token[128]; while(*ptr){ int t=0; while(*ptr && *ptr!=','){ if(t < (int)sizeof(token)-1) token[t++]=*ptr; ptr++; } token[t]=0; char *s=token; while(*s && isspace((unsigned char)*s)) s++; char *e=s+strlen(s)-1; while(e> s && isspace((unsigned char)*e)) *e-- = 0; idx++; if(idx==choice){ int target = (int)eval_expression_numeric(s); int found=-1; for(int i=0;i<num_lines;i++) if(program[i].number==target){ found=i; break;} if(found<0){ fprintf(stderr,"ON: target %d not found\n", target); input_line=NULL; return; } if(prefer_gosub || is_gosub_kw){ if(gosub_sp>=MAX_GOSUB){ fprintf(stderr,"GOSUB stack overflow\n"); input_line=NULL; return;} gosub_stack[gosub_sp++]=current_line_index+1; current_line_index=found-1; } else current_line_index=found-1; input_line=NULL; return; } if(*ptr==',') ptr++; } input_line=NULL; return; }

/* Execute statement */
static void execute_statement(const char *stmt){ char tmp[MAX_LINE_LEN]; strncpy(tmp, stmt, sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0; const char *p=tmp; while(*p && isspace((unsigned char)*p)) p++; if(strncasecmp(p,"PRINT",5)==0){ p+=5; cmd_print(p); return; } if(*p=='?'){ p++; cmd_print(p); return; } if(strncasecmp(p,"INPUT",5)==0){ p+=5; cmd_input(p); return; } if(strncasecmp(p,"DIM",3)==0){ p+=3; cmd_dim(p); return; } if(strncasecmp(p,"GOTO",4)==0){ p+=4; cmd_goto(p); return; } if(strncasecmp(p,"GOSUB",5)==0){ p+=5; cmd_gosub(p); return; } if(strncasecmp(p,"RETURN",6)==0){ cmd_return(); return; } if(strncasecmp(p,"IF",2)==0){ p+=2; input_line=p; pos=0; Value cond=parse_logical_or(); skip_ws(); if(match_keyword("THEN")){ skip_ws(); if(isdigit((unsigned char)input_line[pos])){ int ln = (int)parse_relational().num; if(cond.num != 0.0){ int found=-1; for(int i=0;i<num_lines;i++) if(program[i].number==ln){ found=i; break;} if(found>=0) current_line_index = found - 1; else fprintf(stderr,"IF: line %d not found\n", ln); } } else { if(cond.num != 0.0) execute_statement(input_line + pos); } } free_value(&cond); input_line=NULL; return; } if(strncasecmp(p,"FOR",3)==0){ p+=3; cmd_for(p); return; } if(strncasecmp(p,"NEXT",4)==0){ p+=4; cmd_next(p); return; } if(strncasecmp(p,"END",3)==0){ exit(0); } if(strncasecmp(p,"ON",2)==0){ p+=2; skip_ws(); cmd_on_goto_gosub(p,0); return; } const char *q=p; if(strncasecmp(q,"LET",3)==0) q+=3; while(*q && isspace((unsigned char)*q)) q++; if(isalpha((unsigned char)*q)){ char name[32]; int i=0; const char *qq=q; while(*qq && (isalnum((unsigned char)*qq) || *qq=='$' || *qq=='_')){ if(i < (int)sizeof(name)-1) name[i++] = (char)toupper((unsigned char)*qq); qq++; } name[i]=0; const char *after = qq; while(*after && isspace((unsigned char)*after)) after++; if(*after == '='){ /* array assignment? */ const char *arrp = q + strlen(name); while(*arrp && isspace((unsigned char)*arrp)) arrp++; if(*arrp == '('){ /* array assignment - parse index and rhs using temp copy */ char tmpbuf[512]; strncpy(tmpbuf, q, sizeof(tmpbuf)-1); tmpbuf[sizeof(tmpbuf)-1]=0; input_line = tmpbuf; pos = strlen(name); skip_ws(); match_char('('); Value idxv = parse_logical_or(); int idx = (int)idxv.num; free_value(&idxv); match_char(')'); match_char('='); Value val = parse_logical_or(); Variable *v = find_var(name,1); if(idx <1 || idx > v->array_size){ fprintf(stderr,"BAD SUBSCRIPT\n"); free_value(&val); input_line=NULL; return; } if(v->is_string){ if(v->sarray[idx-1]) free(v->sarray[idx-1]); v->sarray[idx-1] = val.is_string? strdup(val.str) : strdup(""); if(!val.is_string) free_value(&val); } else { double num = val.is_string? str_func_VAL(val.str): val.num; if(!v->array) v->array = calloc(v->array_size, sizeof(double)); v->array[idx-1] = num; free_value(&val); } input_line=NULL; return; } else { const char *rhs = after + 1; double num = eval_expression_numeric(rhs); Variable *v = find_var(name,1); if(v->is_string) snprintf(v->svalue,sizeof(v->svalue),"%g", num); else v->value = num; return; } } } if(*p) fprintf(stderr,"Unknown statement: %s\n", p); }

/* Run */
static void run_program(void){ for(current_line_index=0; current_line_index<num_lines; current_line_index++){ execute_statement(program[current_line_index].text); } }

/* Storage & interactive */
static void list_program(void){ int order[MAX_LINES]; int n=0; for(int i=0;i<num_lines;i++) order[n++]=i; for(int a=0;a<n;a++) for(int b=a+1;b<n;b++) if(program[order[a]].number > program[order[b]].number){ int t=order[a]; order[a]=order[b]; order[b]=t; } for(int i=0;i<n;i++){ int idx=order[i]; printf("%d %s\n", program[idx].number, program[idx].text);} }
static void store_line(int lineno, const char *text){ for(int i=0;i<num_lines;i++) if(program[i].number==lineno){ strncpy(program[i].text,text,MAX_LINE_LEN-1); program[i].text[MAX_LINE_LEN-1]=0; return;} if(num_lines>=MAX_LINES){ fprintf(stderr,"Program too large\n"); return;} program[num_lines].number=lineno; strncpy(program[num_lines].text,text,MAX_LINE_LEN-1); program[num_lines].text[MAX_LINE_LEN-1]=0; num_lines++; }
static void delete_line(int lineno){ for(int i=0;i<num_lines;i++) if(program[i].number==lineno){ memmove(&program[i],&program[i+1], (num_lines-i-1)*sizeof(ProgramLine)); num_lines--; return; } }

static void interactive_mode(void){ char line[MAX_LINE_LEN]; while(1){ printf("> "); fflush(stdout); if(!fgets(line,sizeof(line),stdin)) break; line[strcspn(line,"\n")]=0; if(strlen(line)==0) continue; char up[MAX_LINE_LEN]; strncpy(up,line,sizeof(up)-1); up[sizeof(up)-1]=0; str_upper(up); if(strcmp(up,"RUN")==0){ run_program(); continue; } if(strcmp(up,"LIST")==0){ list_program(); continue; } if(strcmp(up,"NEW")==0){ num_lines=0; num_vars=0; gosub_sp=0; for_sp=0; continue; } const char *p=line; while(*p && isspace((unsigned char)*p)) p++; if(isdigit((unsigned char)*p)){ int lineno = atoi(p); const char *rest = strchr(p,' '); if(!rest){ delete_line(lineno); continue; } rest++; while(*rest && isspace((unsigned char)*rest)) rest++; if(*rest=='?'){ char tmp[MAX_LINE_LEN]; snprintf(tmp,sizeof(tmp),"PRINT %s", rest+1); store_line(lineno,tmp); } else store_line(lineno, rest); continue; } execute_statement(line); } }

int main(void){ printf("BASIC v5_fix8 — Arrays (DIM) and string functions added\n"); interactive_mode(); return 0; }
