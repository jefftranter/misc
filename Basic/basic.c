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

// ---------------- Variable Management ----------------

Variable* find_var(const char *name) {
    for(int i=0;i<var_count;i++)
        if(strcmp(vars[i].name,name)==0) return &vars[i];
    return NULL;
}

Variable* make_var(const char *name, VarType type) {
    Variable *v = find_var(name);
    if(!v) {
        if(var_count>=MAX_VARS) { fprintf(stderr,"Too many variables\n"); longjmp(jump_buffer,1);}
        v = &vars[var_count++];
        strcpy(v->name,name);
        v->type=type;
        v->int_val=0;
        v->str_val=NULL;
        if(type==TYPE_INT_ARRAY) memset(v->int_array,0,sizeof(v->int_array));
        v->array_size=0;
    }
    return v;
}

// ---------------- Utility Functions ----------------

int eval_expr(const char *token) {
    Variable *v = find_var(token);
    if(v && v->type==TYPE_INT) return v->int_val;
    return atoi(token);
}

char* eval_str_func(const char *func, const char *arg1, const char *arg2, const char *arg3) {
    static char buf[256];
    if(strcmp(func,"LEN")==0) {
        Variable *v = find_var(arg1);
        if(v && v->type==TYPE_STRING) { sprintf(buf,"%lu",(unsigned long)strlen(v->str_val)); return buf;}
    } else if(strcmp(func,"LEFT$")==0) {
        Variable *v = find_var(arg1);
        int n = eval_expr(arg2);
        if(v && v->type==TYPE_STRING) { strncpy(buf,v->str_val,n); buf[n]='\0'; return buf;}
    } else if(strcmp(func,"RIGHT$")==0) {
        Variable *v = find_var(arg1);
        int n = eval_expr(arg2);
        if(v && v->type==TYPE_STRING) { int len = strlen(v->str_val); if(n>len) n=len; strcpy(buf,v->str_val+len-n); return buf;}
    } else if(strcmp(func,"MID$")==0) {
        Variable *v = find_var(arg1);
        int start=eval_expr(arg2)-1;
        int n=eval_expr(arg3);
        if(v && v->type==TYPE_STRING) { strncpy(buf,v->str_val+start,n); buf[n]='\0'; return buf;}
    } else if(strcmp(func,"CHR$")==0) {
        int c=eval_expr(arg1); buf[0]=(char)c; buf[1]='\0'; return buf;
    } else if(strcmp(func,"ASC")==0) {
        Variable *v = find_var(arg1);
        if(v && v->type==TYPE_STRING && strlen(v->str_val)>0) { sprintf(buf,"%d",(int)v->str_val[0]); return buf;}
    }
    strcpy(buf,"");
    return buf;
}

// ---------------- Execution ----------------

void execute_line(int index) {
    current_line_index = index;
    char linebuf[MAX_LINE_LEN];
    strcpy(linebuf,program[index].text);

    char *tokens[64];
    int n=0;
    char *p = strtok(linebuf," \t\n");
    while(p && n<64) { tokens[n++]=p; p=strtok(NULL," \t\n"); }
    if(n==0) return;

    for(int i=0;i<strlen(tokens[0]);i++) tokens[0][i]=toupper(tokens[0][i]);

    // ---------------- Commands ----------------
    if(strcmp(tokens[0],"PRINT")==0) {
        for(int i=1;i<n;i++) {
            Variable *v=find_var(tokens[i]);
            if(v) {
                if(v->type==TYPE_INT) printf("%d",v->int_val);
                else if(v->type==TYPE_STRING) printf("%s",v->str_val);
            } else {
                printf("%s",tokens[i]);
            }
            if(i<n-1) printf(" ");
        }
        printf("\n");
    } else if(strcmp(tokens[0],"LET")==0) {
        char *eq = strchr(program[index].text,'=');
        if(!eq) { fprintf(stderr,"Syntax error at line %d\n",program[index].line_no); longjmp(jump_buffer,1);}
        *eq='\0';
        char varname[32]; sscanf(program[index].text+4,"%31s",varname);
        Variable *v = make_var(varname,TYPE_INT);
        char *valstr = eq+1;
        v->int_val=eval_expr(valstr);
    } else if(strcmp(tokens[0],"GOTO")==0) {
        int target = eval_expr(tokens[1]);
        int found=-1;
        for(int i=0;i<program_lines;i++) if(program[i].line_no==target) { found=i; break;}
        if(found>=0) current_line_index=found-1;
        else { fprintf(stderr,"GOTO target %d not found\n",target); longjmp(jump_buffer,1);}
    } else if(strcmp(tokens[0],"GOSUB")==0) {
        int target = eval_expr(tokens[1]);
        if(gosub_stack_ptr>=MAX_SUBSTACK) { fprintf(stderr,"GOSUB stack overflow\n"); longjmp(jump_buffer,1);}
        gosub_stack[gosub_stack_ptr++].line_index=current_line_index+1;
        int found=-1;
        for(int i=0;i<program_lines;i++) if(program[i].line_no==target) { found=i; break;}
        if(found>=0) current_line_index=found-1;
        else { fprintf(stderr,"GOSUB target %d not found\n",target); longjmp(jump_buffer,1);}
    } else if(strcmp(tokens[0],"RETURN")==0) {
        if(gosub_stack_ptr<=0) { fprintf(stderr,"RETURN without GOSUB\n"); longjmp(jump_buffer,1);}
        current_line_index=gosub_stack[--gosub_stack_ptr].line_index-1;
    } else if(strcmp(tokens[0],"END")==0) {
        longjmp(jump_buffer,2);
    } else if(strcmp(tokens[0],"NEW")==0) {
        program_lines=0;
        var_count=0;
        gosub_stack_ptr=0;
        for_stack_ptr=0;
    } else if(strcmp(tokens[0],"ON")==0) {
        int expr = eval_expr(tokens[1]);
        char *cmd = tokens[2];
        char *list = tokens[3];
        int choice = expr;
        int idx = 0;
        char *targets[16];
        char *tok = strtok(list,",");
        while(tok && idx<16) { targets[idx++]=tok; tok=strtok(NULL,","); }
        if(choice<1 || choice>idx) return;
        int target = eval_expr(targets[choice-1]);
        int found=-1;
        for(int i=0;i<program_lines;i++) if(program[i].line_no==target) { found=i; break;}
        if(found<0) { fprintf(stderr,"ON ... target %d not found\n",target); longjmp(jump_buffer,1);}
        if(strcasecmp(cmd,"GOTO")==0) current_line_index=found-1;
        else if(strcasecmp(cmd,"GOSUB")==0) {
            if(gosub_stack_ptr>=MAX_SUBSTACK) { fprintf(stderr,"GOSUB stack overflow\n"); longjmp(jump_buffer,1);}
            gosub_stack[gosub_stack_ptr++].line_index=current_line_index+1;
            current_line_index=found-1;
        } else { fprintf(stderr,"Unknown ON command %s\n",cmd); longjmp(jump_buffer,1);}
    } else if(strcmp(tokens[0],"FOR")==0) {
        if(n<6 || strcmp(tokens[2],"=")!=0 || strcasecmp(tokens[4],"TO")!=0) {
            fprintf(stderr,"Syntax error in FOR at line %d\n",program[index].line_no); 
            longjmp(jump_buffer,1);
        }
        char *varname = tokens[1];
        int start_val = eval_expr(tokens[3]);
        int end_val = eval_expr(tokens[5]);
        int step = 1;
        if(n>=8 && strcasecmp(tokens[6],"STEP")==0) step = eval_expr(tokens[7]);

        Variable *v = make_var(varname, TYPE_INT);
        v->int_val = start_val;

        if(for_stack_ptr>=MAX_FORSTACK) { fprintf(stderr,"FOR stack overflow\n"); longjmp(jump_buffer,1);}
        FOREntry *fe = &for_stack[for_stack_ptr++];
        strcpy(fe->varname,varname);
        fe->line_index = current_line_index;
        fe->end_val = end_val;
        fe->step = step;
    } else if(strcmp(tokens[0],"NEXT")==0) {
        if(for_stack_ptr<=0) { fprintf(stderr,"NEXT without FOR\n"); longjmp(jump_buffer,1);}
        FOREntry *fe = &for_stack[for_stack_ptr-1];
        Variable *v = find_var(fe->varname);
        if(!v) { fprintf(stderr,"FOR variable not found\n"); longjmp(jump_buffer,1);}
        v->int_val += fe->step;
        if(v->int_val <= fe->end_val) current_line_index = fe->line_index;
        else for_stack_ptr--;
    } else if(n>=3 && strcmp(tokens[1],"=")==0) {
        // Optional LET
        char *varname = tokens[0];
        Variable *v = make_var(varname, TYPE_INT);
        char valbuf[256] = "";
        for(int i=2;i<n;i++) {
            strcat(valbuf, tokens[i]);
            if(i<n-1) strcat(valbuf," ");
        }
        v->int_val = eval_expr(valbuf);
    } else {
        fprintf(stderr,"Unknown command at line %d: %s\n",program[index].line_no,program[index].text);
        longjmp(jump_buffer,1);
    }
}

// ---------------- LIST & RUN ----------------

void list_program() {
    for(int i=0;i<program_lines;i++)
        if(program[i].line_no>0)
            printf("%d %s\n",program[i].line_no, program[i].text);
}

void run_program() {
    int status = setjmp(jump_buffer);
    if(status==0) {
        for(current_line_index=0; current_line_index<program_lines; current_line_index++)
            if(program[current_line_index].line_no>0)
                execute_line(current_line_index);
    }
}

// ---------------- Interactive Loop ----------------

void interactive_mode() {
    char line[256];
    while(1) {
        printf("] "); fflush(stdout);
        if(!fgets(line,sizeof(line),stdin)) break;

        line[strcspn(line,"\n")]=0;
        if(strlen(line)==0) continue;

        char upline[256];
        strcpy(upline,line);
        for(int i=0;i<strlen(upline);i++) upline[i]=toupper(upline[i]);

        if(strcmp(upline,"LIST")==0) {
            list_program();
            continue;
        } else if(strcmp(upline,"RUN")==0) {
            run_program();
            continue;
        }

        int lineno;
        if(sscanf(line,"%d",&lineno)==1) {
            int found=-1;
            for(int i=0;i<program_lines;i++)
                if(program[i].line_no==lineno) { found=i; break;}
            
            char *p = strchr(line,' ');
            if(p) {
                p++; // skip space
                // Translate ? at start to PRINT
                if(p[0]=='?') {
                    char temp[256];
                    strcpy(temp,p+1);
                    snprintf(p, MAX_LINE_LEN-(p-line), "PRINT%s", temp);
                }
            }
            
            if(found>=0) {
                if(p) strncpy(program[found].text,p,MAX_LINE_LEN);
            } else {
                if(program_lines>=MAX_LINES) { fprintf(stderr,"Too many lines\n"); continue;}
                program[program_lines].line_no=lineno;
                if(p) strncpy(program[program_lines].text,p,MAX_LINE_LEN);
                program[program_lines].text[MAX_LINE_LEN-1]='\0';
                program_lines++;
            }
        } else {
            // Immediate mode
            // Translate ? at start to PRINT
            if(line[0]=='?') {
                char temp[256];
                strcpy(temp,line+1);
                snprintf(line, sizeof(line), "PRINT%s", temp);
            }

            program[program_lines].line_no=0;
            strncpy(program[program_lines].text,line,MAX_LINE_LEN);
            int status = setjmp(jump_buffer);
            if(status==0) execute_line(program_lines);
        }
    }
}

// ---------------- Main ----------------

int main() {
    printf("Integer BASIC Interpreter (interactive) with NEW, optional LET, ?->PRINT\n");
    interactive_mode();
    return 0;
}
