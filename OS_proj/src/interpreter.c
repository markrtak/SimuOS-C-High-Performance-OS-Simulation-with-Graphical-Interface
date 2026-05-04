#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/interpreter.h"
#include "../include/memory.h"
#include "../include/mutex.h"
#include "../include/pcb.h"

static void trim(char *s) {
    char *p = s;
    while (isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

void executeInstruction(PCB *p, Scheduler *s) {
    const char *line0 = memory_get_code_line(p);
    if (!line0) {
        setState(p, FINISHED);
        return;
    }

    char line[256];
    strncpy(line, line0, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    trim(line);
    if (line[0] == '\0')
        return;

    if (strncmp(line, "semWait ", 8) == 0) {
        char res[64];
        strncpy(res, line + 8, sizeof(res) - 1);
        res[sizeof(res) - 1] = '\0';
        trim(res);
        if (semWaitResource(s, res, p))
            return;
        incrementPC(p);
        return;
    }

    if (strncmp(line, "semSignal ", 10) == 0) {
        char res[64];
        strncpy(res, line + 10, sizeof(res) - 1);
        res[sizeof(res) - 1] = '\0';
        trim(res);
        semSignalResource(s, res, p);
        incrementPC(p);
        return;
    }

    if (strncmp(line, "assign ", 7) == 0) {
        char var[VAR_NAME_LEN], word2[64], word3[64];
        int n = sscanf(line + 7, "%31s %63s %63s", var, word2, word3);
        if (n == 3 && strcmp(word2, "readFile") == 0) {
            const char *fname = pcb_get_var(p, word3);
            if (!fname) fname = word3;
            FILE *rf = fopen(fname, "r");
            char content[VAR_VAL_LEN];
            if (rf) {
                if (!fgets(content, sizeof(content), rf))
                    strcpy(content, "");
                content[strcspn(content, "\r\n")] = '\0';
                fclose(rf);
            } else {
                strcpy(content, "(file_not_found)");
            }
            pcb_set_var(p, var, content);
            incrementPC(p);
            return;
        }
        if (n >= 2) {
            if (strcmp(word2, "input") == 0) {
                char buf[VAR_VAL_LEN];
                printf("[NEED_INPUT] pid=%d var=%s\n", p->pid, var);
                fflush(stdout);
                printf("(PID %d) enter value for %s: ", p->pid, var);
                fflush(stdout);
                if (scanf("%99s", buf) != 1) {
                    printf("\n[INPUT] PID %d: no stdin token for '%s' -> using empty string\n",
                           p->pid, var);
                    fflush(stdout);
                    strcpy(buf, "");
                }
                printf("\n");
                fflush(stdout);
                pcb_set_var(p, var, buf);
                incrementPC(p);
                return;
            }
            pcb_set_var(p, var, word2);
            incrementPC(p);
            return;
        }
    }

    if (strncmp(line, "writeFile ", 10) == 0) {
        char fnVar[VAR_NAME_LEN], valVar[VAR_NAME_LEN];
        if (sscanf(line + 10, "%31s %31s", fnVar, valVar) == 2) {
            const char *fname = pcb_get_var(p, fnVar);
            const char *val   = pcb_get_var(p, valVar);
            if (!fname) fname = fnVar;
            if (!val)   val   = "";
            FILE *wf = fopen(fname, "w");
            if (wf) {
                fprintf(wf, "%s\n", val);
                fclose(wf);
            }
            incrementPC(p);
            return;
        }
    }

    if (strncmp(line, "readFile ", 9) == 0) {
        char fnVar[VAR_NAME_LEN];
        if (sscanf(line + 9, "%31s", fnVar) == 1) {
            const char *fname = pcb_get_var(p, fnVar);
            if (!fname) fname = fnVar;
            FILE *rf = fopen(fname, "r");
            char content[VAR_VAL_LEN];
            if (rf) {
                if (!fgets(content, sizeof(content), rf))
                    strcpy(content, "");
                content[strcspn(content, "\r\n")] = '\0';
                fclose(rf);
            } else {
                strcpy(content, "(file_not_found)");
            }
            printf("Process %d: readFile %s = %s\n", p->pid, fname, content);
            incrementPC(p);
            return;
        }
    }

    if (strncmp(line, "printFromTo ", 12) == 0) {
        char v1[VAR_NAME_LEN], v2[VAR_NAME_LEN];
        if (sscanf(line + 12, "%31s %31s", v1, v2) == 2) {
            const char *s1 = pcb_get_var(p, v1);
            const char *s2 = pcb_get_var(p, v2);
            int from = s1 ? atoi(s1) : 0;
            int to   = s2 ? atoi(s2) : 0;
            printf("Process %d: printFromTo %d..%d:", p->pid, from, to);
            if (from <= to) {
                for (int i = from; i <= to; i++)
                    printf(" %d", i);
            } else {
                for (int i = from; i >= to; i--)
                    printf(" %d", i);
            }
            printf("\n");
            incrementPC(p);
            return;
        }
    }

    if (strncmp(line, "print ", 6) == 0) {
        char var[VAR_NAME_LEN];
        if (sscanf(line + 6, "%31s", var) == 1) {
            const char *val = pcb_get_var(p, var);
            printf("Process %d: %s = %s\n", p->pid, var, val ? val : "(undefined)");
            incrementPC(p);
            return;
        }
    }

    fprintf(stderr, "PID %d: unknown instruction: %s\n", p->pid, line);
    incrementPC(p);
}
