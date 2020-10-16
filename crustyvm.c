/*
 * Copyright 2020 paulguy <paulguy119@gmail.com>
 *
 * This file is part of crustymidi.
 *
 * crustymidi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * crustymidi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with crustymidi.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stddef.h>
#include <math.h>
#include <sys/stat.h>

#ifdef CRUSTY_TEST
#include <stdarg.h>
#endif

#include "crustyvm.h"

#define DEBUG_MAX_PRINT (256)
#define MAX_SYMBOL_LEN (32)
#define MACRO_STACK_SIZE (32)
#define MAX_PASSES (16)
#define MAX_INCLUDE_DEPTH (16)
#define DEFAULT_CALLSTACK_SIZE (256)

#define ALIGNMENT (sizeof(int))
#define FIND_ALIGNMENT_VALUE(VALUE) \
    if((VALUE) % ALIGNMENT != 0) \
        (VALUE) += (ALIGNMENT - ((VALUE) % ALIGNMENT));

#define TOKENLEN(OFFSET) (*((int *)&(cvm->tokenmem[OFFSET])))
#define TOKENVAL(OFFSET) (&(cvm->tokenmem[OFFSET + sizeof(unsigned int)]))

#define LOG_PRINTF(CVM, FMT, ...) \
    (CVM)->log_cb((CVM)->log_priv, "%s: " FMT, (CVM)->stage, ##__VA_ARGS__)
#define LOG_PRINTF_LINE(CVM, FMT, ...) \
    (CVM)->log_cb((CVM)->log_priv, "%s:%s:%u: " FMT, \
        (CVM)->stage, \
        TOKENVAL((CVM)->line[(CVM)->logline].moduleOffset), \
        (CVM)->line[(CVM)->logline].line, \
        ##__VA_ARGS__)
#define LOG_PRINTF_BARE(CVM, FMT, ...) \
    (CVM)->log_cb((CVM)->log_priv, FMT, ##__VA_ARGS__)

typedef struct {
    unsigned int ip;
    unsigned int proc;
} CrustyCallStackArg;

typedef struct {
    unsigned int flags;
    unsigned int val;
    unsigned int index;
    unsigned int ptr;
} CrustyStackArg;

typedef struct {
    unsigned long *offset;
    unsigned int tokencount;

    long moduleOffset;
    unsigned int line;

    unsigned int instruction;
} CrustyLine;

typedef struct CrustyProcedure_s CrustyProcedure;

typedef struct {
    long nameOffset;
    const char *name;
    CrustyType type;
    int procIndex;
    struct CrustyProcedure_s *proc;
    unsigned int length; /* array length
                            0  if reference (local argument)
                            1  if a single value
                            >1 if array */
    unsigned int offset; /* offset in to stack
                            position of value in local stack (from stack pointer) if local
                            position of reference in local stack if reference
                            position in global stack (from 0) if global */

    /* callbacks for IO, both NULL if not IO */
    CRUSTY_IO_READ_FUNC_DECL(read);
    void *readpriv;
    CRUSTY_IO_WRITE_FUNC_DECL(write);
    void *writepriv;
} CrustyVariable;

typedef struct {
    long nameOffset;
    const char *name;
    unsigned int line;
} CrustyLabel;

typedef struct CrustyProcedure_s {
    long nameOffset;
    const char *name;
    unsigned int start;
    unsigned int length;
    unsigned int args;
    unsigned int instruction;

    int *varIndex;
    CrustyVariable **var;
    unsigned int vars;

    unsigned int stackneeded;
    unsigned char *initializer;

    CrustyLabel *label;
    unsigned int labels;
} CrustyProcedure;

typedef enum {
    CRUSTY_INSTRUCTION_TYPE_MOVE,
    CRUSTY_INSTRUCTION_TYPE_ADD,
    CRUSTY_INSTRUCTION_TYPE_SUB,
    CRUSTY_INSTRUCTION_TYPE_MUL,
    CRUSTY_INSTRUCTION_TYPE_DIV,
    CRUSTY_INSTRUCTION_TYPE_MOD,
    CRUSTY_INSTRUCTION_TYPE_AND,
    CRUSTY_INSTRUCTION_TYPE_OR,
    CRUSTY_INSTRUCTION_TYPE_XOR,
    CRUSTY_INSTRUCTION_TYPE_SHR,
    CRUSTY_INSTRUCTION_TYPE_SHL,
    CRUSTY_INSTRUCTION_TYPE_CMP,
    CRUSTY_INSTRUCTION_TYPE_JUMP,
    CRUSTY_INSTRUCTION_TYPE_JUMPN,
    CRUSTY_INSTRUCTION_TYPE_JUMPZ,
    CRUSTY_INSTRUCTION_TYPE_JUMPL,
    CRUSTY_INSTRUCTION_TYPE_JUMPG,
    CRUSTY_INSTRUCTION_TYPE_CALL,
    CRUSTY_INSTRUCTION_TYPE_RET
} CrustyInstructionType;

#define MOVE_DEST_FLAGS (1)
#define MOVE_DEST_VAL   (2)
#define MOVE_DEST_INDEX (3)
#define MOVE_SRC_FLAGS  (4)
#define MOVE_SRC_VAL    (5)
#define MOVE_SRC_INDEX  (6)
#define MOVE_ARGS MOVE_SRC_INDEX

#define MOVE_FLAG_TYPE_MASK (3)
#define MOVE_FLAG_IMMEDIATE (0)
#define MOVE_FLAG_VAR       (1)
#define MOVE_FLAG_LENGTH    (2)
#define MOVE_FLAG_INVALID   (3)

#define MOVE_FLAG_INDEX_TYPE_MASK (1 << 2)
#define MOVE_FLAG_INDEX_IMMEDIATE (0 << 2)
#define MOVE_FLAG_INDEX_VAR (1 << 2)

#define JUMP_LOCATION (1)
#define JUMP_ARGS JUMP_LOCATION

#define CALL_PROCEDURE (1)
#define CALL_START_ARGS (2)
#define CALL_ARG_FLAGS (0)
#define CALL_ARG_VAL (1)
#define CALL_ARG_INDEX (2)
#define CALL_ARG_SIZE (CALL_ARG_INDEX + 1)

#define RET_ARGS (0)

typedef struct CrustyVM_s {
    void (*log_cb)(void *priv, const char *fmt, ...);
    void *log_priv;

    unsigned int flags;

/* compile-time data */

/* logging things */
    unsigned int logline;
    const char *stage;

    CrustyLine *line;
    unsigned int lines;

    char *tokenmem;
    int tokenmemlen;

    CrustyVariable *var;
    unsigned int vars;

    CrustyProcedure *proc;
    unsigned int procs;

    int *inst;
    unsigned int insts;

    unsigned int stacksize;
    unsigned int initialstack;
    unsigned char *initializer;

    unsigned int callstacksize;

    /* runtime data */
    unsigned char *stack; /* runtime stack */
    CrustyCallStackArg *cstack; /* call stack */
    unsigned int sp; /* stack pointer */
    unsigned int csp; /* callstack pointer */
    unsigned int ip; /* instruction pointer */
    /* result of last operation, for conditional jumps */
    CrustyType resulttype;
    double floatresult;
    int intresult;
    CrustyStatus status;
} CrustyVM;

/* compile-time stuff */

typedef struct {
    long nameOffset;
    unsigned int start;

    long *argOffset;
    unsigned int argcount;
} CrustyMacro;

typedef enum {
    CRUSTY_EXPR_NUMBER,
    CRUSTY_EXPR_LPAREN,
    CRUSTY_EXPR_RPAREN,
    CRUSTY_EXPR_PLUS,
    CRUSTY_EXPR_MINUS,
    CRUSTY_EXPR_MULTIPLY,
    CRUSTY_EXPR_DIVIDE,
    CRUSTY_EXPR_MODULO,
    CRUSTY_EXPR_EQUALS,
    CRUSTY_EXPR_NEQUALS,
    CRUSTY_EXPR_LESS,
    CRUSTY_EXPR_GREATER,
    CRUSTY_EXPR_LEQUALS,
    CRUSTY_EXPR_GEQUALS,
    CRUSTY_EXPR_AND,
    CRUSTY_EXPR_OR,
    CRUSTY_EXPR_XOR,
    CRUSTY_EXPR_NAND,
    CRUSTY_EXPR_NOR,
    CRUSTY_EXPR_XNOR,
    CRUSTY_EXPR_LSHIFT,
    CRUSTY_EXPR_RSHIFT
} CrustyExprOp;

typedef struct CrustyExpr_s {
    CrustyExprOp op;

    int number;

    struct CrustyExpr_s *prev;
    struct CrustyExpr_s *next;
} CrustyExpr;

const char *CRUSTY_STATUSES[] = {
    "Ready",
    "Active",
    "Internal error/VM bug",
    "Array access out of range",
    "Invalid instruction",
    "Stack overflow",
    "Callback returned failure",
    "Float used as index",
    "Invalid status code"
};

FILE *crustyvm_open_file(const char *filename,
                         char **safepath,
                         void (*log_cb)(void *priv, const char *fmt, ...),
                         void *log_priv) {
    FILE *in;
    char *fullpath;
    char *slash;
    struct stat filestat;

    if(stat(filename, &filestat) < 0) {
        log_cb(log_priv, "Failed to stat %s.\n", filename);
        return(NULL);
    }
    if(!S_ISREG(filestat.st_mode)) {
        log_cb(log_priv, "Not a file: %s\n", filename);
        return(NULL);
    }
    
    fullpath = realpath(filename, NULL);
    if(fullpath == NULL) {
        log_cb(log_priv, "Failed to get full path.\n");
        return(NULL);
    }
    /* not likely but may as well */
    slash = strrchr(fullpath, '/');
    if(slash[1] == '\0') {
        log_cb(log_priv, "Invalid path? %s\n", fullpath);
        return(NULL);
    }
    slash[1] = '\0';
    if(*safepath != NULL) {
        if(strncmp(fullpath, *safepath, strlen(*safepath)) != 0) {
            log_cb(log_priv, "File attempted to be accessed from unsafe path: "
                              "%s\n", fullpath);
            free(fullpath);
            return(NULL);
        }
        free(fullpath);
    } else {
        *safepath = fullpath;
    }

    in = fopen(filename, "rb");
    if(in == NULL) {
        log_cb(log_priv, "Failed to open file %s.\n", filename);
        return(NULL);
    }

    return(in);
}

static CrustyVM *init() {
    CrustyVM *cvm;

    cvm = malloc(sizeof(CrustyVM));
    if(cvm == NULL) {
        return(NULL);
    }

    cvm->log_cb = NULL;
    cvm->log_priv = NULL;
    cvm->stage = NULL;
    cvm->line = NULL;
    cvm->lines = 0;
    cvm->tokenmem = NULL;
    cvm->tokenmemlen = 0;
    cvm->var = NULL;
    cvm->vars = 0;
    cvm->proc = NULL;
    cvm->procs = 0;
    cvm->inst = NULL;
    cvm->insts = 0;
    cvm->stack = NULL;
    cvm->cstack = NULL;
    cvm->initialstack = 0;
    cvm->initializer = NULL;

    return(cvm);
}

void crustyvm_free(CrustyVM *cvm) {
    unsigned int i;

    if(cvm->line != NULL) {
        for(i = 0; i < cvm->lines; i++) {
            if(cvm->line[i].offset != NULL) {
                free(cvm->line[i].offset);
            }
        }
        free(cvm->line);
    }

    if(cvm->tokenmem != NULL) {
        free(cvm->tokenmem);
    }

    if(cvm->proc != NULL) {
        for(i = 0; i < cvm->procs; i++) {
            if(cvm->proc[i].varIndex != NULL) {
                free(cvm->proc[i].varIndex);
            }
            if(cvm->proc[i].var != NULL) {
                free(cvm->proc[i].var);
            }
            if(cvm->proc[i].label != NULL) {
                free(cvm->proc[i].label);
            }
            if(cvm->proc[i].initializer != NULL) {
                free(cvm->proc[i].initializer);
            }
        }
        free(cvm->proc);
    }

    if(cvm->var != NULL) {
        free(cvm->var);
    }

    if(cvm->inst != NULL) {
        free(cvm->inst);
    }

    if(cvm->stack != NULL) {
        free(cvm->stack);
    }

    if(cvm->cstack != NULL) {
        free(cvm->cstack);
    }

    if(cvm->initializer != NULL) {
        free(cvm->initializer);
    }

    free(cvm);
}

static long add_token(CrustyVM *cvm,
                     const char *token,
                     unsigned long len,
                     int quoted,
                     unsigned int *line) {
    char *temp;
    unsigned long oldlen;
    unsigned long srcpos, destpos;
    char hexchr[3];
    char *end;
    unsigned char value;
    unsigned long newlen;

    oldlen = cvm->tokenmemlen;
    /* original memory + new length tag + new string + null terminator for the
     * cases where a string may be printed. */
    newlen = sizeof(unsigned int) + cvm->tokenmemlen + len + 1;
    FIND_ALIGNMENT_VALUE(newlen)
    temp = realloc(cvm->tokenmem, newlen);
    if(temp == NULL) {
        return(-1);
    }
    cvm->tokenmem = temp;
    temp = TOKENVAL(cvm->tokenmemlen);

    if(token == NULL) { /* just allocate the space and return it */
        TOKENLEN(cvm->tokenmemlen) = len;
        cvm->tokenmemlen += sizeof(unsigned int) + len + 1;
        TOKENVAL(oldlen)[len] = '\0';
    } else {
        if(quoted) { /* much slower method and uncommonly used */
            srcpos = 0;
            destpos = 0;
            for(srcpos = 0; srcpos < len; srcpos++) {
                if(token[srcpos] == '\\') {
                    if(srcpos + 1 == len) {
                        LOG_PRINTF(cvm, "Lone escape char at end of string.\n");
                        return(-1);
                    }
                    srcpos++;
                    switch(token[srcpos]) {
                        case 'r':
                            temp[destpos] = '\r';
                            destpos++;
                            break;
                        case 'n':
                            temp[destpos] = '\n';
                            destpos++;
                            break;
                        case '\n':
                            /* escape a newline to eliminate it */
                            /* including multichar newlines */
                            if(srcpos + 1 < len &&
                               token[srcpos + 1] == '\r') {
                                srcpos++;
                            }
                            /* increment line count passed in */
                            (*line)++;
                            break;
                        case '\r':
                            /* same as above */
                            if(srcpos + 1 < len &&
                               token[srcpos + 1] == '\n') {
                                srcpos++;
                            }
                            break;
                        case '\\':
                            temp[destpos] = '\\';
                            destpos++;
                            break;
                        case 'x':
                            if(srcpos + 2 == len) {
                                LOG_PRINTF(cvm, "Hex escape sequence at the "
                                                "end of string.\n");
                                return(-1);
                            }
                            hexchr[0] = token[srcpos + 1];
                            hexchr[1] = token[srcpos + 2];
                            value = strtoul(hexchr, &end, 16);
                            if(end != &(hexchr[2])) {
                                LOG_PRINTF(cvm, "Incomplete hex escape "
                                                "sequence.\n");
                                return(-1);
                            }
                            temp[destpos] = value;
                            destpos++;
                            srcpos += 2;
                            break;
                        case '"':
                            temp[destpos] = '"';
                            destpos++;
                            break;
                        default:
                            LOG_PRINTF(cvm, "Invalid escape sequence: \\%c.\n", token[srcpos]);
                            return(-1);
                    }
                } else {
                    if(token[srcpos] == '\n') {
                        /* same */
                        (*line)++;
                    }
                    temp[destpos] = token[srcpos];
                    destpos++;
                }
            }
            temp[destpos] = '\0';
            if(destpos < len) {
                newlen = cvm->tokenmemlen + sizeof(unsigned int) + destpos + 1;
                FIND_ALIGNMENT_VALUE(newlen)
                temp = realloc(cvm->tokenmem, newlen);
                if(temp == NULL) {
                    LOG_PRINTF(cvm, "Failed to trim tokenmem.\n");
                    return(-1);
                }
                cvm->tokenmem = temp;
            }
            TOKENLEN(cvm->tokenmemlen) = destpos;
            cvm->tokenmemlen = newlen;
        } else {
            memcpy(temp, token, len);
            temp[len] = '\0';
            TOKENLEN(cvm->tokenmemlen) = len;
            cvm->tokenmemlen = newlen;
        }
    }

    return((long)oldlen);
}

static int compare_token_and_string(CrustyVM *cvm,
                                    long offset,
                                    const char *str) {
    int len = strlen(str);

    /* this is not appropriate for sorting strings, but that's OK */
    if(len != TOKENLEN(offset)) {
        return(-1);
    }

    return(memcmp(TOKENVAL(offset), str, len));
}

static int compare_token_and_token(CrustyVM *cvm,
                                   long offset1,
                                   long offset2) {
    if(TOKENLEN(offset1) != TOKENLEN(offset2)) {
        return(-1);
    }

    return(memcmp(TOKENVAL(offset1), TOKENVAL(offset2), TOKENLEN(offset1)));
}

#define GET_TOKEN_OFFSET(LINE, TOKEN) (cvm->line[LINE].offset[TOKEN])
#define GET_TOKEN(LINE, TOKEN) TOKENVAL(GET_TOKEN_OFFSET(LINE, TOKEN))

#define ISJUNK(X) ((X) == ' ' || \
                   (X) == '\t' || \
                   (X) == '\r' || \
                   (X) == '\n' || \
                   (X) == ';')

#define PROGRAM (includestack[includestackptr])
#define LEN (includesize[includestackptr])
#define MODULE (includemodule[includestackptr])
#define LINE (includeline[includestackptr])
#define POS (includepos[includestackptr])

/* this is before a bunch of stuff is guaranteed to be set up, so just fetch
   the same information from local state */
#define LOG_PRINTF_TOK(CVM, FMT, ...) \
    (CVM)->log_cb((CVM)->log_priv, "%s:%s:%u: " FMT, \
        (CVM)->stage, \
        TOKENVAL(MODULE), \
        LINE, \
        ##__VA_ARGS__)

static int tokenize(CrustyVM *cvm,
                    const char *modulename,
                    char *safepath,
                    const char *programdata,
                    unsigned long programdatalen) {
    unsigned int i, j;
    CrustyLine *temp;
    unsigned long linelen, lineend;
    unsigned long cursor;
    int scanningjunk;
    int quotedstring;
    long tokenstart;
    unsigned int linesmem;
    long filelen;

    /* data to read from */
    const char *includestack[MAX_INCLUDE_DEPTH];
    /* length of module */
    unsigned long includesize[MAX_INCLUDE_DEPTH];
    /* name of module */
    long includemodule[MAX_INCLUDE_DEPTH];
    /* line within current module, for line metadata */
    unsigned int includeline[MAX_INCLUDE_DEPTH];
    /* byte in current module */
    unsigned long includepos[MAX_INCLUDE_DEPTH];
    unsigned int includestackptr = 0;

    PROGRAM = programdata;
    LEN = programdatalen;
    tokenstart = add_token(cvm, modulename, strlen(modulename), 0, NULL);
    if(tokenstart < 0) {
        LOG_PRINTF(cvm, "Failed to allocate memory for module name.\n");
        return(-1);
    }
    MODULE = tokenstart;
    LINE = 0;
    POS = 0;

    cvm->lines = 0; /* current line */
    linesmem = 0; /* actual size of array */
    for(;;) {
        LINE++;
        /* find the end of meaningful line contents and total size of line up to
           the start of the next line */
        lineend = 0;
        for(linelen = 0; POS + linelen < LEN; linelen++) {
            if(PROGRAM[POS + linelen] == '\r') {
                linelen++;
                /* mark this character as the end of the line, unless a comment
                   was previously found, then that is the real line end, so
                   don't overwrite it */
                if(lineend == 0 && PROGRAM[POS] != ';') {
                    lineend = linelen;
                }
                if(POS + linelen < LEN - 1 &&
                   PROGRAM[POS + linelen] == '\n') {
                    linelen++;
                }
                break;
            } else if(PROGRAM[POS + linelen] == '\n') {
                linelen++;
                /* same as above */
                if(lineend == 0 && PROGRAM[POS] != ';') {
                    lineend = linelen;
                }
                if(POS + linelen < LEN - 1 &&
                   PROGRAM[POS + linelen] == '\r') {
                    linelen++;
                }
                break;
            } else if(PROGRAM[POS + linelen] == '"') {
                /* allow quoted strings to span lines by scanning until the next
                   quote (or end of file) is found */

                /* ignore quoted strings in comments */
                if(lineend == 0) {
                    while(POS + linelen < LEN - 1) {
                        linelen++;
                        if(PROGRAM[POS + linelen] == '"' &&
                           PROGRAM[POS + linelen - 1] != '\\') {
                            break;
                        }
                    }
                    if(POS + linelen == LEN - 1 && PROGRAM[POS + linelen] != '"') {
                        LOG_PRINTF_TOK(cvm, "Quoted string reached end of file.\n");
                        return(-1);
                    }
                }
            } else if(PROGRAM[POS + linelen] == ';') { /* comments */
                /* only count the first found comment */
                if(lineend == 0) {
                    lineend = linelen;
                }
            }
        }
        /* scanning reached the end of file without hitting a newline, so the
           line length is just the rest of the file */
        if(POS + linelen == LEN) {
            lineend = linelen;
        }

        /* allocate memory for a new line if needed */
        if(cvm->lines + 1 > linesmem) {
            temp = realloc(cvm->line, sizeof(CrustyLine) * (linesmem + 1));
            if(temp == NULL) {
                LOG_PRINTF_TOK(cvm, "Failed to allocate memory for lines list.\n");
                return(-1);
            }
            cvm->line = temp;
            linesmem++;
        }

        cvm->line[cvm->lines].tokencount = 0;
        cvm->line[cvm->lines].offset = NULL;
        cvm->line[cvm->lines].moduleOffset = MODULE;
        cvm->line[cvm->lines].line = LINE;

        /* find starts and ends of tokens and insert them in to the line entry

           assume we'll start with junk so if there is no junk at the start
           of the line, the first token will be marked at 0 */
        scanningjunk = 1;
        quotedstring = 0;
        for(cursor = 0; cursor < lineend; cursor++) {
            if(!quotedstring) {
                if(scanningjunk) {
                    /* if we're scanning for junk and there's still junk,
                       nothing more to do. */
                    if(ISJUNK(PROGRAM[POS + cursor])) {
                        continue;
                    }

                    /* no longer junk, so create a space for the offset in to
                       token memory. */
                    cvm->line[cvm->lines].offset =
                        realloc(cvm->line[cvm->lines].offset,
                                sizeof(unsigned long) *
                                (cvm->line[cvm->lines].tokencount + 1));
                    if(cvm->line[cvm->lines].offset == NULL) {
                        LOG_PRINTF_TOK(cvm, "Couldn't allocate memory for offsets.\n");
                        return(-1);
                    }

                    /* check if at the start of a quoted string */
                    if(PROGRAM[POS + cursor] == '"') {
                        /* point the start to the next character, which will
                           for sure exist because a quote at the end of the line
                           is previously checked for */
                        cursor++;
                        tokenstart = POS + cursor;
                        quotedstring = 1;
                        /* don't reset junk scanning because there could be junk
                           directly following a quoted string */
                        continue;
                    }

                    /* start scanning non-junk */
                    tokenstart = POS + cursor;
                    scanningjunk = 0;

                    continue;
                }

                /* if junk wasn't found, continue scanning */
                if(!ISJUNK(PROGRAM[POS + cursor])) {
                    continue;
                }

                /* transition from not junk to junk by adding the token to token
                   memory, then pointing the current offset to the token */
                tokenstart = add_token(cvm,
                                       &(PROGRAM[tokenstart]),
                                       POS + cursor - tokenstart,
                                       0,
                                       NULL);
                if(tokenstart < 0) {
                    LOG_PRINTF_TOK(cvm, "Couldn't create token.\n");
                    return(-1);
                }
                cvm->line[cvm->lines].offset[cvm->line[cvm->lines].tokencount] =
                    tokenstart;
                cvm->line[cvm->lines].tokencount++;

                scanningjunk = 1;
            } else {
                /* this check is safe because cursor will have been incremented
                   at least once to get to this point. */
                /* second check is always safe because a quote was previously
                 * found and the cursor pointer was advanced one, so there is
                 * at least a quote there before here.  this allows for
                 * quotation marks to appear in strings at all. */
                if(PROGRAM[POS + cursor] == '"' &&
                   PROGRAM[POS + cursor - 1] != '\\') {
                    /* transition from quoted string to junk. Same as above. */
                    tokenstart = add_token(cvm,
                                           &(PROGRAM[tokenstart]),
                                           POS + cursor - tokenstart,
                                           1, /* quoted */
                                           &(LINE));
                    if(tokenstart < 0) {
                        LOG_PRINTF_TOK(cvm, "Couldn't create token.\n");
                        return(-1);
                    }
                    cvm->line[cvm->lines].offset[cvm->line[cvm->lines].tokencount] =
                        tokenstart;
                    cvm->line[cvm->lines].tokencount++;

                    scanningjunk = 1;
                    quotedstring = 0;
                }
            }
        }

        /* check for includes */
        if(cvm->line[cvm->lines].tokencount > 0) {
            if(compare_token_and_string(cvm,
                                        GET_TOKEN_OFFSET(cvm->lines, 0),
                                        "include") == 0) {
                if(cvm->line[cvm->lines].tokencount != 2) {
                    LOG_PRINTF_TOK(cvm, "include takes a single filename");
                    return(-1);
                }

                if(includestackptr == MAX_INCLUDE_DEPTH) {
                    LOG_PRINTF_TOK(cvm, "Includes too deep.\n");
                    return(-1);
                }

                /* make sure the same file isn't included from cyclicly */
                for(i = 0; i <= includestackptr; i++) {
                    if(compare_token_and_token(cvm,
                                               GET_TOKEN_OFFSET(cvm->lines, 1),
                                               includemodule[i]) == 0) {
                        LOG_PRINTF_TOK(cvm, "Circular includes.\n");
                        LOG_PRINTF_TOK(cvm, "%s\n", TOKENVAL(includemodule[0]));
                        for(j = 1; j <= includestackptr; j++) {
                            LOG_PRINTF_TOK(cvm, "-> %d include %s\n",
                                                includeline[j],
                                                TOKENVAL(includemodule[j]));
                        }
                        LOG_PRINTF_TOK(cvm, "!! %d include %s\n",
                                            cvm->lines,
                                            GET_TOKEN(cvm->lines, 1));
                        return(-1);
                    }
                }

                /* load the file in to program memory.  Can't use any of the
                   convenience macros though so log messages point to the right
                   line/module */
                FILE *in;
                in = crustyvm_open_file(GET_TOKEN(cvm->lines, 1),
                                        &safepath,
                                        cvm->log_cb,
                                        cvm->log_priv);
                if(in == NULL) {
                    LOG_PRINTF_TOK(cvm, "Failed to open include file %s.\n",
                                        GET_TOKEN(cvm->lines, 1));
                    return(-1);
                }

                if(fseek(in, 0, SEEK_END) < 0) {
                    LOG_PRINTF_TOK(cvm, "Failed to seek include file.\n");
                    return(-1);
                }

                filelen = ftell(in);
                if(filelen < 0) {
                    LOG_PRINTF_TOK(cvm, "Failed to get include file size.\n");
                    return(-1);
                }
                includesize[includestackptr+1] = (unsigned long)filelen;

                includestack[includestackptr+1] = malloc(includesize[includestackptr+1]);
                if(includestack[includestackptr+1] == NULL) {
                    LOG_PRINTF_TOK(cvm, "Failed to allocate memory for include.\n");
                    return(-1);
                }

                /* read the contents in to memory */
                rewind(in);
                /* needs to be made non-const so this buffer can be read in to,
                   but the array is of const char ** because the 0th entry is
                   always the const char passed in to the function, which is
                   never modified. */
                if(fread((char *)(includestack[includestackptr+1]),
                         1,
                         includesize[includestackptr+1],
                         in) < includesize[includestackptr+1]) {
                    LOG_PRINTF_TOK(cvm, "Failed to read include file.\n");
                    return(-1);
                }
                fclose(in);

                /* go past the include line */
                POS += linelen;

                /* add the module name */
                includestackptr++;
                MODULE = cvm->line[cvm->lines].offset[1];
                LINE = 0;
                POS = 0;

                /* we're done with this line and it won't end up in the line
                   list so free its offsets and it will be reused, tokencount
                   will be reset later */
                free(cvm->lines[cvm->line].offset);
                cvm->lines[cvm->line].offset = NULL;

                /* don't advance line count */
                continue;
            } else { /* no include, so just advance things normal */
                POS += linelen;
                cvm->lines++;
            }
        } else { /* don't have lines increment if it's a blank line */
            /* no need to free offset because no token was ever found */
            POS += linelen;
        }

        /* reached the end, so pop it off, if already at the bottom, tokenizing
           is done */
        if(POS == LEN) {
            if(includestackptr == 0) {
                break;
            }
            /* see comment above before fread() */
            free((char *)PROGRAM);
            includestackptr--;
        }
    }

    return(0);
}

#undef LOG_PRINTF_TOK
#undef LINE
#undef MODULE
#undef LEN
#undef PROGRAM
#undef ISJUNK

static CrustyMacro *find_macro(CrustyVM *cvm,
                               CrustyMacro *macro,
                               unsigned int count,
                               const char *name) {
    unsigned int i;

    for(i = 0; i < count; i++) {
        if(compare_token_and_string(cvm,
                                    macro[i].nameOffset,
                                    name) == 0) {
            return(&(macro[i]));
        }
    }

    return(NULL);
}

static long string_replace(CrustyVM *cvm,
                            long tokenOffset,
                            long macroOffset,
                            long replaceOffset) {
    char *token = TOKENVAL(tokenOffset);
    char *macro = TOKENVAL(macroOffset);
    char *replace = TOKENVAL(replaceOffset);
    int macrofound = 0;
    char *macroInToken;
    int tokenlen = TOKENLEN(tokenOffset);
    int macrolen = TOKENLEN(macroOffset);
    int replacelen = TOKENLEN(replaceOffset);
    int macroInTokenLen;
    int betweenlen;
    char *temp;
    int newlen;
    int i;
    int srcpos, dstpos;
    long tokenstart;

    /* scan the token to find the number of instances of the macro */
    macroInToken = memmem(token, tokenlen, macro, macrolen);
    while(macroInToken != NULL) {
        macroInTokenLen = macroInToken - token;
        macrofound++;
        /* crusty pointer arithmetic that might not be
           safe to make sure there are more characters
           to search within or if the last result is
           at the end of the string anyway. */
        if(macroInTokenLen + macrolen < tokenlen) {
            /* start checking 1 character in so the same
               string isn't found again, safe because
               there is at least an additional character
               after this */
            macroInToken = memmem(macroInToken + 1, tokenlen - macroInTokenLen - 1, macro, macrolen);
        } else { /* already at end of token */
            break;
        }
    }

    /* if nothing, just return the token so it can be assigned to whatever
       needed a potentially processed token */
    if(macrofound == 0) {
        return(tokenOffset);
    }

    /* token take away the macros, add replacements */
    newlen = tokenlen - (macrolen * macrofound) + (replacelen * macrofound);

    /* will return an index in to a null terminated buffer long enough for newlen */
    tokenstart = add_token(cvm, NULL, newlen, 0, NULL);
    if(tokenstart < 0) {
        LOG_PRINTF(cvm, "Failed to add string replace memory to extra memory.\n");
        return(-1);
    }
    temp = TOKENVAL(tokenstart);
    /* update these because they may have moved */
    token = TOKENVAL(tokenOffset);
    macro = TOKENVAL(macroOffset);
    replace = TOKENVAL(replaceOffset);

    /* alternate scanning for macros, copying from the token in to the
       destination up until the found macro, then copy the replacement in to the
       destination instead of the macro. */
    srcpos = 0;
    dstpos = 0;
    for(i = 0; i < macrofound; i++) {
        /* find macro */
        macroInToken = memmem(&(token[srcpos]), tokenlen - srcpos, macro, macrolen);
        /* more funky pointer arithmetic */
        betweenlen = macroInToken - &(token[srcpos]);
        /* copy from last found macro (or beginning) up until the beginning of
           the found macro */
        memcpy(&(temp[dstpos]), &(token[srcpos]), betweenlen);
        dstpos += betweenlen;
        srcpos += betweenlen;
        /* copy the replacement */
        memcpy(&(temp[dstpos]), replace, replacelen);
        dstpos += replacelen;
        srcpos += macrolen;
    }

    /* see if there's anything after the last macro replacement to copy over */
    memcpy(&(temp[dstpos]), &(token[srcpos]), newlen - dstpos);

    return(tokenstart);
}

/* I wrote this kinda crappily, and when adding a bunch more operators it got
   extra crappy.  I probably won't rewrite it because i'd have to basically
   either figure out what I did or just rewrite it from scratch to be less silly
   but it probably doesn't really matter.  It won't be noticeably slow for any
   program written in this, and it doesn't affect runtime speed whatsoever. */
static CrustyExpr *do_expression(CrustyVM *cvm, CrustyExpr *expr) {
    CrustyExpr *cursor;
    CrustyExpr *innercursor;
    CrustyExpr *eval;
    int level;

    /* scan for parentheses, this function assumes matched parentheses */
    cursor = expr;
    while(cursor != NULL) {
        if(cursor->op == CRUSTY_EXPR_LPAREN) {
            level = 1;
            /* this won't crash because a previous test determined that every
               left parenthesis will always have a matching right parenthesis */
            innercursor = cursor;
            while(level > 0) {
                innercursor = innercursor->next;
                if(innercursor->op == CRUSTY_EXPR_LPAREN) {
                    level++;
                } else if(innercursor->op == CRUSTY_EXPR_RPAREN) {
                    level--;
                }
            }
            if(cursor->next == innercursor) {
                LOG_PRINTF_LINE(cvm, "Empty parentheses in evaluation.\n");
                return(NULL);
            }
            /* evaluate the inner expression in isolation */
            innercursor->prev->next = NULL;
            cursor->next->prev = NULL;
            eval = do_expression(cvm, cursor->next);
            if(eval == NULL) {
                /* don't log this so we don't get repeated reports of evaluation
                   failing all the way down the stack. */
                return(NULL);
            }
            /* at this point, the entire expression within the parentheses 
               should be collapsed in to a single value.  Make eval point to the
               items before the left parenthesis and after the right parenthesis
               and if there's anything actually there, make sure those point to
               eval, effectively cutting out the parentheses and any expression
               inside and inserting just the single value which the inner
               expression evaluated to. */
            eval->prev = cursor->prev;
            if(eval->prev != NULL) {
                eval->prev->next = eval;
            } else { /* cursor was a leading parenthesis, so make the start of
                        expression point to this value instead */
                expr = eval;
            }
            eval->next = innercursor->next;
            if(eval->next != NULL) {
                eval->next->prev = eval;
            } /* nothing to do because there's no tail pointer */

            cursor = eval;
        }

        cursor = cursor->next;
    }

    /* at this point, there should be no more parentheses, if there were any in
       the first place and eval should start with a number, alternate between an
       operation and a number until terminating on a number */

    /* operation precedence based on C, described from:
       https://en.cppreference.com/w/c/language/operator_precedence */

    /* multiplication and division */
    cursor = expr;
    while(cursor != NULL) {
        if(cursor->op == CRUSTY_EXPR_MULTIPLY ||
           cursor->op == CRUSTY_EXPR_DIVIDE ||
           cursor->op == CRUSTY_EXPR_MODULO) {
            if(cursor->prev == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing before.\n");
                return(NULL);
            }
            if(cursor->prev->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number before.\n");
                return(NULL);
            }
            if(cursor->next == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing after.\n");
                return(NULL);
            }
            if(cursor->next->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number after.\n");
                return(NULL);
            }

            if(cursor->op == CRUSTY_EXPR_MULTIPLY) {
                cursor->prev->number = cursor->prev->number * cursor->next->number;
            } else if(cursor->op == CRUSTY_EXPR_DIVIDE) {
                cursor->prev->number = cursor->prev->number / cursor->next->number;
            } else {
                cursor->prev->number = cursor->prev->number % cursor->next->number;
            }

            /* the operator and second operand are unimportant now so move the
               context to the result value and repoint it to the value following
               the second operand */
            cursor = cursor->prev;
            cursor->next = cursor->next->next->next;
            if(cursor->next != NULL) {
                cursor->next->prev = cursor;
            }
        }
        cursor = cursor->next;
    }

    /* addition and subtraction */
    cursor = expr;
    while(cursor != NULL) {
        if(cursor->op == CRUSTY_EXPR_PLUS ||
           cursor->op == CRUSTY_EXPR_MINUS) {
            if(cursor->prev == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing before.\n");
                return(NULL);
            }
            if(cursor->prev->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number before.\n");
                return(NULL);
            }
            if(cursor->next == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing after.\n");
                return(NULL);
            }
            if(cursor->next->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number after.\n");
                return(NULL);
            }

            if(cursor->op == CRUSTY_EXPR_PLUS) {
                cursor->prev->number = cursor->prev->number + cursor->next->number;
            } else {
                cursor->prev->number = cursor->prev->number - cursor->next->number;
            }

            /* the operator and second operand are unimportant now so move the
               context to the result value and repoint it to the value following
               the second operand */
            cursor = cursor->prev;
            cursor->next = cursor->next->next->next;
            if(cursor->next != NULL) {
                cursor->next->prev = cursor;
            }
        }
        cursor = cursor->next;
    }

    /* bit shift */
    cursor = expr;
    while(cursor != NULL) {
        if(cursor->op == CRUSTY_EXPR_LSHIFT ||
           cursor->op == CRUSTY_EXPR_RSHIFT) {
            if(cursor->prev == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing before.\n");
                return(NULL);
            }
            if(cursor->prev->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number before.\n");
                return(NULL);
            }
            if(cursor->next == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing after.\n");
                return(NULL);
            }
            if(cursor->next->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number after.\n");
                return(NULL);
            }

            if(cursor->op == CRUSTY_EXPR_LSHIFT) {
                cursor->prev->number = cursor->prev->number << cursor->next->number;
            } else {
                cursor->prev->number = cursor->prev->number >> cursor->next->number;
            }

            /* the operator and second operand are unimportant now so move the
               context to the result value and repoint it to the value following
               the second operand */
            cursor = cursor->prev;
            cursor->next = cursor->next->next->next;
            if(cursor->next != NULL) {
                cursor->next->prev = cursor;
            }
        }
        cursor = cursor->next;
    }

    /* less than (or equal) and greater than (or equal) */
    cursor = expr;
    while(cursor != NULL) {
        if(cursor->op == CRUSTY_EXPR_LESS ||
           cursor->op == CRUSTY_EXPR_LEQUALS ||
           cursor->op == CRUSTY_EXPR_GREATER ||
           cursor->op == CRUSTY_EXPR_GEQUALS) {
            if(cursor->prev == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing before.\n");
                return(NULL);
            }
            if(cursor->prev->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number before.\n");
                return(NULL);
            }
            if(cursor->next == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing after.\n");
                return(NULL);
            }
            if(cursor->next->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number after.\n");
                return(NULL);
            }

            if(cursor->op == CRUSTY_EXPR_LESS) {
                cursor->prev->number = (cursor->prev->number < cursor->next->number);
            } else if(cursor->op == CRUSTY_EXPR_LEQUALS) {
                cursor->prev->number = (cursor->prev->number <= cursor->next->number);
            } else if(cursor->op == CRUSTY_EXPR_GREATER) {
                cursor->prev->number = (cursor->prev->number > cursor->next->number);
            } else {
                cursor->prev->number = (cursor->prev->number >= cursor->next->number);
            }

            /* the operator and second operand are unimportant now so move the
               context to the result value and repoint it to the value following
               the second operand */
            cursor = cursor->prev;
            cursor->next = cursor->next->next->next;
            if(cursor->next != NULL) {
                cursor->next->prev = cursor;
            }
        }
        cursor = cursor->next;
    }

    /* equal to and not equal to */
    cursor = expr;
    while(cursor != NULL) {
        if(cursor->op == CRUSTY_EXPR_EQUALS ||
           cursor->op == CRUSTY_EXPR_NEQUALS) {
            if(cursor->prev == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing before.\n");
                return(NULL);
            }
            if(cursor->prev->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number before.\n");
                return(NULL);
            }
            if(cursor->next == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing after.\n");
                return(NULL);
            }
            if(cursor->next->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number after.\n");
                return(NULL);
            }

            if(cursor->op == CRUSTY_EXPR_EQUALS) {
                cursor->prev->number = (cursor->prev->number == cursor->next->number);
            } else {
                cursor->prev->number = (cursor->prev->number != cursor->next->number);
            }

            /* the operator and second operand are unimportant now so move the
               context to the result value and repoint it to the value following
               the second operand */
            cursor = cursor->prev;
            cursor->next = cursor->next->next->next;
            if(cursor->next != NULL) {
                cursor->next->prev = cursor;
            }
        }
        cursor = cursor->next;
    }

    /* and and nand */
    cursor = expr;
    while(cursor != NULL) {
        if(cursor->op == CRUSTY_EXPR_AND ||
           cursor->op == CRUSTY_EXPR_NAND) {
            if(cursor->prev == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing before.\n");
                return(NULL);
            }
            if(cursor->prev->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number before.\n");
                return(NULL);
            }
            if(cursor->next == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing after.\n");
                return(NULL);
            }
            if(cursor->next->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number after.\n");
                return(NULL);
            }

            if(cursor->op == CRUSTY_EXPR_AND) {
                cursor->prev->number = cursor->prev->number & cursor->next->number;
            } else {
                cursor->prev->number = ~(cursor->prev->number & cursor->next->number);
            }

            /* the operator and second operand are unimportant now so move the
               context to the result value and repoint it to the value following
               the second operand */
            cursor = cursor->prev;
            cursor->next = cursor->next->next->next;
            if(cursor->next != NULL) {
                cursor->next->prev = cursor;
            }
        }
        cursor = cursor->next;
    }

    /* or and nor */
    cursor = expr;
    while(cursor != NULL) {
        if(cursor->op == CRUSTY_EXPR_OR ||
           cursor->op == CRUSTY_EXPR_NOR) {
            if(cursor->prev == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing before.\n");
                return(NULL);
            }
            if(cursor->prev->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number before.\n");
                return(NULL);
            }
            if(cursor->next == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing after.\n");
                return(NULL);
            }
            if(cursor->next->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number after.\n");
                return(NULL);
            }

            if(cursor->op == CRUSTY_EXPR_OR) {
                cursor->prev->number = cursor->prev->number | cursor->next->number;
            } else {
                cursor->prev->number = ~(cursor->prev->number | cursor->next->number);
            }

            /* the operator and second operand are unimportant now so move the
               context to the result value and repoint it to the value following
               the second operand */
            cursor = cursor->prev;
            cursor->next = cursor->next->next->next;
            if(cursor->next != NULL) {
                cursor->next->prev = cursor;
            }
        }
        cursor = cursor->next;
    }

    /* xor and xnor */
    cursor = expr;
    while(cursor != NULL) {
        if(cursor->op == CRUSTY_EXPR_XOR ||
           cursor->op == CRUSTY_EXPR_XNOR) {
            if(cursor->prev == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing before.\n");
                return(NULL);
            }
            if(cursor->prev->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number before.\n");
                return(NULL);
            }
            if(cursor->next == NULL) {
                LOG_PRINTF_LINE(cvm, "Operator with nothing after.\n");
                return(NULL);
            }
            if(cursor->next->op != CRUSTY_EXPR_NUMBER) {
                LOG_PRINTF_LINE(cvm, "Operator with not a number after.\n");
                return(NULL);
            }

            if(cursor->op == CRUSTY_EXPR_XOR) {
                cursor->prev->number = cursor->prev->number ^ cursor->next->number;
            } else {
                cursor->prev->number = ~(cursor->prev->number ^ cursor->next->number);
            }

            /* the operator and second operand are unimportant now so move the
               context to the result value and repoint it to the value following
               the second operand */
            cursor = cursor->prev;
            cursor->next = cursor->next->next->next;
            if(cursor->next != NULL) {
                cursor->next->prev = cursor;
            }
        }
        cursor = cursor->next;
    }

    /* at this point, everything should be collapsed in to one lone value */
    if(expr->prev != NULL || expr->next != NULL) {
        LOG_PRINTF_LINE(cvm, "Expression didn't evaluate down to a single number.\n");
        return(NULL);
    }

    return(expr);
}

static CrustyExpr *add_expr(CrustyVM *cvm,
                            CrustyExprOp op,
                            int number,
                            CrustyExpr *buffer,
                            int *len,
                            CrustyExpr **new) {
    CrustyExpr *expr;
    expr = realloc(buffer, sizeof(CrustyExpr) * (*len + 1));
    if(expr == NULL) {
        LOG_PRINTF_LINE(cvm, "Failed to allocate memory for CrustyExpr.\n");
        return(NULL);
    }

    *new = &(expr[*len]);
    (*new)->op = op;
    (*new)->number = number;
    (*len)++;

    return(expr);
}

#define ISJUNK(X) ((X) == ' ' || (X) == '\t')

/* this is all awful and probably a horrible, inefficient way to do this but I don't know a better way */
static long evaluate_expr(CrustyVM *cvm,
                          const char *expression,
                          unsigned int exprlen) {
    CrustyExpr *expr = NULL;
    int exprmem = 0;
    CrustyExpr *temp;
    CrustyExpr *new = NULL;
    int parens = 0;
    int valsize;

    char *end;
    long num;

    int i;
    long tokenstart;

    for(i = 0; i < exprlen; i++) {
        if(ISJUNK(expression[i])) {
            continue;
        } else if(expression[i] == '(') {
            temp = add_expr(cvm, CRUSTY_EXPR_LPAREN, 0, expr, &exprmem, &new);
            if(temp == NULL) {
                goto error;
            }
            expr = temp;
            parens++;
        } else if(expression[i] == ')') {
            temp = add_expr(cvm, CRUSTY_EXPR_RPAREN, 0, expr, &exprmem, &new);
            if(temp == NULL) {
                goto error;
            }
            expr = temp;
            parens--;
        } else if(expression[i] == '+' &&
                  new != NULL &&
                  (new->op == CRUSTY_EXPR_NUMBER ||
                   new->op == CRUSTY_EXPR_RPAREN)) {
            /* Only assume we want to add a + or - if we're following a
               point where it'd be clearly valid to do so, otherwise one
               wouldn't be able to for example add or subtract a negative
               number, because the evaluation of something like 2 - -2 would
               become 2 subtract subtract 2 which can't work.  This will
               allow it to be 2 subtract -2 because this'll fall through and
               the -2 will be evaluated by strtol. */
            temp = add_expr(cvm, CRUSTY_EXPR_PLUS, 0, expr, &exprmem, &new);
            if(temp == NULL) {
                goto error;
            }
            expr = temp;
        } else if(expression[i] == '-' &&
                  new != NULL &&
                  (new->op == CRUSTY_EXPR_NUMBER ||
                   new->op == CRUSTY_EXPR_RPAREN)) {
            /* see above */
            temp = add_expr(cvm, CRUSTY_EXPR_MINUS, 0, expr, &exprmem, &new);
            if(temp == NULL) {
                goto error;
            }
            expr = temp;
        } else if(expression[i] == '*') {
            temp = add_expr(cvm, CRUSTY_EXPR_MULTIPLY, 0, expr, &exprmem, &new);
            if(temp == NULL) {
                goto error;
            }
            expr = temp;
        } else if(expression[i] == '/') {
            temp = add_expr(cvm, CRUSTY_EXPR_DIVIDE, 0, expr, &exprmem, &new);
            if(temp == NULL) {
                goto error;
            }
            expr = temp;
        } else if(expression[i] == '%') {
            temp = add_expr(cvm, CRUSTY_EXPR_MODULO, 0, expr, &exprmem, &new);
            if(temp == NULL) {
                goto error;
            }
            expr = temp;
        } else if(expression[i] == '=') {
            if(i + 1 < exprlen) {
                if(expression[i + 1] == '=') {
                    temp = add_expr(cvm, CRUSTY_EXPR_EQUALS, 0, expr, &exprmem, &new);
                    if(temp == NULL) {
                        goto error;
                    }
                    expr = temp;
                    i++;
                } else {
                    LOG_PRINTF_LINE(cvm, "Invalid operator: =%c\n", expression[i + 1]);
                    goto error;
                }
            } else {
                LOG_PRINTF_LINE(cvm, "Invalid operator: =\n");
                goto error;
            }
        } else if(expression[i] == '<') {
            if(i + 1 < exprlen) {
                if(expression[i + 1] == '=') {
                    temp = add_expr(cvm, CRUSTY_EXPR_LEQUALS, 0, expr, &exprmem, &new);
                    if(temp == NULL) {
                        goto error;
                    }
                    expr = temp;
                    i++;
                } else if(expression[i + 1] == '<') {
                    temp = add_expr(cvm, CRUSTY_EXPR_LSHIFT, 0, expr, &exprmem, &new);
                    if(temp == NULL) {
                        goto error;
                    }
                    expr = temp;
                    i++;
                } else if(ISJUNK(expression[i + 1])) {
                    temp = add_expr(cvm, CRUSTY_EXPR_LESS, 0, expr, &exprmem, &new);
                    if(temp == NULL) {
                        goto error;
                    }
                    expr = temp;
                } else {
                    LOG_PRINTF_LINE(cvm, "Invalid operator: <%c\n", expression[i + 1]);
                    goto error;
                }
            } else {
                LOG_PRINTF_LINE(cvm, "Operator at end of expression: <\n");
                goto error;
            }            
        } else if(expression[i] == '>') {
            if(i + 1 < exprlen) {
                if(expression[i + 1] == '=') {
                    temp = add_expr(cvm, CRUSTY_EXPR_GEQUALS, 0, expr, &exprmem, &new);
                    if(temp == NULL) {
                        goto error;
                    }
                    expr = temp;
                    i++;
                } else if(expression[i + 1] == '>') {
                    temp = add_expr(cvm, CRUSTY_EXPR_RSHIFT, 0, expr, &exprmem, &new);
                    if(temp == NULL) {
                        goto error;
                    }
                    expr = temp;
                    i++;
                } else if(ISJUNK(expression[i + 1])) {
                    temp = add_expr(cvm, CRUSTY_EXPR_GREATER, 0, expr, &exprmem, &new);
                    if(temp == NULL) {
                        goto error;
                    }
                    expr = temp;
                } else {
                    LOG_PRINTF_LINE(cvm, "Invalid operator: >%c\n", expression[i + 1]);
                    goto error;
                }
            } else {
                LOG_PRINTF_LINE(cvm, "Operator at end of expression: >\n");
                goto error;
            }            
        } else if(expression[i] == '!') {
            if(i + 1 < exprlen) {
                if(expression[i + 1] == '=') {
                    temp = add_expr(cvm, CRUSTY_EXPR_NEQUALS, 0, expr, &exprmem, &new);
                    if(temp == NULL) {
                        goto error;
                    }
                    expr = temp;
                    i++;
                } else if(expression[i + 1] == '&') {
                    temp = add_expr(cvm, CRUSTY_EXPR_NAND, 0, expr, &exprmem, &new);
                    if(temp == NULL) {
                        goto error;
                    }
                    expr = temp;
                    i++;
                } else if(expression[i + 1] == '|') {
                    temp = add_expr(cvm, CRUSTY_EXPR_NOR, 0, expr, &exprmem, &new);
                    if(temp == NULL) {
                        goto error;
                    }
                    expr = temp;
                    i++;
                } else if(expression[i + 1] == '^') {
                    temp = add_expr(cvm, CRUSTY_EXPR_XNOR, 0, expr, &exprmem, &new);
                    if(temp == NULL) {
                        goto error;
                    }
                    expr = temp;
                    i++;
                } else if(ISJUNK(expression[i + 1])) {
                    LOG_PRINTF_LINE(cvm, "Invalid operator: !\n");
                    goto error;
                } else {
                    LOG_PRINTF_LINE(cvm, "Invalid operator: !%c\n", expression[i + 1]);
                    goto error;
                }
            } else {
                LOG_PRINTF_LINE(cvm, "Invalid operator: !\n");
                goto error;
            }            
        } else if(expression[i] == '&') {
            temp = add_expr(cvm, CRUSTY_EXPR_AND, 0, expr, &exprmem, &new);
            if(temp == NULL) {
                goto error;
            }
            expr = temp;
        } else if(expression[i] == '|') {
            temp = add_expr(cvm, CRUSTY_EXPR_OR, 0, expr, &exprmem, &new);
            if(temp == NULL) {
                goto error;
            }
            expr = temp;
        } else if(expression[i] == '^') {
            temp = add_expr(cvm, CRUSTY_EXPR_XOR, 0, expr, &exprmem, &new);
            if(temp == NULL) {
                goto error;
            }
            expr = temp;
        } else {
            num = strtol(&(expression[i]), &end, 0);
            if(&(expression[i]) != end) {
                temp = add_expr(cvm, CRUSTY_EXPR_NUMBER, num, expr, &exprmem, &new);
                if(temp == NULL) {
                    goto error;
                }
                expr = temp;

                i += (int)(end - &(expression[i]) - 1);
            } else {
                /* insert a 0 for an undefined variable or whatever the user
                   might have put in that can't be interpreted as anything. This
                   will make user errors harder to find but whichever. */
                temp = add_expr(cvm, CRUSTY_EXPR_NUMBER, 0, expr, &exprmem, &new);

                if(temp == NULL) {
                    goto error;
                }
                expr = temp;
                /* find the next "junk" char */
                while(i < exprlen) {
                    if(ISJUNK(expression[i])) {
                        break;
                    }
                    i++;
                }
            }
        }
    }

    if(parens != 0) {
        LOG_PRINTF_LINE(cvm, "Unmatched parentheses.\n");
        goto error;
    }

    if(exprmem == 0) {
        LOG_PRINTF_LINE(cvm, "No expression tokens found.\n");
        goto error;
    }

    /* point everything */
    expr[0].prev = NULL;
    if(exprmem > 1) {
        expr[0].next = &(expr[1]);
        for(i = 1; i < exprmem - 1; i++) {
            expr[i].prev = &(expr[i - 1]);
            expr[i].next = &(expr[i + 1]);
        }
        expr[exprmem - 1].next = NULL;
        expr[exprmem - 1].prev = &(expr[exprmem - 2]);
    } else {
        expr[0].next = NULL;
    }

    /* pass in the expression, any errors will result in NULL being returned */
    temp = do_expression(cvm, expr);
    if(temp == NULL) {
        goto error;
    }
    /* temp should now be a lone CRUSTY_EXPR_NUMBER */

    /* create the string containing the evaluated value */
    valsize = snprintf(NULL, 0, "%d", temp->number);
    /* returned buffer is already null terminated and large enough to fit valsize */
    tokenstart = add_token(cvm, NULL, valsize, 0, NULL);
    if(tokenstart < 0) {
        LOG_PRINTF_LINE(cvm, "Failed to allocate memory for expression value string.\n");
        goto error;
    }
    if(snprintf(TOKENVAL(tokenstart),
                valsize + 1,
                "%d",
                temp->number) < 0) {
        LOG_PRINTF_LINE(cvm, "Failed to write expression value in to string.\n");
        goto error;
    }
    free(expr);

    return(tokenstart);
error:
    if(expr != NULL) {
        free(expr);
    }

    return(-1);
}

#undef ISJUNK

#define INSTRUCTION_COUNT (26)

static int valid_instruction(const char *name) {
    int i;

    const char *INSTRUCTION_LIST[] = {
        "stack",
        "proc",
        "export",
        "ret",
        "label",
        "static",
        "local",
        "move",
        "add",
        "sub",
        "mul",
        "div",
        "mod",
        "and",
        "or",
        "xor",
        "shl",
        "shr",
        "cmp",
        "call",
        "jump",
        "jumpn",
        "jumpz",
        "jumpl",
        "jumpg",
        "binclude"
    };  

    for(i = 0; i < INSTRUCTION_COUNT; i++) {
        if(strcmp(name, INSTRUCTION_LIST[i]) == 0) {
            return(1);
        }
    }

    return(0);
}

#undef INSTRUCTION_COUNT

#define GET_ACTIVE(TOKEN) TOKENVAL(active.offset[TOKEN])

static int preprocess(CrustyVM *cvm,
                      const unsigned long *inVar,
                      const unsigned long *inValue,
                      unsigned int inVars) {
    unsigned int i, j;
    CrustyLine *new = NULL;
    unsigned int mem;
    unsigned int lines;
    char *temp;

    CrustyLine active;
    active.offset = NULL;

    CrustyMacro *macro = NULL;
    unsigned int macrocount = 0;
    CrustyMacro *curmacro = NULL;

    unsigned int returnstack[MACRO_STACK_SIZE];
    CrustyMacro *macrostack[MACRO_STACK_SIZE];
    long *macroargs[MACRO_STACK_SIZE];
    int macrostackptr = -1;

    long *vars = NULL;
    long *values = NULL;
    unsigned int varcount = 0;

    int foundmacro = 0;

    long tokenstart;

    mem = 0; /* actual memory allocated for line */
    lines = 0; /* size of initialized array */

    cvm->logline = 0; /* line being evaluated */
    while(cvm->logline < cvm->lines) {
        /* no need to check if tokencount > 0 because those lines were filtered
           out previously */

        if(active.offset != NULL) {
            free(active.offset);
        }
        active.offset = malloc(sizeof(long) * cvm->line[cvm->logline].tokencount);
        if(active.offset == NULL) {
            LOG_PRINTF_LINE(cvm, "Failed to allocate memory for active token arguments.");
            goto failure;
        }

#ifdef CRUSTY_TEST
        LOG_PRINTF_LINE(cvm, " Original: ");
        if(macrostackptr >= 0) {
            LOG_PRINTF_BARE(cvm, "%s ", TOKENVAL(macrostack[macrostackptr]->nameOffset);
        }
        for(i = 0; i < cvm->line[cvm->logline].tokencount; i++) {
            LOG_PRINTF_BARE(cvm, "%s ", TOKENVAL(cvm->line[cvm->logline].offset[i]);
        }
        LOG_PRINTF_BARE(cvm, "\n");
#endif

        /* make mutable active line */
        active.tokencount = cvm->line[cvm->logline].tokencount;
        active.moduleOffset = cvm->line[cvm->logline].moduleOffset;
        active.line = cvm->line[cvm->logline].line;

        /* replace any tokens with tokens containing any possible macro
           replacement values */
        for(i = 0; i < active.tokencount; i++) {
            active.offset[i] = cvm->line[cvm->logline].offset[i];
            /* don't rewrite the line at all if it's ending the current
             * macro. */
            if(!(macrostackptr >= 0 && i == 1 &&
                 compare_token_and_string(cvm,
                                          active.offset[0],
                                          "endmacro") == 0 &&
                 compare_token_and_token(cvm,
                                         active.offset[1],
                                         macrostack[macrostackptr]->nameOffset) == 0)) {
                for(j = 0; j < inVars; j++) {
                    /* first part of a hack to prevent a -D parameter
                     * on the command line becoming "undefined".
                     * Also avoid rewriting the macro name of the current
                     * macro. */
                    if(i == 1 &&
                       compare_token_and_string(cvm,
                                                active.offset[0],
                                                "if") == 0 &&
                       compare_token_and_token(cvm,
                                               active.offset[1],
                                               inVar[j]) == 0) {
                        continue;
                    }
                    tokenstart = string_replace(cvm,
                                                active.offset[i],
                                                inVar[j],
                                                inValue[j]);
                    if(tokenstart < 0) {
                        /* reason will have already been printed */
                        goto failure;
                    }
                    active.offset[i] = tokenstart;
                }
                if((macrostackptr >= 0 && macrostack[macrostackptr]->argcount > 0)) {
                    for(j = 0; j < macrostack[macrostackptr]->argcount; j++) {
                        /* function will just pass back the token passed to
                           it in the case there's nothing to be done,
                           otherwise it'll create the new string in extramem
                           and update the length and return it. */
                        tokenstart =
                            string_replace(cvm,
                                           active.offset[i],
                                           macrostack[macrostackptr]->argOffset[j],
                                           macroargs[macrostackptr][j]);
                        if(tokenstart < 0) {
                            /* reason will have already been printed */
                            goto failure;
                        }
                        active.offset[i] = tokenstart;
                    }
                }
                for(j = 0; j < varcount; j++) {
                    tokenstart = string_replace(cvm, active.offset[i], vars[j], values[j]);
                    if(tokenstart < 0) {
                        /* reason will have already been printed */
                        goto failure;
                    }
                    active.offset[i] = tokenstart;
                }
            }
        }

#ifdef CRUSTY_TEST
        LOG_PRINTF_LINE(cvm, "Rewritten: ");
        for(i = 0; i < active.tokencount; i++) {
            LOG_PRINTF_BARE(cvm, "%s ", GET_ACTIVE(i));
        }
        LOG_PRINTF_BARE(cvm, "\n");
#endif

        if(compare_token_and_string(cvm,
                                    active.offset[0],
                                    "macro") == 0) {
            if(curmacro == NULL) { /* don't evaluate any macros which may be
                                      within other macros. */
                if(active.tokencount < 2) {
                    LOG_PRINTF_LINE(cvm, "Macros must at least be defined with a name.\n");
                    goto failure;
                }

                /* if the macro wasn't found, allocate space for it, otherwise
                   override previous declaration */
                curmacro = find_macro(cvm, macro, macrocount, GET_ACTIVE(1));
                if(curmacro == NULL) {
                    curmacro = realloc(macro, sizeof(CrustyMacro) * (macrocount + 1));
                    if(curmacro == NULL) {
                        LOG_PRINTF_LINE(cvm, "Failed to allocate memory for macro.\n");
                        goto failure;
                    }
                    macro = curmacro;
                    curmacro = &(macro[macrocount]);
                    macrocount++;
                }
                curmacro->nameOffset = active.offset[1];
                curmacro->argcount = active.tokencount - 2;
                curmacro->argOffset = malloc(sizeof(long) * curmacro->argcount);
                if(curmacro->argOffset == NULL) {
                    LOG_PRINTF_LINE(cvm, "Failed to allocate memory for macro args list.\n");
                    free(curmacro);
                    goto failure;
                }
                for(i = 2; i < active.tokencount; i++) {
                    curmacro->argOffset[i - 2] = active.offset[i];
                }
                curmacro->start = cvm->logline + 1; /* may not be defined now but a
                                                  valid program will have it
                                                  defined eventually as a macro
                                                  at least needs a matching
                                                  endmacro */

                /* suppress copying evaluated macro in to destination */
                goto skip_copy;
            } else {
                foundmacro = 1;
            }
        } else if(compare_token_and_string(cvm,
                                           active.offset[0],
                                           "endmacro") == 0) {
            if(active.tokencount != 2) {
                LOG_PRINTF_LINE(cvm, "endmacro takes a name.\n");
                goto failure;
            }

            /* if a macro is being read in and the end of that macro has
               been reached, another macro can start being read in again */
            if(curmacro != NULL &&
               compare_token_and_token(cvm,
                                       active.offset[1],
                                       curmacro->nameOffset) == 0) {
                curmacro = NULL;

                /* suppress copying evaluated endmacro in to destination */
                goto skip_copy;
            }

            /* if a macro is being output and the end of the macro currently
               being output is reached, then pop it off the stack. */
            if(macrostackptr >= 0 &&
               compare_token_and_token(cvm,
                                       macrostack[macrostackptr]->nameOffset,
                                       active.offset[1]) == 0) {
                free(macroargs[macrostackptr]);
                cvm->logline = returnstack[macrostackptr];
                macrostackptr--;

                /* suppress copying evaluated endmacro in to destination */
                goto skip_copy;
            }
        } else if(compare_token_and_string(cvm,
                                           active.offset[0],
                                           "if") == 0) {
            /* don't evaluate macro calls while reading in a macro, only
               while writing out */
            if(curmacro == NULL) {
                /* at this point, a defined variable will already have
                   replaced the first argument so we just need to determine
                   whather it's a number and whether it's not 0 */
                if(active.tokencount < 3) {
                    LOG_PRINTF_LINE(cvm,
                        "if takes a variable and at least 1 more argument.\n");
                    goto failure;
                }

                int dothing = 0;
                /* second part of hack to check to see if a
                 * condition variable was defined on the command
                 * line, regardless of it being 0 */
                for(j = 0; j < inVars; j++) {
                    if(compare_token_and_token(cvm,
                                               active.offset[1],
                                               inVar[j]) == 0) {
                        dothing = 1;
                        break;
                    }
                }
                if(!dothing) {
                    char *endchar;
                    int num;
                    num = strtol(GET_ACTIVE(1), &endchar, 0);
                    /* check that the entire string was valid and that the
                       result was not zero */
                    if(GET_ACTIVE(1)[0] != '\0' &&
                       endchar - GET_ACTIVE(1) == TOKENLEN(active.offset[1]) &&
                       num != 0) {
                        dothing = 1;
                    }
                }
                if(dothing) {
                    /* move everything over 2 */
                    for(j = 2; j < active.tokencount; j++) {
                        cvm->line[cvm->logline].offset[j - 2] =
                            cvm->line[cvm->logline].offset[j];
                    }
                    cvm->line[cvm->logline].tokencount -= 2;

                    continue; /* don't copy but reevaluate */
                }

                goto skip_copy; /* don't copy and don't reevaluate */
            }
        } else if(compare_token_and_string(cvm,
                                           active.offset[0],
                                           "expr") == 0) {
           if(curmacro == NULL) { /* don't evaluate any macros which may be
                                      within other macros. */
               if(active.tokencount != 3) {
                   LOG_PRINTF_LINE(cvm,
                       "expr takes a variable name and an expression.\n");
                   goto failure;
               }

               temp = realloc(vars, sizeof(long) * (varcount + 1));
               if(temp == NULL) {
                   LOG_PRINTF_LINE(cvm, "Failed to allocate memory for expr var.\n");
                   goto failure;
               }
               vars = (long *)temp;

               temp = realloc(values, sizeof(long) * (varcount + 1));
               if(temp == NULL) {
                   LOG_PRINTF_LINE(cvm, "Failed to allocate memory for expr value.\n");
                   goto failure;
               }
               values = (long *)temp;

               vars[varcount] = active.offset[1];
               values[varcount] = evaluate_expr(cvm,
                                                TOKENVAL(active.offset[2]),
                                                TOKENLEN(active.offset[2]));
               if(values[varcount] < 0) {
                   LOG_PRINTF_LINE(cvm, "Expression evaluation failed.\n");
                   goto failure;
               }
               varcount++;

               goto skip_copy;
           } else {
               foundmacro = 1;
           }
        } else if(!valid_instruction(GET_ACTIVE(0))) {
            /* don't evaluate macro calls while reading in a macro, only
               while writing out */
            if(curmacro == NULL) {
                if(macrostackptr == MACRO_STACK_SIZE - 1) {
                    LOG_PRINTF_LINE(cvm, "Macro stack filled.\n");
                }

                macrostack[macrostackptr + 1] = find_macro(cvm,
                                                           macro,
                                                           macrocount,
                                                           GET_ACTIVE(0));
                if(macrostack[macrostackptr + 1] == NULL) {
                    LOG_PRINTF_LINE(cvm, "Invalid keyword or macro not found: %s.\n",
                                        GET_ACTIVE(0));
                    goto failure;
                }

                if(macrostack[macrostackptr + 1] == curmacro) {
                    LOG_PRINTF_LINE(cvm, "Macro called recursively: %s.\n",
                                        TOKENVAL(curmacro->nameOffset));
                    goto failure;
                }
                if(active.tokencount - 1 !=
                   macrostack[macrostackptr + 1]->argcount) {
                    LOG_PRINTF_LINE(cvm, "Wrong number of arguments to macro: "
                                         "got %d, expected %d.\n",
                               active.tokencount - 1,
                               macrostack[macrostackptr + 1]->argcount);
                    goto failure;
                }

                macrostackptr++;
                macroargs[macrostackptr] =
                    malloc(sizeof(long) * macrostack[macrostackptr]->argcount);
                if(macroargs[macrostackptr] == NULL) {
                    LOG_PRINTF_LINE(cvm, "Failed to allocate memory for macro args.\n");
                    goto failure;
                }
                for(i = 0; i < macrostack[macrostackptr]->argcount; i++) {
                    macroargs[macrostackptr][i] = active.offset[i + 1];
                }
                returnstack[macrostackptr] = cvm->logline;
                cvm->logline = macrostack[macrostackptr]->start;

                /* don't copy the next line but make sure it's still evaluated */
                continue;
            } else {
                foundmacro = 1;
            }
        }

        /* don't actually output a macro being read in */
        if(curmacro == NULL) {
            if(lines == mem) {
                temp = realloc(new, sizeof(CrustyLine) * (mem + 1));
                if(temp == NULL) {
                    LOG_PRINTF_LINE(cvm, "Failed to allocate memory for line copy.\n");
                    goto failure;
                }
                new = (CrustyLine *)temp;
                mem++;
            }

            new[lines].tokencount = active.tokencount;
            new[lines].moduleOffset = active.moduleOffset;
            new[lines].line = active.line;
            new[lines].offset = malloc(sizeof(long) * new[lines].tokencount);
            if(new[lines].offset == NULL) {
                LOG_PRINTF_LINE(cvm, "Failed to allocate memory for line offsets copy.\n");
                goto failure;
            }
            for(i = 0; i < new[lines].tokencount; i++) {
                new[lines].offset[i] = active.offset[i];
            }

            lines++;
        }

skip_copy:
        cvm->logline++;
    }

    if(curmacro != NULL) {
        LOG_PRINTF(cvm, "Macro without endmacro: %s@%s:%u.\n",
                        TOKENVAL(curmacro->nameOffset),
                        TOKENVAL(cvm->line[curmacro->start].moduleOffset),
                        cvm->line[curmacro->start].line);
        goto failure;
    }

    for(i = 0; i < cvm->lines; i++) {
        free(cvm->line[i].offset);
    }
    free(cvm->line);
    cvm->line = new;
    cvm->lines = lines;

    if(macro != NULL) {
        for(i = 0; i < macrocount; i++) {
            if(macro[i].argOffset != NULL) {
                free(macro[i].argOffset);
            }
        }
        free(macro);
    }

    if(active.offset != NULL) {
        free(active.offset);
    }

    if(vars != NULL) {
        free(vars);
    }
    if(values != NULL) {
        free(values);
    }

    return(foundmacro);

failure:
    if(new != NULL) {
        for(i = 0; i < lines; i++) {
            if(new[i].offset != NULL) {
                free(new[i].offset);
            }
        }
        free(new);
    }

    if(macro != NULL) {
        for(i = 0; i < macrocount; i++) {
            if(macro[i].argOffset != NULL) {
                free(macro[i].argOffset);
            }
        }
        free(macro);
    }

    if(active.offset != NULL) {
        free(active.offset);
    }

    if(vars != NULL) {
        free(vars);
    }
    if(values != NULL) {
        free(values);
    }

    return(-1);
}

#undef GET_ACTIVE

static int find_procedure(CrustyVM *cvm,
                          const char *name) {
    unsigned int i;

    for(i = 0; i < cvm->procs; i++) {
        if(compare_token_and_string(cvm,
                                    cvm->proc[i].nameOffset,
                                    name) == 0) {
            return(i);
        }
    }

    return(-1);
}

static int variable_is_global(CrustyVariable *var) {
    return(var->procIndex == -1);
}

static int variable_is_argument(CrustyVariable *var) {
    return(var->length == 0);
}

static int variable_is_callback(CrustyVariable *var) {
    return(var->read != NULL || var->write != NULL);
}

/* this function is used while proc->var and var->proc are invalid and the only
   associations between variables and their procedures is a list of indexes in
   to the variable list within each procedure */
static int find_variable(CrustyVM *cvm,
                         CrustyProcedure *proc,
                         const char *name) {
    unsigned int i;

    if(proc != NULL) {
        /* scan local */
        for(i = 0; i < proc->vars; i++) {
            if(compare_token_and_string(cvm,
                                        cvm->var[proc->varIndex[i]].nameOffset,
                                        name) == 0) {
                return(proc->varIndex[i]);
            }
        }
    }
    /* scan global */

    for(i = 0; i < cvm->vars; i++) {
        if(variable_is_global(&(cvm->var[i]))) {
            if(compare_token_and_string(cvm,
                                        cvm->var[i].nameOffset,
                                        name) == 0) {
                return(i);
            }
        }
    }

    return(-1);
}

static int new_variable(CrustyVM *cvm,
                        long nameOffset,
                        CrustyType type,
                        unsigned int length,
                        void *initializer,
                        const CrustyCallback *cb,
                        int procIndex) {
    int varIndex;
    unsigned char *temp;
    CrustyVariable *var;
    CrustyProcedure *proc = NULL;

    if(procIndex >= 0) {
        proc = &(cvm->proc[procIndex]);
    }

    varIndex = find_variable(cvm, proc, TOKENVAL(nameOffset));
    if(varIndex >= 0) {
        if(cb != NULL) {
            LOG_PRINTF(cvm, "Redeclaration of callback variable: %s\n",
                       TOKENVAL(cvm->var[varIndex].nameOffset));
        } else if(cvm->var[varIndex].procIndex == -1) {
            LOG_PRINTF(cvm, "Redeclaration of static variable: %s\n",
                       TOKENVAL(cvm->var[varIndex].nameOffset));
        } else {
            LOG_PRINTF(cvm, "Redeclaration of local variable: %s\n",
                       TOKENVAL(cvm->var[varIndex].nameOffset));
        }
        return(-1);
    }

    temp = realloc(cvm->var, sizeof(CrustyVariable) * (cvm->vars + 1));
    if(temp == NULL) {
        LOG_PRINTF(cvm, "Failed to allocate memory for variable.\n");
        return(-1);
    }
    cvm->var = (CrustyVariable *)temp;
    var = &(cvm->var[cvm->vars]);

    var->nameOffset = nameOffset;
    var->length = length;
    var->type = type;
    var->procIndex = procIndex;

    /* local */
    if(proc != NULL) {
        var->read = NULL;
        var->write = NULL;

        temp = realloc(proc->varIndex, sizeof(int) * (proc->vars + 1));
        if(temp == NULL) {
            LOG_PRINTF(cvm, "Failed to allocate memory for local variable list.\n");
            return(-1);
        }
        proc->varIndex = (int *)temp;
        proc->varIndex[proc->vars] = cvm->vars;
        proc->vars++;

        /* local variable locations are updated first, then applied because
         * the stack grows down for some reason, so the space needs to be
         * "taken up" first for their location */
        if(length == 0) {
            proc->args++;
            var->offset = proc->args;
            proc->stackneeded += sizeof(CrustyStackArg);

            temp = realloc(proc->initializer, proc->stackneeded);
            if(temp == NULL) {
                LOG_PRINTF(cvm, "Failed to expand procedure initializer.\n");
                return(-1);
            }
            proc->initializer = temp;
 
            /* no need to initialize anything here, because these fields are
             * filled in at runtime. */
        } else if(type == CRUSTY_TYPE_INT) {
            int lastSize = proc->stackneeded;

            proc->stackneeded += (length * sizeof(int));
            var->offset = proc->stackneeded;
            /* make space for the new local variable and because i'm dumb and
             * again, made the stack grow weird because i dunno, the data has
             * to be moved towards the end of the stack. */
            temp = realloc(proc->initializer, proc->stackneeded);
            if(temp == NULL) {
                LOG_PRINTF(cvm, "Failed to expand procedure initializer.\n");
                return(-1);
            }
            proc->initializer = temp;
            /* use memmove() because the locations will overlap */
            memmove(&(proc->initializer[length * sizeof(int)]),
                    proc->initializer,
                    lastSize);
            memcpy(proc->initializer, initializer, length * sizeof(int));
        } else if(type == CRUSTY_TYPE_FLOAT) {
            int lastSize = proc->stackneeded;

            proc->stackneeded += (length * sizeof(double));
            var->offset = proc->stackneeded;
            /* same as above */
            temp = realloc(proc->initializer, proc->stackneeded);
            if(temp == NULL) {
                LOG_PRINTF(cvm, "Failed to expand procedure initializer.\n");
                return(-1);
            }
            proc->initializer = temp;

            memmove(&(proc->initializer[length * sizeof(double)]),
                    proc->initializer,
                    lastSize);
            memcpy(proc->initializer, initializer, length * sizeof(double));
        } else { /* CHAR */
            int lastSize = proc->stackneeded;

            proc->stackneeded += length;
            FIND_ALIGNMENT_VALUE(proc->stackneeded)

            var->offset = proc->stackneeded;
            /* same as above */
            temp = realloc(proc->initializer, proc->stackneeded);
            if(temp == NULL) {
                LOG_PRINTF(cvm, "Failed to expand procedure initializer.\n");
                return(-1);
            }
            proc->initializer = temp;

            memmove(&(proc->initializer[var->offset - lastSize]),
                    proc->initializer,
                    lastSize);
            memcpy(proc->initializer, initializer, length);
        }
    } else { /* global */
        if(cb == NULL) {
            var->read = NULL;
            var->write = NULL;
 
            var->offset = cvm->initialstack;
            if(type == CRUSTY_TYPE_INT) {
                cvm->initialstack += (length * sizeof(int));
                /* the initial stack isn't as silly and grows upwards, so less
                 * special things to do. */
                temp = realloc(cvm->initializer, cvm->initialstack);
                if(temp == NULL) {
                    LOG_PRINTF(cvm, "Failed to expand procedure initializer.\n");
                    return(-1);
                }
                cvm->initializer = temp;

                memcpy(&(cvm->initializer[var->offset]),
                       initializer,
                       length * sizeof(int));
            } else if(type == CRUSTY_TYPE_FLOAT) {
                cvm->initialstack += (length * sizeof(double));

                temp = realloc(cvm->initializer, cvm->initialstack);
                if(temp == NULL) {
                    LOG_PRINTF(cvm, "Failed to expand procedure initializer.\n");
                    return(-1);
                }
                cvm->initializer = temp;

                memcpy(&(cvm->initializer[var->offset]),
                       initializer,
                       length * sizeof(double));
            } else { /* CHAR */
                cvm->initialstack += length;
                /* make things aligned */
                FIND_ALIGNMENT_VALUE(cvm->initialstack)

                temp = realloc(cvm->initializer, cvm->initialstack);
                if(temp == NULL) {
                    LOG_PRINTF(cvm, "Failed to expand procedure initializer.\n");
                    return(-1);
                }
                cvm->initializer = temp;

                memcpy(&(cvm->initializer[var->offset]),
                       initializer,
                       length);
            }
        } else {
            var->read = cb->read;
            var->write = cb->write;
            var->readpriv = cb->readpriv;
            var->writepriv = cb->writepriv;
        }
    }

    cvm->vars++;

    return(0);
}

#define ISJUNK(X) ((X) == ' ' || \
                   (X) == '\t')

static int number_list_ints(const char *list,
                            unsigned int len,
                            int **buffer) {
    int count = 0;
    int cur;
    unsigned int i;
    char *end;

    /* count numbers found in list */
    for(i = 0; i < len; i++) {
        if(!ISJUNK(list[i])) {
            strtol(&(list[i]), &end, 0);

            if(end == &(list[i]) ||
               !ISJUNK(*end)) {
                if(*end == '\0') {
                    /* number found at end of string, nothing more to do */
                    count++;
                    break;
                }
                /* separator ended with something not a number and number ended on
                   something not a separator */
                return(0);
            }

            /* more gross pointery stuff */
            i += (end - &(list[i]));
            count++;
        }
    }

    *buffer = malloc(sizeof(int) * count);
    if(*buffer == NULL) {
        return(-1);
    }

    /* scan for numbers a second time, but populate the array */
    cur = 0;
    for(i = 0; i < len; i++) {
        if(!ISJUNK(list[i])) {
            (*buffer)[cur] = strtol(&(list[i]), &end, 0);

            /* don't bother with sanity checks because they were already done */

            /* more gross pointery stuff */
            i += (end - &(list[i]));
            cur++;
        }
    }

    return(count);
}

static int number_list_floats(const char *list,
                              unsigned int len,
                              double **buffer) {
    int count = 0;
    int cur;
    unsigned int i;
    char *end;

    /* count numbers found in list */
    for(i = 0; i < len; i++) {
        if(!ISJUNK(list[i])) {
            strtod(&(list[i]), &end);

            if(end == &(list[i]) ||
               !ISJUNK(*end)) {
                if(*end == '\0') {
                    /* number found at end of string, nothing more to do */
                    count++;
                    break;
                }
                /* separator ended with something not a number and number ended on
                   something not a separator */
                return(0);
            }

            /* more gross pointery stuff */
            i += (end - &(list[i]));
            count++;
        }
    }

    *buffer = malloc(sizeof(double) * count);
    if(*buffer == NULL) {
        return(-1);
    }

    /* scan for numbers a second time, but populate the array */
    cur = 0;
    for(i = 0; i < len; i++) {
        if(!ISJUNK(list[i])) {
            (*buffer)[cur] = strtod(&(list[i]), &end);

            /* don't bother with sanity checks because they were already done */

            /* more gross pointery stuff */
            i += (end - &(list[i]));
            cur++;
        }
    }

    return(count);
}

#undef ISJUNK

static int variable_declaration(CrustyVM *cvm,
                                CrustyLine *line,
                                int procIndex) {
    char *end;
    int length;
    int *intinit = NULL;
    double *floatinit = NULL;
    void *initializer = NULL;
    CrustyType type;

    if(line->tokencount == 2) { /* no initializer, allocated to 0 */
        type = CRUSTY_TYPE_INT;
        length = 1;

        intinit = malloc(sizeof(int));
        if(intinit == NULL) {
            LOG_PRINTF_LINE(cvm, "Failed to allocate memory for initializer.\n");
            return(-1);
        }

        intinit[0] = 0;
        initializer = intinit;
    } else if(line->tokencount == 3) { /* with initializer */
        int num;
        type = CRUSTY_TYPE_INT;
        length = 1;

        num = strtol(TOKENVAL(line->offset[2]), &end, 0);
        if(end != TOKENVAL(line->offset[2]) && *end == '\0') {
            intinit = malloc(sizeof(int));
            if(intinit == NULL) {
                LOG_PRINTF_LINE(cvm, "Failed to allocate memory for initializer.\n");
                return(-1);
            }

            intinit[0] = num;
            initializer = intinit;
        } else {
            LOG_PRINTF_LINE(cvm, "Initializer wasn't a number.\n");
            return(-1);
        }
    } else if(line->tokencount == 4) {
        if(compare_token_and_string(cvm, line->offset[2], "ints") == 0) {
            type = CRUSTY_TYPE_INT;
            length = number_list_ints(TOKENVAL(line->offset[3]),
                                      TOKENLEN(line->offset[3]),
                                      &intinit);
            if(length < 0) {
                LOG_PRINTF_LINE(cvm, "Failed to allocate memory for initializer.\n");
                return(-1);
            } else if(length == 0) {
                LOG_PRINTF_LINE(cvm, "Initializer must be a space separated list of numbers.\n");
                return(-1);
            } else if(length == 1) { /* array without initializer, so fill with zero */
                length = intinit[0];
                if(length <= 0) {
                    LOG_PRINTF_LINE(cvm, "Arrays size must be positive and non "
                                        "zero.\n");
                    free(intinit);
                    return(-1);
                }

                free(intinit);
                intinit = malloc(sizeof(int) * length);
                if(intinit == NULL) {
                    LOG_PRINTF_LINE(cvm, "Failed to allocate memory for initializer.\n");
                    return(-1);
                }

                memset(intinit, 0, sizeof(int) * length);
                initializer = intinit;
            }
            /* array with initializer, nothing to do, since length and intinit
               are already what they should be */
            initializer = intinit;
        } else if(compare_token_and_string(cvm,
                                           line->offset[2],
                                           "floats") == 0) {
            type = CRUSTY_TYPE_FLOAT;
            /* if the argument provided is a single, valid integer, use that
             * for the length, otherwise, it's a list of float initializers */
            length = strtol(TOKENVAL(line->offset[3]), &end, 0);
            if(end != TOKENVAL(line->offset[3]) &&
               *end == '\0') {
                if(length <= 0) {
                    LOG_PRINTF_LINE(cvm, "Arrays size must be positive and non "
                                        "zero.\n");
                    return(-1);
                }
                floatinit = malloc(length * sizeof(double));
                if(floatinit == NULL) {
                    LOG_PRINTF_LINE(cvm, "Failed to allocate memory for initializer");
                    return(-1);
                }
                memset(floatinit, 0, sizeof(double) * length);
                initializer = floatinit;
            } else {
                length = number_list_floats(TOKENVAL(line->offset[3]),
                                            TOKENLEN(line->offset[3]),
                                            &floatinit);
                if(length < 0) {
                    LOG_PRINTF_LINE(cvm, "Failed to allocate memory for initializer.\n");
                    return(-1);
                } else if(length == 0) {
                    LOG_PRINTF_LINE(cvm, "Initializer must be a space separated list of numbers.\n");
                    return(-1);
                }
                initializer = floatinit;
            }
            /* array with initializer */
        } else if(compare_token_and_string(cvm,
                                           line->offset[2],
                                           "string") == 0) {
            type = CRUSTY_TYPE_CHAR;
            length = TOKENLEN(line->offset[3]);
            initializer = TOKENVAL(line->offset[3]);
        } else {
            LOG_PRINTF_LINE(cvm, "variable declaration can be array or string.\n");
            return(-1);
        }
    } else {
        LOG_PRINTF_LINE(cvm, "static can be declared as string or "
                             "array and is followed by an initializer "
                             "or array may be followed by a numeric "
                             "size.\n");
        return(-1);
    }

    if(new_variable(cvm,
                    line->offset[1],
                    type,
                    length,
                    initializer,
                    NULL,
                    procIndex) < 0) {
        /* print an error so the user can get a line number. */
        LOG_PRINTF_LINE(cvm, "Error from new_variable().\n");
        return(-1);
    }

    /* free memory which is copied in to the procedure/global initializer */
    if(intinit != NULL) {
        free(intinit);
    } else if(floatinit != NULL) {
        free(floatinit);
    }
    /* don't free chrinit since that's just copied from the token list */
            
    return(0);
}

static int symbols_scan(CrustyVM *cvm,
                        char *safepath) {
    unsigned int i, j;
    CrustyProcedure *curProc = NULL;
    int curProcIndex = -1;

    CrustyLine *new = NULL;
    char *temp;
    unsigned int lines;

    new = malloc(sizeof(CrustyLine) * cvm->lines);
    if(new == NULL) {
        LOG_PRINTF(cvm, "Failed to allocate memory for lines\n");
        return(-1);
    }

    cvm->stacksize = 0;

    lines = 0;
    for(cvm->logline = 0; cvm->logline < cvm->lines; cvm->logline++) {
        if(curProc != NULL) {
            curProc->length++;
        }
        /* no need to check if tokencount > 0 because those lines were filtered
           out previously */
        if(compare_token_and_string(cvm,
                                    GET_TOKEN_OFFSET(cvm->logline, 0),
                                    "proc") == 0) {
            if(cvm->line[cvm->logline].tokencount < 2) {
                LOG_PRINTF_LINE(cvm, "proc takes a name as argument.\n");
                goto failure;
            }

            if(curProc != NULL) {
                LOG_PRINTF_LINE(cvm, "proc within proc.\n");
                goto failure;
            }

            if(find_procedure(cvm, GET_TOKEN(cvm->logline, 1)) >= 0) {
                LOG_PRINTF_LINE(cvm, "Redeclaration of procedure.\n");
                goto failure;
            }

            curProc = realloc(cvm->proc, sizeof(CrustyProcedure) * (cvm->procs + 1));
            if(curProc == NULL) {
                LOG_PRINTF_LINE(cvm, "Couldn't allocate memory for procedure.\n");
                goto failure;
            }
            cvm->proc = curProc;
            curProcIndex = cvm->procs;
            curProc = &(cvm->proc[curProcIndex]);
            cvm->procs++;

            curProc->nameOffset = cvm->line[cvm->logline].offset[1];
            curProc->start = lines;
            curProc->length = 0;
            curProc->stackneeded = 0;
            curProc->initializer = NULL;
            curProc->args = 0; 
            curProc->var = NULL;
            curProc->varIndex = NULL;
            curProc->vars = 0;
            curProc->label = NULL;
            curProc->labels = 0;

            unsigned int args = cvm->line[cvm->logline].tokencount - 2;
            /* add arguments as local variables */
            for(i = 0; i < args; i++) {
                /* argument variables have 0 length and no initializers and no
                   read or write functions but obviously is a local variable */
                if(new_variable(cvm,
                                cvm->line[cvm->logline].offset[i + 2],
                                CRUSTY_TYPE_NONE,
                                0,
                                NULL,
                                NULL,
                                curProcIndex) < 0) {
                                    /* print an error so the user can get a line
                                     * number. */
                                    LOG_PRINTF_LINE(cvm, "Error from "
                                                        "new_variable().\n");
                                    goto failure;
                }
            }                    

            continue; /* don't copy in to new list */
        } else if(compare_token_and_string(cvm,
                                           GET_TOKEN_OFFSET(cvm->logline, 0),
                                           "ret") == 0) {
            if(curProc == NULL) {
                LOG_PRINTF_LINE(cvm, "ret without proc.\n");
                goto failure;
            }

            cvm->stacksize += curProc->stackneeded;
            curProc = NULL;
            curProcIndex = -1;

            /* this is a real instruction, so it should be copied over */
        } else if(compare_token_and_string(cvm,
                                           GET_TOKEN_OFFSET(cvm->logline, 0),
                                           "static") == 0) {
            if(cvm->line[cvm->logline].tokencount < 2) {
                LOG_PRINTF_LINE(cvm, "static takes a name as argument.\n");
                goto failure;
            }

            if(variable_declaration(cvm, &(cvm->line[cvm->logline]), -1) < 0) {
                goto failure;
            }

            continue; /* don't copy in to new list */
        } else if(compare_token_and_string(cvm,
                                           GET_TOKEN_OFFSET(cvm->logline, 0),
                                           "local") == 0) {
            if(cvm->line[cvm->logline].tokencount < 2) {
                LOG_PRINTF_LINE(cvm, "local takes a name as argument.\n");
                goto failure;
            }

            if(curProc == NULL) {
                LOG_PRINTF_LINE(cvm, "local declared outside of procedure.\n");
                goto failure;
            }

            if(variable_declaration(cvm, &(cvm->line[cvm->logline]), curProcIndex) < 0) {
                goto failure;
            }

            continue; /* don't copy in to new list */
        } else if(compare_token_and_string(cvm,
                                           GET_TOKEN_OFFSET(cvm->logline, 0),
                                           "stack") == 0) {
            if(cvm->line[cvm->logline].tokencount != 2) {
                LOG_PRINTF_LINE(cvm, "stack takes a number as argument.\n");
                goto failure;
            }

            long stack = strtol(GET_TOKEN(cvm->logline, 1), &temp, 0);
            if(temp - GET_TOKEN(cvm->logline, 1) == TOKENLEN(GET_TOKEN_OFFSET(cvm->logline, 1))) {
                cvm->stacksize += stack;
            } else {
                LOG_PRINTF_LINE(cvm, "stack takes a number as argument.\n");
                goto failure;
            }

            continue; /* don't copy in to new list */
        } else if(compare_token_and_string(cvm,
                                           GET_TOKEN_OFFSET(cvm->logline, 0),
                                           "label") == 0) {
            if(cvm->line[cvm->logline].tokencount != 2) {
                LOG_PRINTF_LINE(cvm, "label takes a name as argument.\n");
                goto failure;
            }

            if(curProc == NULL) {
                LOG_PRINTF_LINE(cvm, "label not in a procedure.\n");
                goto failure;
            }

            for(i = 0; i < curProc->labels; i++) {
                if(compare_token_and_token(cvm,
                                           GET_TOKEN_OFFSET(cvm->logline, 1),
                                           curProc->label[i].nameOffset) == 0) {
                    LOG_PRINTF_LINE(cvm, "Duplicate label: %s\n",
                                        GET_TOKEN(cvm->logline, 1));
                    goto failure;
                }
            }

            temp = realloc(curProc->label, sizeof(CrustyLabel) * (curProc->labels + 1));
            if(temp == NULL) {
                LOG_PRINTF_LINE(cvm, "Failed to allocate memory for labels list.\n");
                goto failure;
            }
            curProc->label = (CrustyLabel *)temp;
            curProc->label[curProc->labels].nameOffset =
                cvm->line[cvm->logline].offset[1];
            curProc->label[curProc->labels].line = lines;
            curProc->labels++;

            continue; /* don't copy in to new list */
        } else if(compare_token_and_string(cvm,
                                           GET_TOKEN_OFFSET(cvm->logline, 0),
                                           "binclude") == 0) {
            if(cvm->line[cvm->logline].tokencount < 4 ||
               cvm->line[cvm->logline].tokencount > 6) {
                LOG_PRINTF_LINE(cvm, "binclude takes at least a symbol name, "
                                    "type and filename and optionally a "
                                    "start and length.\n");
                goto failure;
            }

            unsigned long fileStart = 0;
            long fileLength = 0;

            if(cvm->line[cvm->logline].tokencount >= 5) {
                fileStart = strtoul(GET_TOKEN(cvm->logline, 4), &temp, 0);
                if(temp - GET_TOKEN(cvm->logline, 4) != TOKENLEN(GET_TOKEN_OFFSET(cvm->logline, 4))) {
                    LOG_PRINTF_LINE(cvm, "binclude start field must be a "
                                        "number\n");
                    goto failure;
                }
            }
            if(cvm->line[cvm->logline].tokencount == 6) {
                fileLength = strtoul(GET_TOKEN(cvm->logline, 5), &temp, 0);
                if(temp - GET_TOKEN(cvm->logline, 5) != TOKENLEN(GET_TOKEN_OFFSET(cvm->logline, 5))) {
                    LOG_PRINTF_LINE(cvm, "binclude start field must be a "
                                        "number\n");
                    goto failure;
                }
            }

            CrustyType type;
            if(compare_token_and_string(cvm,
                                        GET_TOKEN_OFFSET(cvm->logline, 2),
                                        "chars") == 0) {
                type = CRUSTY_TYPE_CHAR;
            } else if(compare_token_and_string(cvm,
                                        GET_TOKEN_OFFSET(cvm->logline, 2),
                                        "ints") == 0) {
                type = CRUSTY_TYPE_INT;
            } else if(compare_token_and_string(cvm,
                                        GET_TOKEN_OFFSET(cvm->logline, 2),
                                        "floats") == 0) {
                type = CRUSTY_TYPE_FLOAT;
            } else {
                LOG_PRINTF_LINE(cvm, "Type must be chars, ints or floats.\n");
                goto failure;
            }
            
            FILE *in;
            in = crustyvm_open_file(GET_TOKEN(cvm->logline, 3),
                                    &safepath,
                                    cvm->log_cb,
                                    cvm->log_priv);
            if(in == NULL) {
                LOG_PRINTF_LINE(cvm, "Failed to open %s for reading.\n",
                                    GET_TOKEN(cvm->logline, 3));
                goto failure;
            }
            if(fileLength == 0) {
                if(fseek(in, 0, SEEK_END) < 0) {
                    LOG_PRINTF_LINE(cvm, "Failed to seek to end.\n");
                    goto failure;
                }
                fileLength = ftell(in);
                if(fileLength < 0) {
                    LOG_PRINTF_LINE(cvm, "Failed to get length.\n");
                    goto failure;
                }
                if(fseek(in, 0, SEEK_SET) < 0) {
                    LOG_PRINTF_LINE(cvm, "Failed to seek to start.\n");
                    goto failure;
                }
                fileLength -= fileStart;
            }

            if(type == CRUSTY_TYPE_INT) {
                fileLength = fileLength / sizeof(int) * sizeof(int);
           } else if(type == CRUSTY_TYPE_FLOAT) {
                fileLength = fileLength / sizeof(double) * sizeof(double);
            }
            if(fileLength == 0) {
                LOG_PRINTF_LINE(cvm, "Selected size not large enough for "
                                    "type.\n");
                goto failure;
            }
 
            if(fseek(in, fileStart, SEEK_SET) < 0) {
                LOG_PRINTF_LINE(cvm, "Failed to seek to position.\n");
                goto failure;
            }

            unsigned char *buf;
            buf = malloc(fileLength);
            if(buf == NULL) {
                LOG_PRINTF_LINE(cvm, "Couldn't allocate memory.\n");
                goto failure;
            }

            if(fread(buf, 1, fileLength, in) != fileLength) {
                LOG_PRINTF_LINE(cvm, "Couldn't read full length.\n");
                free(buf);
                goto failure;
            }

            fclose(in);

            if(type == CRUSTY_TYPE_CHAR) {
                if(new_variable(cvm,
                                cvm->line[cvm->logline].offset[1],
                                CRUSTY_TYPE_CHAR,
                                fileLength,
                                buf,
                                NULL,
                                curProcIndex) < 0) {
                    free(buf);
                    goto failure;
                }
            } else if(type == CRUSTY_TYPE_INT) {
                if(new_variable(cvm,
                                cvm->line[cvm->logline].offset[1],
                                CRUSTY_TYPE_INT,
                                fileLength / sizeof(int),
                                buf,
                                NULL,
                                curProcIndex) < 0) {
                    free(buf);
                    goto failure;
                }
            } else if(type == CRUSTY_TYPE_FLOAT) {
                if(new_variable(cvm,
                                cvm->line[cvm->logline].offset[1],
                                CRUSTY_TYPE_FLOAT,
                                fileLength / sizeof(double),
                                buf,
                                NULL,
                                curProcIndex) < 0) {
                    free(buf);
                    goto failure;
                }
            }
            free(buf);

            continue;
        }

        new[lines].tokencount = cvm->line[cvm->logline].tokencount;
        new[lines].moduleOffset = cvm->line[cvm->logline].moduleOffset;
        new[lines].line = cvm->line[cvm->logline].line;
        new[lines].offset = malloc(sizeof(long) * new[lines].tokencount);
        if(new[lines].offset == NULL) {
            LOG_PRINTF_LINE(cvm, "Failed to allocate memory for line copy.\n");
            goto failure;
        }
        for(i = 0; i < new[lines].tokencount; i++) {
            new[lines].offset[i] = cvm->line[cvm->logline].offset[i];
        }
        lines++;
    }

    if(curProc != NULL) {
        LOG_PRINTF(cvm, "Procedure without return.\n");
        goto failure;
    }

    /* point CrustyVariables in CrustyProcedures and vise versa */
    for(i = 0; i < cvm->vars; i++) {
        cvm->var[i].proc = NULL;
    }

    for(i = 0; i < cvm->procs; i++) {
        cvm->proc[i].var = malloc(sizeof(CrustyVariable *) * cvm->proc[i].vars);
        if(cvm->proc[i].var == NULL) {
            LOG_PRINTF(cvm, "Failed to allocate memory for procedure variable pointer list.\n");
            goto failure;
        }

        for(j = 0; j < cvm->proc[i].vars; j++) {
            cvm->proc[i].var[j] = &(cvm->var[cvm->proc[i].varIndex[j]]);
            cvm->proc[i].var[j]->proc = &(cvm->proc[i]);
        }
    }

    temp = realloc(new, sizeof(CrustyLine) * lines);
    if(temp == NULL) {
        LOG_PRINTF(cvm, "Failed to shrink line array.\n");
        goto failure;
    }

    for(i = 0; i < cvm->lines; i++) {
        free(cvm->line[i].offset);
    }
    free(cvm->line);
    cvm->line = (CrustyLine *)temp;
    cvm->lines = lines;

    cvm->stacksize += cvm->initialstack;

    return(0);

failure:
    if(new != NULL) {
        for(i = 0; i < lines; i++) {
            if(new[i].offset != NULL) {
                free(new[i].offset);
            }
        }
        free(new);
    }

    return(-1);
}

/* get a bunch of messy checks out of the way.  Many of these are wacky and
   should be impossible but may as well get as much out of the way as possible. */
static int symbols_verify(CrustyVM *cvm) {
    unsigned int i, j, k;
    unsigned int leni, lenj, offi, offj;
    int ret = 0;

    for(i = 0; i < cvm->vars; i++) {
        if(variable_is_global(&(cvm->var[i]))) {
            if(cvm->var[i].length == 0) {
                LOG_PRINTF(cvm, "Global variable %s has 0 length.\n", cvm->var[i].name);
                ret = -1;
            }

            if(cvm->var[i].read == NULL && cvm->var[i].write == NULL) {
                if(cvm->var[i].type != CRUSTY_TYPE_INT &&
                   cvm->var[i].type != CRUSTY_TYPE_FLOAT &&
                   cvm->var[i].type != CRUSTY_TYPE_CHAR) {
                    LOG_PRINTF(cvm, "Non-callback variable with invalid type.\n");
                    continue;
                }

                if(cvm->var[i].type == CRUSTY_TYPE_INT) {
                    leni = cvm->var[i].length * sizeof(int);
                } else if(cvm->var[i].type == CRUSTY_TYPE_FLOAT) {
                    leni = cvm->var[i].length * sizeof(double);
                } else {
                    leni = cvm->var[i].length;
                }
                if(cvm->var[i].offset + leni > cvm->initialstack) {
                    LOG_PRINTF(cvm, "Global variable %s exceeds initial stack: "
                                    "%u + %u = %u > %u\n",
                                    cvm->var[i].name,
                                    cvm->var[i].offset,
                                    leni,
                                    cvm->var[i].offset + leni,
                                    cvm->initialstack);
                    ret = -1;
                }
                for(j = i + 1; j < cvm->vars; j++) {
                    if(variable_is_global(&(cvm->var[j]))) {
                        if(cvm->var[i].type == CRUSTY_TYPE_INT) {
                            lenj = cvm->var[j].length * sizeof(int);
                        } else if(cvm->var[i].type == CRUSTY_TYPE_FLOAT) {
                            lenj = cvm->var[j].length * sizeof(double);
                        } else {
                            lenj = cvm->var[j].length;
                        }
                        if((cvm->var[j].offset > cvm->var[i].offset &&
                            cvm->var[j].offset < cvm->var[i].offset + leni - 1) ||
                           (cvm->var[j].offset + lenj - 1 > cvm->var[i].offset &&
                            cvm->var[j].offset + lenj - 1 < cvm->var[i].offset + leni - 1)) {
                            LOG_PRINTF(cvm, "Global variables %s and %s overlap: "
                                            "(%u -> %u) (%u -> %u)\n",
                                       cvm->var[i].name, cvm->var[j].name,
                                       cvm->var[i].offset,
                                       cvm->var[i].offset + leni - 1,
                                       cvm->var[j].offset,
                                       cvm->var[j].offset + lenj - 1);
                            ret = -1;
                        }
                    }
                }
            }
        } else { /* locals */
            if(variable_is_callback(&(cvm->var[i]))) {
                LOG_PRINTF(cvm, "Local variable %s with callback.\n", cvm->var[i].name);
                ret = -1;
            }

            for(j = 0; j < cvm->var[i].proc->vars; j++) {
                if(cvm->var[i].proc == cvm->var[i].proc->var[j]->proc) {
                    break;
                }
            }
            if(j == cvm->var[i].proc->vars) {
                LOG_PRINTF(cvm, "Couldn't find variable in procedure %s "
                                "referenced by variable %s.\n",
                           cvm->var[i].proc->name, cvm->var[i].name);
                ret = -1;
            }
        }
    }

    for(i = 0; i < cvm->procs; i++) {
        for(j = 0; j < cvm->proc[i].vars; j++) {
            if(cvm->proc[i].var[j]->proc != &(cvm->proc[i])) {
                LOG_PRINTF(cvm, "Mispointed variable %s points to procedure %s "
                                "but pointed to by procedure %s.\n",
                           cvm->proc[i].var[j]->name,
                           cvm->proc[i].var[j]->proc->name,
                           cvm->proc[i].name);
                ret = -1;
            }

            if(cvm->proc[i].var[j]->length == 0) {
                if(j > cvm->proc[i].args) {
                    LOG_PRINTF(cvm, "Variable %s in proc %s has 0 length but "
                                    "index greater than args. (%u > %u)\n",
                               cvm->proc[i].var[j]->name,
                               cvm->proc[i].name,
                               j, cvm->proc[i].args);
                    ret = -1;
                }
                if(cvm->proc[i].var[j]->offset > cvm->proc[i].args) {
                    LOG_PRINTF(cvm, "Variable %s in proc %s is argument but "
                                    "stack offset greater than args. (%u > %u)\n",
                               cvm->proc[i].var[j]->name,
                               cvm->proc[i].name,
                               cvm->proc[i].var[j]->offset,
                               cvm->proc[i].args);
                    ret = -1;
                }
            }

            if(!variable_is_argument(cvm->proc[i].var[j])) {
                if(cvm->proc[i].var[j]->type == CRUSTY_TYPE_INT) {
                    lenj = cvm->proc[i].var[j]->length * sizeof(int);
                    offj = cvm->proc[i].var[j]->offset;
                    offj -= lenj; /* stack is indexed from top */
                } else if(cvm->proc[i].var[j]->type == CRUSTY_TYPE_FLOAT) {
                    lenj = cvm->proc[i].var[j]->length * sizeof(double);
                    offj = cvm->proc[i].var[j]->offset;
                    offj -= lenj; /* stack is indexed from top */
                } else { /* CHAR */
                    lenj = cvm->proc[i].var[j]->length;
                    offj = cvm->proc[i].var[j]->offset;
                    offj -= lenj;
                }
            } else {
                lenj = sizeof(CrustyStackArg);
                offj = cvm->proc[i].var[j]->offset * sizeof(CrustyStackArg);
                offj -= lenj;
            }
            if(offj > cvm->proc[i].stackneeded) {
                LOG_PRINTF(cvm, "Variable %s from procedure %s exceeds "
                                "needed stack: %u > %u\n",
                                cvm->proc[i].var[j]->name,
                                cvm->proc[i].name,
                                offj,
                                cvm->proc[i].stackneeded);
                ret = -1;
            }
            for(k = j + 1; k < cvm->proc[i].vars; k++) {
                if(!variable_is_argument(cvm->proc[i].var[k])) {
                    if(cvm->proc[i].var[k]->type == CRUSTY_TYPE_INT) {
                        leni = cvm->proc[i].var[k]->length * sizeof(int);
                        offi = cvm->proc[i].var[k]->offset;
                        offi -= leni; /* stack is indexed from top */
                    } else if(cvm->proc[i].var[k]->type == CRUSTY_TYPE_FLOAT) {
                        leni = cvm->proc[i].var[k]->length * sizeof(double);
                        offi = cvm->proc[i].var[k]->offset;
                        offi -= leni; /* stack is indexed from top */
                    } else { /* CHAR */
                        leni = cvm->proc[i].var[k]->length;
                        offi = cvm->proc[i].var[k]->offset;
                        offi -= leni;
                    }
                } else {
                    leni = sizeof(CrustyStackArg);
                    offi = cvm->proc[i].var[k]->offset * sizeof(CrustyStackArg);
                    offi -= leni;
                }
                if((offi > offj &&
                    offi < offj + lenj - 1) ||
                   (offi + leni - 1 > offj &&
                    offi + leni - 1 < offj + lenj - 1)) {
                    LOG_PRINTF(cvm, "Variables %s and %s from procedure %s "
                                    "overlap: (%u -> %u) (%u -> %u)\n",
                               cvm->proc[i].var[j]->name,
                               cvm->proc[i].var[k]->name,
                               cvm->proc[i].name,
                               offj, offj + lenj - 1,
                               offi, offi + leni - 1);
                    ret = -1;
                }
            }
        }
    }

    return(ret);
}

static int populate_var(CrustyVM *cvm,
                        char *name,
                        CrustyProcedure *proc,
                        int readable,
                        int writable,
                        int *flags,
                        int *var,
                        int *index) {
    CrustyVariable *varObj, *indexObj;
    char *colon;
    char *vararray = NULL;
    char *end;

    *flags = 0;
    *index = 0;

    /* test to see if we have an immediate value */
    *var = strtol(name, &end, 0);
    if(name != end && *end == '\0') {
        /* immediate value */
        if(writable) {
            LOG_PRINTF_LINE(cvm, "Immediate values aren't writable.\n");
            return(-1);
        }

        *flags |= MOVE_FLAG_IMMEDIATE;
        return(0);
    }

    /* find colon */
    colon = strrchr(name, ':');

    if(colon != NULL) {
        /* replace : with 0 and start at beginning of offset, if any */
        *colon = '\0';
        vararray = &(colon[1]);
    }

    *var = find_variable(cvm, proc, name);
    if(*var == -1) {
        LOG_PRINTF_LINE(cvm, "Variable %s not found.\n", name);
        goto failure;
    }
    varObj = &(cvm->var[*var]);

    if(writable) {
        if(varObj->read != NULL && varObj->write == NULL) {
            LOG_PRINTF_LINE(cvm, "%s isn't a writable callback.\n", name);
            goto failure;
        }
    }
    if(readable) {
        if(varObj->write != NULL && varObj->read == NULL) {
            LOG_PRINTF_LINE(cvm, "%s isn't a readable callback.\n", name);
            goto failure;
        }
    }

    if(vararray != NULL) {
        if(*vararray == '\0') {
            /* array length */
            if(writable) {
                LOG_PRINTF_LINE(cvm, "Array length isn't writable.\n");
                goto failure;
            }

            *flags |= MOVE_FLAG_LENGTH;
        } else {
            *flags |= MOVE_FLAG_VAR;
            *index = strtol(vararray, &end, 0);
            if(end != vararray && *end == '\0') {
                /* array with immediate index, if length is 0, it's a local
                 * variable and we can't know until runtime. */
                if(*index < 0 ||
                   ((int)(varObj->length) > 0 &&
                    *index > (int)(varObj->length) - 1)) {
                    LOG_PRINTF_LINE(cvm, "Immediate index out of array size.\n");
                    goto failure;
                }
                *flags |= MOVE_FLAG_INDEX_IMMEDIATE;
            } else {
                /* array with variable index */
                *index = find_variable(cvm, proc, vararray);
                if(*index == -1) {
                    LOG_PRINTF_LINE(cvm, "Array index variable %s not found.\n", vararray);
                    goto failure;
                }
                indexObj = &(cvm->var[*index]);

                if(indexObj->write != NULL &&
                   indexObj->read == NULL) {
                    LOG_PRINTF_LINE(cvm, "%s isn't a readable callback.\n", vararray);
                    goto failure;
                }

                *flags |= MOVE_FLAG_INDEX_VAR;
            }
        }
    } else {
        /* variable */
        *flags |= MOVE_FLAG_VAR | MOVE_FLAG_INDEX_IMMEDIATE;
        *index = 0;
    }

    if(colon != NULL) {
        *colon = ':';
    }
    return(0);

failure:
    if(colon != NULL) {
        *colon = ':';
    }
    return(-1);
}

static int *new_instruction(CrustyVM *cvm, unsigned int args) {
    int *temp;

    temp = realloc(cvm->inst, sizeof(int) * (cvm->insts + args + 1));
    if(temp == NULL) {
        LOG_PRINTF_LINE(cvm, "Failed to allocate memory for instructions.\n");
        return(NULL);
    }
    cvm->inst = temp;
    temp = &(cvm->inst[cvm->insts]);
    cvm->insts += (args + 1);

    return(temp);
}

static int find_label(CrustyProcedure *proc, const char *name) {
    unsigned int i;

    for(i = 0; i < proc->labels; i++) {
        if(strcmp(proc->label[i].name, name) == 0) {
            return(proc->label[i].line);
        }
    }

    return(-1);
}

#define MATH_INSTRUCTION(NAME, ENUM) \
    else if(compare_token_and_string(cvm, \
                                     GET_TOKEN_OFFSET(cvm->logline, 0), \
                                     NAME) == 0) { \
        if(cvm->line[cvm->logline].tokencount != 3) { \
            LOG_PRINTF_LINE(cvm, NAME " takes two operands.\n"); \
            return(-1); \
        } \
    \
        inst = new_instruction(cvm, MOVE_ARGS); \
        if(inst == NULL) { \
            return(-1); \
        } \
    \
        inst[0] = ENUM; \
    \
        if(populate_var(cvm, \
                        GET_TOKEN(cvm->logline, 1), \
                        curproc, \
                        1, 1, \
                        &(inst[MOVE_DEST_FLAGS]), \
                        &(inst[MOVE_DEST_VAL]), \
                        &(inst[MOVE_DEST_INDEX])) < 0) { \
            return(-1); \
        } \
    \
        if(populate_var(cvm, \
                        GET_TOKEN(cvm->logline, 2), \
                        curproc, \
                        1, 0, \
                        &(inst[MOVE_SRC_FLAGS]), \
                        &(inst[MOVE_SRC_VAL]), \
                        &(inst[MOVE_SRC_INDEX])) < 0) { \
            return(-1); \
        }

#define JUMP_INSTRUCTION(NAME, ENUM) \
    else if(compare_token_and_string(cvm, \
                                     GET_TOKEN_OFFSET(cvm->logline, 0), \
                                     NAME) == 0) { \
        if(cvm->line[cvm->logline].tokencount != 2) { \
            LOG_PRINTF_LINE(cvm, NAME " takes a label.\n"); \
            return(-1); \
        } \
    \
        inst = new_instruction(cvm, JUMP_ARGS); \
        if(inst == NULL) { \
            return(-1); \
        } \
    \
        inst[0] = ENUM; \
    \
        inst[JUMP_LOCATION] = find_label(curproc, GET_TOKEN(cvm->logline, 1)); \
        if(inst[JUMP_LOCATION] == -1) { \
            LOG_PRINTF_LINE(cvm, "Couldn't find label %s.\n", \
                                 GET_TOKEN(cvm->logline, 1)); \
            return(-1); \
        }

static int codegen(CrustyVM *cvm) {
    CrustyProcedure *curproc = NULL;
    int procnum = 0;
    int *inst;
    unsigned int j;

    for(cvm->logline = 0; cvm->logline < cvm->lines; cvm->logline++) {
        if(curproc == NULL) {
            if(cvm->logline == cvm->proc[procnum].start) {
                curproc = &(cvm->proc[procnum]);
                curproc->instruction = cvm->insts;
            }
        }

        if(curproc == NULL) {
            LOG_PRINTF_LINE(cvm, "BUG: code line not in a procedure.\n");
            return(-1);
        }

        cvm->line[cvm->logline].instruction = cvm->insts;

        if(compare_token_and_string(cvm,
                                    GET_TOKEN_OFFSET(cvm->logline, 0),
                                    "move") == 0) {
            if(cvm->line[cvm->logline].tokencount != 3) {
                LOG_PRINTF_LINE(cvm, "move takes a destination and source.\n");
                return(-1);
            }

            inst = new_instruction(cvm, MOVE_ARGS);
            if(inst == NULL) {
                return(-1);
            }

            inst[0] = CRUSTY_INSTRUCTION_TYPE_MOVE;

            if(populate_var(cvm,
                            GET_TOKEN(cvm->logline, 1),
                            curproc,
                            0, 1,
                            &(inst[MOVE_DEST_FLAGS]),
                            &(inst[MOVE_DEST_VAL]),
                            &(inst[MOVE_DEST_INDEX])) < 0) {
                return(-1);
            }

            if(populate_var(cvm,
                            GET_TOKEN(cvm->logline, 2),
                            curproc,
                            1, 0,
                            &(inst[MOVE_SRC_FLAGS]),
                            &(inst[MOVE_SRC_VAL]),
                            &(inst[MOVE_SRC_INDEX])) < 0) {
                return(-1);
            }
        } MATH_INSTRUCTION("add", CRUSTY_INSTRUCTION_TYPE_ADD)
        } MATH_INSTRUCTION("sub", CRUSTY_INSTRUCTION_TYPE_SUB)
        } MATH_INSTRUCTION("mul", CRUSTY_INSTRUCTION_TYPE_MUL)
        } MATH_INSTRUCTION("div", CRUSTY_INSTRUCTION_TYPE_DIV)
        } MATH_INSTRUCTION("mod", CRUSTY_INSTRUCTION_TYPE_MOD)
        } MATH_INSTRUCTION("and", CRUSTY_INSTRUCTION_TYPE_AND)
        } MATH_INSTRUCTION("or",  CRUSTY_INSTRUCTION_TYPE_OR )
        } MATH_INSTRUCTION("xor", CRUSTY_INSTRUCTION_TYPE_XOR)
        } MATH_INSTRUCTION("shr", CRUSTY_INSTRUCTION_TYPE_SHR)
        } MATH_INSTRUCTION("shl", CRUSTY_INSTRUCTION_TYPE_SHL)
        } else if(compare_token_and_string(cvm,
                                           GET_TOKEN_OFFSET(cvm->logline, 0),
                                           "cmp") == 0) {
            if(cvm->line[cvm->logline].tokencount < 2 ||
               cvm->line[cvm->logline].tokencount > 3) {
                LOG_PRINTF_LINE(cvm, "cmp takes one or two operands.\n");
                return(-1);
            }

            inst = new_instruction(cvm, MOVE_ARGS);
            if(inst == NULL) {
                return(-1);
            }

            inst[0] = CRUSTY_INSTRUCTION_TYPE_CMP;

            if(populate_var(cvm,
                            GET_TOKEN(cvm->logline, 1),
                            curproc,
                            1, 0,
                            &(inst[MOVE_DEST_FLAGS]),
                            &(inst[MOVE_DEST_VAL]),
                            &(inst[MOVE_DEST_INDEX])) < 0) {
                return(-1);
            }

            if(cvm->line[cvm->logline].tokencount == 3) {
                if(populate_var(cvm,
                                GET_TOKEN(cvm->logline, 2),
                                curproc,
                                1, 0,
                                &(inst[MOVE_SRC_FLAGS]),
                                &(inst[MOVE_SRC_VAL]),
                                &(inst[MOVE_SRC_INDEX])) < 0) {
                    return(-1);
                }
            } else {
                inst[MOVE_SRC_FLAGS] = MOVE_FLAG_IMMEDIATE;
                inst[MOVE_SRC_VAL] = 0;
                inst[MOVE_SRC_INDEX] = 0; /* ignored but may as well */
            }
        } JUMP_INSTRUCTION("jump",  CRUSTY_INSTRUCTION_TYPE_JUMP )
        } JUMP_INSTRUCTION("jumpn", CRUSTY_INSTRUCTION_TYPE_JUMPN)
        } JUMP_INSTRUCTION("jumpz", CRUSTY_INSTRUCTION_TYPE_JUMPZ)
        } JUMP_INSTRUCTION("jumpl", CRUSTY_INSTRUCTION_TYPE_JUMPL)
        } JUMP_INSTRUCTION("jumpg", CRUSTY_INSTRUCTION_TYPE_JUMPG)
        } else if(compare_token_and_string(cvm,
                                           GET_TOKEN_OFFSET(cvm->logline, 0),
                                           "call") == 0) {
            if(cvm->line[cvm->logline].tokencount < 2) {
                LOG_PRINTF_LINE(cvm, "call takes a procedure and possible arguments.\n");
                return(-1);
            }

            unsigned int args = cvm->line[cvm->logline].tokencount - 2;
            unsigned int i;

            inst = new_instruction(cvm, (args * 3) + 1);
            if(inst == NULL) {
                return(-1);
            }

            inst[0] = CRUSTY_INSTRUCTION_TYPE_CALL;

            inst[1] = find_procedure(cvm,
                                     GET_TOKEN(cvm->logline, 1));
            if(inst[1] == -1) {
                LOG_PRINTF_LINE(cvm, "Couldn't find procedure %s.\n",
                                     GET_TOKEN(cvm->logline, 1));
                return(-1);
            }

            if(args != cvm->proc[inst[1]].args) {
                LOG_PRINTF_LINE(cvm, "Procedure %s takes %u args, %u given.\n",
                                cvm->proc[inst[1]].name,
                                cvm->proc[inst[1]].args,
                                args);
                return(-1);
            }

            for(i = 0; i < args; i++) {
                if(populate_var(cvm,
                            GET_TOKEN(cvm->logline, i + 2),
                            curproc,
                            0, 0,
                            &(inst[CALL_START_ARGS + (i * CALL_ARG_SIZE) + CALL_ARG_FLAGS]),
                            &(inst[CALL_START_ARGS + (i * CALL_ARG_SIZE) + CALL_ARG_VAL]),
                            &(inst[CALL_START_ARGS + (i * CALL_ARG_SIZE) + CALL_ARG_INDEX]))
                            < 0 ) {
                    return(-1);
                }
            }
        } else if(compare_token_and_string(cvm,
                                           GET_TOKEN_OFFSET(cvm->logline, 0),
                                           "ret") == 0) {
            if(cvm->line[cvm->logline].tokencount != 1) {
                LOG_PRINTF_LINE(cvm, "ret takes no arguments.\n");
                return(-1);
            }

            inst = new_instruction(cvm, 0);
            if(inst == NULL) {
                return(-1);
            }

            inst[0] = CRUSTY_INSTRUCTION_TYPE_RET;

            procnum++;
            curproc = NULL;
        } else {
            LOG_PRINTF_LINE(cvm, "Invalid instruction mnemonic: %s\n",
                                 GET_TOKEN(cvm->logline, 0));
            return(-1);
        }
    }

    /* convert jump arguments from line to ip */
    for(j = 0; j < cvm->lines; j++) {
        switch(cvm->inst[cvm->line[j].instruction]) {
            case CRUSTY_INSTRUCTION_TYPE_JUMP:
            case CRUSTY_INSTRUCTION_TYPE_JUMPN:
            case CRUSTY_INSTRUCTION_TYPE_JUMPZ:
            case CRUSTY_INSTRUCTION_TYPE_JUMPL:
            case CRUSTY_INSTRUCTION_TYPE_JUMPG:
                cvm->inst[cvm->line[j].instruction + JUMP_LOCATION] =
                cvm->line[cvm->inst[cvm->line[j].instruction + JUMP_LOCATION]].instruction;
            default:
                break;
        }
    }

    return(0);
}

#undef JUMP_INSTRUCTION
#undef MATH_INSTRUCTION

/* do a lot of checking now so a lot can be skipped later when actually
   executing. */
static int check_move_arg(CrustyVM *cvm,
                          int dest,
                          int flags,
                          int val,
                          int index) {
    if((flags & MOVE_FLAG_TYPE_MASK) == MOVE_FLAG_IMMEDIATE) {
        if(dest) {
            LOG_PRINTF_LINE(cvm, "Destination flagged as immediate.\n");
            return(-1);
        }

#ifdef CRUSTY_TEST
        LOG_PRINTF_BARE(cvm, "%d", val);
#endif
    } else if((flags & MOVE_FLAG_TYPE_MASK) == MOVE_FLAG_LENGTH) {
        if(dest) {
            LOG_PRINTF_LINE(cvm, "Destination flagged as array length.\n");
            return(-1);
        }

        if(val < 0 || val > (int)(cvm->vars) - 1) {
            LOG_PRINTF_LINE(cvm, "Var out of range (%d).\n", val);
            return(-1);
        }

#ifdef CRUSTY_TEST
        LOG_PRINTF_BARE(cvm, "%d(%s):", val, cvm->var[val].name);
#endif
    } else if((flags & MOVE_FLAG_TYPE_MASK) == MOVE_FLAG_VAR) {
        if(val < 0 || val > (int)(cvm->vars) - 1) {
            LOG_PRINTF_LINE(cvm, "Var out of range (%d).\n", val);
            return(-1);
        }

        if(index < 0) {
            LOG_PRINTF_LINE(cvm, "Negative index %d.\n", index);
            return(-1);
        }

        if(dest) {
            if(cvm->var[val].read != NULL && cvm->var[val].write == NULL) {
                LOG_PRINTF_LINE(cvm, "Read only callback variable as "
                                     "destination (%s).\n", cvm->var[val].name);
                return(-1);
            }
        } else {
            if(cvm->var[val].write != NULL && cvm->var[val].read == NULL) {
                LOG_PRINTF_LINE(cvm, "Write only callback variable as "
                                     "source (%s).\n", cvm->var[val].name);
                return(-1);
            }
        }

        if((flags & MOVE_FLAG_INDEX_TYPE_MASK) == MOVE_FLAG_INDEX_VAR) {
            if(index < 0 || index > (int)(cvm->vars) - 1) {
                LOG_PRINTF_LINE(cvm, "Index var out of range (%d).\n", index);
                return(-1);
            }

            if(cvm->var[index].write != NULL && cvm->var[index].read == NULL) {
                LOG_PRINTF_LINE(cvm, "Write only callback variable "
                                     "as index (%s).\n", cvm->var[val].name);
                return(-1);
            }

#ifdef CRUSTY_TEST
            LOG_PRINTF_BARE(cvm, "%d(%s):%d(%s)",
                            val, cvm->var[val].name,
                            index, cvm->var[index].name);
#endif
        } else if((flags & MOVE_FLAG_INDEX_TYPE_MASK) == MOVE_FLAG_INDEX_IMMEDIATE) {
            if(index < 0 ||
               (cvm->var[val].length > 0 &&
                index > (int)(cvm->var[val].length) - 1)) {
                LOG_PRINTF_LINE(cvm, "Index out of range %d.\n", index);
                return(-1);
            }

#ifdef CRUSTY_TEST
            LOG_PRINTF_BARE(cvm, "%d(%s):%d", val, cvm->var[val].name, index);
#endif
        }
    } else {
        LOG_PRINTF_LINE(cvm, "Invalid variable type.\n");
        return(-1);
    }

    return(0);
}

static int check_math_instruction(CrustyVM *cvm,
                                  const char *name,
                                  unsigned int i,
                                  unsigned int notcmp) {
    if(i + MOVE_ARGS > cvm->insts - 1) {
        LOG_PRINTF_LINE(cvm, "Instruction memory ends before end "
                             "of %s instruction.\n", name);
        return(-1);
    }

#ifdef CRUSTY_TEST
    LOG_PRINTF_BARE(cvm, "%s ", name);
#endif
    if(check_move_arg(cvm,
                      notcmp,
                      cvm->inst[i+MOVE_DEST_FLAGS],
                      cvm->inst[i+MOVE_DEST_VAL],
                      cvm->inst[i+MOVE_DEST_INDEX]) < 0) {
        return(-1);
    }
#ifdef CRUSTY_TEST
    LOG_PRINTF_BARE(cvm, " ");
#endif
    if(check_move_arg(cvm,
                      0,
                      cvm->inst[i+MOVE_SRC_FLAGS],
                      cvm->inst[i+MOVE_SRC_VAL],
                      cvm->inst[i+MOVE_SRC_INDEX]) < 0) {
        return(-1);
    }
#ifdef CRUSTY_TEST
    LOG_PRINTF_BARE(cvm, "\n");
#endif
    return(0);
}

#define MATH_INSTRUCTION(NAME, NOTCMP) \
    if(check_math_instruction(cvm, NAME, i, (NOTCMP)) < 0) { \
        return(-1); \
    }

static int check_jump_instruction(CrustyVM *cvm,
                                  const char *name,
                                  CrustyProcedure *proc,
                                  unsigned int i) {
    unsigned int j;
    unsigned int line;
    unsigned int found;

    if(i + JUMP_ARGS > cvm->insts - 1) {
        LOG_PRINTF_LINE(cvm, "Instruction memory ends before end "
                             "of %s instruction.\n", name);
        return(-1);
    }

    found = 0;
    for(j = 0; j < cvm->lines; j++) {
        if(cvm->inst[i+JUMP_LOCATION] < 0 ) {
            LOG_PRINTF_LINE(cvm, "Negative jump pointer?\n");
            return(-1);
        }

        if((unsigned int)(cvm->inst[i+JUMP_LOCATION]) == cvm->line[j].instruction) {
            line = j;
            found = 1;
            break;
        }
    }

    if(found == 0) {
        LOG_PRINTF_LINE(cvm, "Jump argument doesn't land on an instruction.\n");
        return(-1);
    }

    if(proc != NULL) {
        if(line < proc->start || line > proc->start + proc->length) {
            LOG_PRINTF_LINE(cvm, "Jump outside of procedure.\n");
            return(-1);
        }
    }

#ifdef CRUSTY_TEST
    LOG_PRINTF_BARE(cvm, "%s %d\n", name, cvm->inst[i+JUMP_LOCATION]);
#endif
    return(0);
}

#define JUMP_INSTRUCTION(NAME) \
    if(proc == NULL) { \
        if(check_jump_instruction(cvm, NAME, NULL, i) < 0) { \
            return(-1); \
        } \
    } else { \
        if(check_jump_instruction(cvm, NAME, *proc, i) < 0) { \
            return(-1); \
        } \
    }

static int check_instruction(CrustyVM *cvm,
                      CrustyProcedure **proc,
                      unsigned int i) {
#ifdef CRUSTY_TEST
    LOG_PRINTF(cvm, "%d: ", i);
#endif
    switch(cvm->inst[i]) {
        case CRUSTY_INSTRUCTION_TYPE_MOVE:
            MATH_INSTRUCTION("move", 1)
            return(MOVE_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_ADD:
            MATH_INSTRUCTION("add", 1)
            return(MOVE_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_SUB:
            MATH_INSTRUCTION("sub", 1)
            return(MOVE_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_MUL:
            MATH_INSTRUCTION("mul", 1)
            return(MOVE_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_DIV:
            MATH_INSTRUCTION("div", 1)
            return(MOVE_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_MOD:
            MATH_INSTRUCTION("mod", 1)
            return(MOVE_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_AND:
            MATH_INSTRUCTION("and", 1)
            return(MOVE_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_OR:
            MATH_INSTRUCTION("or", 1)
            return(MOVE_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_XOR:
            MATH_INSTRUCTION("xor", 1)
            return(MOVE_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_SHR:
            MATH_INSTRUCTION("shr", 1)
            return(MOVE_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_SHL:
            MATH_INSTRUCTION("shl", 1)
            return(MOVE_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_CMP:
            MATH_INSTRUCTION("cmp", 0)
            return(MOVE_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_JUMP:
            JUMP_INSTRUCTION("jump")
            return(JUMP_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_JUMPN:
            JUMP_INSTRUCTION("jumpn")
            return(JUMP_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_JUMPZ:
            JUMP_INSTRUCTION("jumpz")
            return(JUMP_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_JUMPL:
            JUMP_INSTRUCTION("jumpl")
            return(JUMP_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_JUMPG:
            JUMP_INSTRUCTION("jumpg")
            return(JUMP_ARGS + 1);
        case CRUSTY_INSTRUCTION_TYPE_CALL:
            if(i + JUMP_ARGS > cvm->insts - 1) {
                LOG_PRINTF_LINE(cvm, "Instruction memory ends before end "
                                     "of call instruction.\n");
                return(-1);
            }

            if(cvm->inst[i+CALL_PROCEDURE] < 0 ||
               cvm->inst[i+CALL_PROCEDURE] > (int)(cvm->procs) - 1) {
                LOG_PRINTF_LINE(cvm, "Call to procedure out of range.\n");
                return(-1);
            }

            CrustyProcedure *callProc = &(cvm->proc[cvm->inst[i+CALL_PROCEDURE]]);
            unsigned int j;

#ifdef CRUSTY_TEST
            LOG_PRINTF_BARE(cvm, "call %d(%s)", cvm->inst[i+CALL_PROCEDURE],
                            callProc->name);
#endif

            for(j = 0; j < callProc->args; j++) {
#ifdef CRUSTY_TEST
                LOG_PRINTF_BARE(cvm, " ");
#endif
                if(check_move_arg(cvm,
                                  0,
                                  cvm->inst[i + CALL_START_ARGS + (j * CALL_ARG_SIZE) + CALL_ARG_FLAGS],
                                  cvm->inst[i + CALL_START_ARGS + (j * CALL_ARG_SIZE) + CALL_ARG_VAL],
                                  cvm->inst[i + CALL_START_ARGS + (j * CALL_ARG_SIZE) + CALL_ARG_INDEX]) < 0) {
                    return(-1);
                }
            }

#ifdef CRUSTY_TEST
            LOG_PRINTF_BARE(cvm, "\n");
#endif

            return(CALL_PROCEDURE + (callProc->args * 3) + 1);
        case CRUSTY_INSTRUCTION_TYPE_RET:
            /* takes no arguments so it can't end early */
#ifdef CRUSTY_TEST
            LOG_PRINTF_BARE(cvm, "ret\n");
#endif
            if(proc != NULL) {
                *proc = NULL;
            }

            return(RET_ARGS + 1);
        default:
            LOG_PRINTF_LINE(cvm, "Invalid instruction %u.\n", cvm->inst[i]);
            return(-1);
    }
}

#undef JUMP_INSTRUCTION
#undef MATH_INSTRUCTION

static int codeverify(CrustyVM *cvm) {
    CrustyProcedure *curproc = NULL;
    int procnum = 0;
    int instsize;
    unsigned int i = 0;
    cvm->logline = 0;

    while(i < cvm->insts) {
        if(curproc == NULL) {
            if(cvm->logline == cvm->proc[procnum].start) {
                curproc = &(cvm->proc[procnum]);
                procnum++;
#ifdef CRUSTY_TEST
                LOG_PRINTF(cvm, "proc %s\n", curproc->name);
#endif
            } else {
                LOG_PRINTF_LINE(cvm, "BUG: code line not in a procedure.\n");
                check_instruction(cvm, NULL, i);
                return(-1);
            }
        }

        instsize = check_instruction(cvm, &curproc, i);
        if(instsize < 0) {
            return(-1);
        }

        i += instsize;
        cvm->logline++;
    }

    if(curproc != NULL) {
        LOG_PRINTF_LINE(cvm, "Procedure without ret?\n");
        return(-1);
    }

    return(0);
}

int crustyvm_reset(CrustyVM *cvm) {
    const char *temp = cvm->stage;

    cvm->stage = "reset";
#ifdef CRUSTY_TEST
    LOG_PRINTF(cvm, "Start\n");
#endif

    memcpy(cvm->stack, cvm->initializer, cvm->initialstack);

    cvm->status = CRUSTY_STATUS_READY;
    cvm->stage = temp;

    return(0);
}

#ifdef CRUSTY_TEST
static int write_lines(CrustyVM *cvm, const char *name) {
    FILE *out;
    unsigned int i, j;
    char *temp;

    out = fopen(name, "wb");
    if(out == NULL) {
        LOG_PRINTF(cvm, "Couldn't open file %s for writing.\n", name);
        return(-1);
    }

    for(i = 0; i < cvm->lines; i++) {
        for(j = 0; j < cvm->line[i].tokencount; j++) {
            if(fprintf(out, "%s", GET_TOKEN(i, j)) < 0) {
                LOG_PRINTF(cvm, "Couldn't write to file.\n");
                return(-1);
            }
            if(j < cvm->line[i].tokencount - 1) {
                if(fprintf(out, " ") < 0) {
                    LOG_PRINTF(cvm, "Couldn't write to file.\n");
                    return(-1);
                }
            }
        }
        if(fprintf(out, "\n") < 0) {
            LOG_PRINTF(cvm, "Couldn't write to file.\n");
            return(-1);
        }
    }

    fclose(out);

    return(0);
}
#endif

CrustyVM *crustyvm_new(const char *name,
                       char *safepath,
                       const char *program,
                       long len,
                       unsigned int flags,
                       unsigned int callstacksize,
                       const CrustyCallback *cb,
                       unsigned int cbcount,
                       const char **var,
                       const char **value,
                       unsigned int vars,
                       void (*log_cb)(void *priv, const char *fmt, ...),
                       void *log_priv) {
    CrustyVM *cvm;
    int foundmacro;
    unsigned int i, j;
    char namebuffer[] = "preprocess###.cvm";
    long tokenstart;
    unsigned long *varOffset;
    unsigned long *valueOffset;

#ifdef CRUSTY_TEST
    unsigned int k;
#endif

    if(name == NULL) {
        /* nothing is defined yet so don't use the macro */
        log_cb(log_priv, "NULL passed as program name.\n");
        return(NULL);
    }

    cvm = init();
    if(cvm == NULL) {
        return(NULL);
    }

    cvm->flags = flags;

    cvm->log_cb = log_cb;
    cvm->log_priv = log_priv;

    cvm->stage = "tokenize";
#ifdef CRUSTY_TEST
    LOG_PRINTF(cvm, "Start\n");
#endif

    if(tokenize(cvm, name, safepath, program, len) < 0) {
        crustyvm_free(cvm);
        return(NULL);
    }

#ifdef CRUSTY_TEST
    if(cvm->flags & CRUSTY_FLAG_OUTPUT_PASSES) {
    /* this outputs ugly nonsense that's full of nulls and other crap and not
       really useful outside of debugging the program itself. */
        FILE *out;
        out = fopen("tokens.bin", "wb");
        if(out == NULL) {
            LOG_PRINTF(cvm, "Failed to open tokenizer output file for writing.\n");
            crustyvm_free(cvm);
            return(NULL);
        }

        if(fwrite(cvm->tokenmem, 1, cvm->tokenmemlen, out) <
           (unsigned long)cvm->tokenmemlen) {
            LOG_PRINTF(cvm, "Failed to write tokenizer output.\n");
            crustyvm_free(cvm);
            return(NULL);
        }
        fclose(out);
    }
#endif

    if(cvm->lines == 0) {
        LOG_PRINTF(cvm, "No lines remain after pass.\n");
        crustyvm_free(cvm);
        return(NULL);
    }

#ifdef CRUSTY_TEST
    if(cvm->flags & CRUSTY_FLAG_OUTPUT_PASSES) {
        FILE *out;
        out = fopen("tokens_meta.txt", "wb");
        if(out == NULL) {
            LOG_PRINTF(cvm, "Failed to open tokenizer metadata output file for writing.\n");
            crustyvm_free(cvm);
            return(NULL);
        }

        for(i = 0; i < cvm->lines; i++) {
            fprintf(out, "%s %04u ",
                    TOKENVAL(cvm->line[i].moduleOffset),
                    cvm->line[i].line);
            for(j = 0; j < cvm->line[i].tokencount; j++) {
                fprintf(out, "%s", GET_TOKEN(i, j));
                if(j < cvm->line[i].tokencount - 1) {
                    fprintf(out, " ");
                }
            }
            fprintf(out, "\n");
        }
        fclose(out);
    }

    if(cvm->flags & CRUSTY_FLAG_OUTPUT_PASSES) {
        if(write_lines(cvm, "tokenize.cvm", 1) < 0) {
            LOG_PRINTF(cvm, "Failed to write tokenize pass.\n");
            crustyvm_free(cvm);
            return(NULL);
        }
    }
#endif

    cvm->stage = "input variables";
#ifdef CRUSTY_TEST
    LOG_PRINTF(cvm, "Start\n");
#endif

    varOffset = malloc(sizeof(long) * vars);
    if(varOffset == NULL) {
        LOG_PRINTF(cvm, "Failed to allocate memory for input variables list.\n");
        crustyvm_free(cvm);
        return(NULL);
    }
    valueOffset = malloc(sizeof(long) * vars);
    if(valueOffset == NULL) {
        LOG_PRINTF(cvm, "Failed to allocate memory for input values list.\n");
        crustyvm_free(cvm);
        return(NULL);
    }

    for(i = 0; i < vars; i++) {
        tokenstart = add_token(cvm, var[i], strlen(var[i]), 0, NULL);
        if(tokenstart < 0) {
            LOG_PRINTF(cvm, "Failed to allocate memory for input variable.\n");
            crustyvm_free(cvm);
            return(NULL);
        }
        varOffset[i] = tokenstart;
        tokenstart = add_token(cvm, value[i], strlen(value[i]), 0, NULL);
        if(tokenstart < 0) {
            LOG_PRINTF(cvm, "Failed to allocate memory for input value.\n");
            crustyvm_free(cvm);
            return(NULL);
        }
        valueOffset[i] = tokenstart;
    }

#ifdef CRUSTY_TEST
    for(i = 0; i < vars; i++) {
        LOG_PRINTF(cvm, "%s = %s\n",
                   TOKENVAL(varOffset[i]),
                   TOKENVAL(valueOffset[i]));
    }
#endif

    for(i = 0; i < MAX_PASSES; i++) {
        snprintf(namebuffer, sizeof(namebuffer), "preprocess %d", i + 1);
        cvm->stage = namebuffer;
#ifdef CRUSTY_TEST
        LOG_PRINTF(cvm, "Start\n", i);
#endif

        foundmacro = preprocess(cvm, varOffset, valueOffset, vars);
        if(foundmacro < 0) {
            LOG_PRINTF(cvm, "Failed preprocess at pass %d.\n", i + 1);
            crustyvm_free(cvm);
            return(NULL);
        }

        if(cvm->lines == 0) {
            LOG_PRINTF(cvm, "No lines remain after pass.\n");
            crustyvm_free(cvm);
            return(NULL);
        }

#ifdef CRUSTY_TEST
        if(cvm->flags & CRUSTY_FLAG_OUTPUT_PASSES) {
            snprintf(namebuffer, sizeof(namebuffer), "preprocess%03d.cvm", i + 1);
            if(write_lines(cvm, namebuffer, 1) < 0) {
                LOG_PRINTF(cvm, "Failed to write pass %d.\n", i + 1);
                crustyvm_free(cvm);
                return(NULL);
            }
        }
#endif

        if(foundmacro == 0) {
            break;
        }
    }
    free(varOffset);
    free(valueOffset);
    if(i == MAX_PASSES) {
        LOG_PRINTF(cvm, "Preprocess passes exceeded.\n");
        crustyvm_free(cvm);
        return(NULL);
    }

    cvm->stage = "adding callbacks";
#ifdef CRUSTY_TEST
    LOG_PRINTF(cvm, "Start\n");
#endif

    for(i = 0; i < cbcount; i++) {
        if(cb[i].read == NULL && cb[i].write == NULL) {
            LOG_PRINTF(cvm, "Callback variables must have a non-NULL read and/or write function.\n");
            crustyvm_free(cvm);
            return(NULL);
        }

        tokenstart = add_token(cvm, cb[i].name, strlen(cb[i].name), 0, NULL);
        if(tokenstart < 0) {
            LOG_PRINTF(cvm, "Failed to allocate memory for callback name.\n");
            crustyvm_free(cvm);
            return(NULL);
        }
        /* callback variables have 0 length and read or write must be non-NULL */
        if(new_variable(cvm,
                        tokenstart,
                        cb[i].readType,
                        cb[i].length,
                        NULL,
                        &(cb[i]),
                        -1) < 0) {
            /* reason will have already been printed */
            crustyvm_free(cvm);
            return(NULL);
        }
    }

    cvm->stage = "symbols scan";
#ifdef CRUSTY_TEST
    LOG_PRINTF(cvm, "Start\n");
#endif

    if(symbols_scan(cvm, safepath) < 0) {
        LOG_PRINTF(cvm, "Symbols scan failed.\n");
        crustyvm_free(cvm);
        return(NULL);
    }

    if(cvm->lines == 0) {
        LOG_PRINTF(cvm, "No lines remain after pass.\n");
        crustyvm_free(cvm);
        return(NULL);
    }

    for(i = 0; i < cvm->procs; i++) {
        cvm->proc[i].name = TOKENVAL(cvm->proc[i].nameOffset);
        for(j = 0; j < cvm->proc[i].labels; j++) {
            cvm->proc[i].label[j].name =
                TOKENVAL(cvm->proc[i].label[j].nameOffset);
        }
    }

    for(i = 0; i < cvm->vars; i++) {
        cvm->var[i].name = TOKENVAL(cvm->var[i].nameOffset);
    }

#ifdef CRUSTY_TEST
    /* output a text file because it is no longer a valid cvm source file */
    if(cvm->flags & CRUSTY_FLAG_OUTPUT_PASSES) {
        if(write_lines(cvm, "symbols scan.txt", 0) < 0) {
            LOG_PRINTF(cvm, "Failed to write tokenize pass.\n");
            crustyvm_free(cvm);
            return(NULL);
        }
    }

    cvm->stage = "symbols list";
    LOG_PRINTF(cvm, "Global Variables:\n");
    for(i = 0; i < cvm->vars; i++) {
        if(variable_is_global(&(cvm->var[i]))) {
            LOG_PRINTF(cvm, " %s", cvm->var[i].name);
            if(cvm->var[i].read != NULL) {
                LOG_PRINTF_BARE(cvm, " r");
            }
            if(cvm->var[i].write != NULL) {
                LOG_PRINTF_BARE(cvm, " w");
            }
            LOG_PRINTF_BARE(cvm, "\n");
            if(cvm->var[i].length > 0) {
                if(cvm->var[i].type == CRUSTY_TYPE_CHAR) {
                    LOG_PRINTF(cvm, "  String initializer: \"");
                    for(j = 0; j < cvm->var[i].length; j++) {
                        LOG_PRINTF_BARE(cvm, "%c",
                            ((char *)&(cvm->initializer[cvm->var[i].offset]))[j]);
                    }
                    LOG_PRINTF_BARE(cvm, "\"");
                }
                if(cvm->var[i].type == CRUSTY_TYPE_INT) {
                    LOG_PRINTF(cvm, "  Integer initializer:");
                    for(j = 0; j < cvm->var[i].length; j++) {
                        LOG_PRINTF_BARE(cvm, " %d",
                            ((int *)&(cvm->initializer[cvm->var[i].offset]))[j]);
                    }
                }
                if(cvm->var[i].type == CRUSTY_TYPE_FLOAT) {
                    LOG_PRINTF(cvm, "  Float initializer:");
                    for(j = 0; j < cvm->var[i].length; j++) {
                        LOG_PRINTF_BARE(cvm, " %g",
                            ((double *)&(cvm->initializer[cvm->var[i].offset]))[j]);
                    }
                }
                LOG_PRINTF_BARE(cvm, "\n");
            }
        }
    }
    for(i = 0; i < cvm->procs; i++) {
        LOG_PRINTF(cvm, "Procedure: %s @%u, %u, args: %u\n", cvm->proc[i].name,
                                                             cvm->proc[i].start,
                                                             cvm->proc[i].length,
                                                             cvm->proc[i].args);
        if(cvm->proc[i].vars > 0) {
            LOG_PRINTF(cvm, " Variables:\n");
            for(j = 0; j < cvm->proc[i].vars; j++) {
                LOG_PRINTF(cvm, "  %s", cvm->proc[i].var[j]->name);
                if(j < cvm->proc[i].args) {
                    LOG_PRINTF_BARE(cvm, " arg %u\n", j);
                } else {
                    LOG_PRINTF_BARE(cvm, "\n");
                }
                if(cvm->proc[i].var[j]->length > 0) {
                    if(cvm->proc[i].var[j]->type == CRUSTY_TYPE_CHAR) {
                        LOG_PRINTF(cvm, "   String initializer: \"");
                        for(k = 0; k < cvm->proc[i].var[j]->length; k++) {
                            LOG_PRINTF_BARE(cvm, "%c",
                                ((char *)&(cvm->initializer[cvm->proc[i].var[j].offset]))[k]);
                        }
                        LOG_PRINTF_BARE(cvm, "\"");
                    }
                    if(cvm->proc[i].var[j]->type == CRUSTY_TYPE_INT) {
                        LOG_PRINTF(cvm, "   Integer initializer:");
                        for(k = 0; k < cvm->proc[i].var[j]->length; k++) {
                            LOG_PRINTF_BARE(cvm, "%c",
                                ((int *)&(cvm->initializer[cvm->proc[i].var[j].offset]))[k]);
                        }
                    }
                    if(cvm->proc[i].var[j]->type == CRUSTY_TYPE_FLOAT) {
                        LOG_PRINTF(cvm, "   Float initializer:");
                        for(k = 0; k < cvm->proc[i].var[j]->length; k++) {
                            LOG_PRINTF_BARE(cvm, "%c",
                                ((float *)&(cvm->initializer[cvm->proc[i].var[j].offset]))[k]);
                        }
                    }
                    LOG_PRINTF_BARE(cvm, "\n");
                }
                if(cvm->proc[i].var[j]->proc != &(cvm->proc[i])) {
                    LOG_PRINTF(cvm, "   Improperly pointed procedure!\n");
                }
            }
        }
        if(cvm->proc[i].labels > 0) {
            LOG_PRINTF(cvm, " Labels:\n");
            for(j = 0; j < cvm->proc[i].labels; j++) {
                LOG_PRINTF(cvm, "  %s @%u\n",
                    cvm->proc[i].label[j].name,
                    cvm->proc[i].label[j].line);
            }
        }
    }
#endif

    cvm->stage = "symbols verification";
#ifdef CRUSTY_TEST
    LOG_PRINTF(cvm, "Start\n");
#endif

    if(symbols_verify(cvm) < 0) {
        LOG_PRINTF(cvm, "Symbols verification failed.\n");
        crustyvm_free(cvm);
        return(NULL);
    }

    cvm->stage = "code generation";
#ifdef CRUSTY_TEST
    LOG_PRINTF(cvm, "Start\n");
#endif

    if(codegen(cvm) < 0) {
        LOG_PRINTF(cvm, "Code generation failed.\n");
        crustyvm_free(cvm);
        return(NULL);
    }

    cvm->stage = "code verification";
#ifdef CRUSTY_TEST
    LOG_PRINTF(cvm, "Start\n");
#endif

    if(codeverify(cvm) < 0) {
        LOG_PRINTF(cvm, "Code verification failed.\n");
        crustyvm_free(cvm);
        return(NULL);
    }

    cvm->stage = "memory allocation";
#ifdef CRUSTY_TEST
    LOG_PRINTF(cvm, "Start\n");
#endif

    cvm->stack = malloc(cvm->stacksize);
    if(cvm->stack == NULL) {
        LOG_PRINTF(cvm, "Failed to allocate stack memory.\n");
        crustyvm_free(cvm);
        return(NULL);
    }

    if(callstacksize == 0) {
        cvm->callstacksize = DEFAULT_CALLSTACK_SIZE;
    } else {
        cvm->callstacksize = callstacksize;
    }

    cvm->cstack = malloc(sizeof(CrustyCallStackArg) * cvm->callstacksize);
    if(cvm->cstack == NULL) {
        LOG_PRINTF(cvm, "Failed to allocate callstack memory.\n");
        crustyvm_free(cvm);
        return(NULL);
    }

    if(crustyvm_reset(cvm) < 0) {
        crustyvm_free(cvm);
        return(NULL);
    }

    return(cvm);
}

static int read_var(CrustyVM *cvm,
                    int *intval,
                    double *floatval,
                    int ptr,
                    CrustyVariable *var,
                    unsigned int index) {
    if(var->read != NULL) {
        if(var->type == CRUSTY_TYPE_CHAR) {
            /* the function will assume only 1 byte of storage so make sure it
             * is all clear. */
            *intval = 0;
            return(var->read(var->readpriv, intval, index));
        } else if(var->type == CRUSTY_TYPE_FLOAT) {
            return(var->read(var->readpriv, floatval, index));
        } else { /* INT */
            return(var->read(var->readpriv, intval, index));
        }
    }

    if(var->type == CRUSTY_TYPE_CHAR) {
        *intval = (int)(cvm->stack[ptr + index]);
    } else if(var->type == CRUSTY_TYPE_FLOAT) {
        *floatval = *((double *)(&(cvm->stack[ptr + (index * sizeof(double))])));
    } else { /* INT */
        *intval = *((int *)(&(cvm->stack[ptr + (index * sizeof(int))])));
    }

    return(0);
}

static void write_var(CrustyVM *cvm,
                      int intval,
                      double floatval,
                      int ptr,
                      CrustyVariable *var,
                      unsigned int index) {
    if(var->type == CRUSTY_TYPE_CHAR) {
        cvm->stack[ptr + index] = ((unsigned char)intval);
    } else if(var->type == CRUSTY_TYPE_FLOAT) {
        *((double *)(&(cvm->stack[ptr + (index * sizeof(double))]))) = floatval;
    } else { /* INT */
        *((int *)(&(cvm->stack[ptr + (index * sizeof(int))]))) = intval;
    }
}

#define STACK_ARG(STACKSTART, IDX) \
    ((CrustyStackArg *)(&(cvm->stack[(STACKSTART) - \
                                     (sizeof(CrustyStackArg) * (IDX))])))

#define GET_PTR(VAR, SP) \
    (variable_is_global(&(cvm->var[VAR])) ? \
        (cvm->var[VAR].offset) : \
        ((SP) - cvm->var[VAR].offset))

/* see variable permutations.txt for more information on what these do */

/* only returns an index to another variable (which may contain a float) or an
   integer */
static int update_src_ref(CrustyVM *cvm,
                          int *flags,
                          int *val,
                          int *index,
                          int *ptr) {
    if((*flags & MOVE_FLAG_TYPE_MASK) == MOVE_FLAG_VAR) {
        if(variable_is_argument(&(cvm->var[*val]))) {
            if((STACK_ARG(cvm->sp, cvm->var[*val].offset)->flags &
                MOVE_FLAG_TYPE_MASK) ==
               MOVE_FLAG_VAR) {
                if((*flags & MOVE_FLAG_INDEX_TYPE_MASK) == MOVE_FLAG_INDEX_VAR) {
                    if(variable_is_argument(&(cvm->var[*index]))) {
                        if((STACK_ARG(cvm->sp, cvm->var[*index].offset)->flags &
                           MOVE_FLAG_TYPE_MASK) == MOVE_FLAG_VAR) {
                            if(cvm->var[STACK_ARG(cvm->sp,
                                                  cvm->var[*index].offset)->val].type ==
                               CRUSTY_TYPE_FLOAT) {
                                cvm->status = CRUSTY_STATUS_FLOAT_INDEX;
                                return(-1);
                            }

                            if(read_var(cvm,
                                        index,
                                        NULL,
                                        STACK_ARG(cvm->sp, cvm->var[*index].offset)->ptr,
                                        &(cvm->var[STACK_ARG(cvm->sp,
                                                             cvm->var[*index].offset)->val]),
                                        STACK_ARG(cvm->sp,
                                                  cvm->var[*index].offset)->index) < 0) {
                                cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                                return(-1);
                            }
                        } else {
                            *index = STACK_ARG(cvm->sp, cvm->var[*index].offset)->val;
                        }
                    } else {
                        if(cvm->var[*index].type == CRUSTY_TYPE_FLOAT) {
                            cvm->status = CRUSTY_STATUS_FLOAT_INDEX;
                            return(-1);
                        }

                        if(read_var(cvm,
                                    index,
                                    NULL,
                                    GET_PTR(*index, *ptr),
                                    &(cvm->var[*index]),
                                    0) < 0) {
                            cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                            return(-1);
                        }
                    }
                } /* else {
                    do nothing
                } */
                if(*index < 0) {
                    cvm->status = CRUSTY_STATUS_OUT_OF_RANGE;
                    return(-1);
                }

                *index += STACK_ARG(cvm->sp, cvm->var[*val].offset)->index;
                if(*index >
                   (int)(cvm->var[STACK_ARG(cvm->sp, 
                                            cvm->var[*val].offset)->val].length -
                   1)) {
                    cvm->status = CRUSTY_STATUS_OUT_OF_RANGE;
                    return(-1);
                }

                *flags = MOVE_FLAG_VAR;
                *ptr = STACK_ARG(cvm->sp, cvm->var[*val].offset)->ptr;
                *val = STACK_ARG(cvm->sp, cvm->var[*val].offset)->val;
                /* index is already updated */
            } else {
                if((*flags & MOVE_FLAG_INDEX_TYPE_MASK) == MOVE_FLAG_INDEX_VAR) {
                    if(variable_is_argument(&(cvm->var[*index]))) {
                        if((STACK_ARG(cvm->sp, cvm->var[*index].offset)->flags &
                            MOVE_FLAG_TYPE_MASK) ==
                           MOVE_FLAG_VAR) {
                            if(cvm->var[STACK_ARG(cvm->sp,
                                                  cvm->var[*index].offset)->val].type ==
                               CRUSTY_TYPE_FLOAT) {
                                cvm->status = CRUSTY_STATUS_FLOAT_INDEX;
                                return(-1);
                            }

                            if(read_var(cvm,
                                        index,
                                        NULL,
                                        STACK_ARG(cvm->sp, cvm->var[*index].offset)->ptr,
                                        &(cvm->var[STACK_ARG(cvm->sp,
                                                             cvm->var[*index].offset)->val]),
                                        STACK_ARG(cvm->sp,
                                                  cvm->var[*index].offset)->index) < 0) {
                                cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                                return(-1);
                            }
                        } else {
                            *index = STACK_ARG(cvm->sp, cvm->var[*index].offset)->val;
                        }
                    } else {
                        if(cvm->var[*index].type == CRUSTY_TYPE_FLOAT) {
                            cvm->status = CRUSTY_STATUS_FLOAT_INDEX;
                            return(-1);
                        }

                        if(read_var(cvm,
                                    index,
                                    NULL,
                                    GET_PTR(*index, *ptr),
                                    &(cvm->var[*index]),
                                    0) < 0) {
                            cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                            return(-1);
                        }
                    }
                } /* else {
                    do nothing
                } */
                if(*index < 0 || *index > 0) {
                    cvm->status = CRUSTY_STATUS_OUT_OF_RANGE;
                    return(-1);
                }

                *flags = MOVE_FLAG_IMMEDIATE;
                *val = STACK_ARG(cvm->sp, cvm->var[*val].offset)->val;
            }
        } else {
            if((*flags & MOVE_FLAG_INDEX_TYPE_MASK) == MOVE_FLAG_INDEX_VAR) {
                if(variable_is_argument(&(cvm->var[*index]))) {
                    if((STACK_ARG(cvm->sp, cvm->var[*index].offset)->flags &
                       MOVE_FLAG_TYPE_MASK) == MOVE_FLAG_VAR) {
                        if(cvm->var[STACK_ARG(cvm->sp,
                                              cvm->var[*index].offset)->val].type ==
                           CRUSTY_TYPE_FLOAT) {
                            cvm->status = CRUSTY_STATUS_FLOAT_INDEX;
                            return(-1);
                        }

                        if(read_var(cvm,
                                    index,
                                    NULL,
                                    STACK_ARG(cvm->sp, cvm->var[*index].offset)->ptr,
                                    &(cvm->var[STACK_ARG(cvm->sp,
                                                         cvm->var[*index].offset)->val]),
                                    STACK_ARG(cvm->sp,
                                              cvm->var[*index].offset)->index) < 0) {
                            cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                            return(-1);
                        }
                    } else {
                        *index = STACK_ARG(cvm->sp, cvm->var[*index].offset)->val;
                    }
                } else {
                    if(cvm->var[*index].type == CRUSTY_TYPE_FLOAT) {
                        cvm->status = CRUSTY_STATUS_FLOAT_INDEX;
                        return(-1);
                    }

                    if(read_var(cvm,
                                index,
                                NULL,
                                GET_PTR(*index, *ptr),
                                &(cvm->var[*index]),
                                0) < 0) {
                        cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                        return(-1);
                    }
                }
            } /* else {
                do nothing
            } */
            if(*index < 0 || *index > (int)(cvm->var[*val].length - 1)) {
                cvm->status = CRUSTY_STATUS_OUT_OF_RANGE;
                return(-1);
            }

            *flags = MOVE_FLAG_VAR;
            /* val and index are already correct */
            *ptr = GET_PTR(*val, *ptr);
        }
    } else if((*flags & MOVE_FLAG_TYPE_MASK) == MOVE_FLAG_LENGTH) {
        if(variable_is_argument(&(cvm->var[*val]))) {
            if((STACK_ARG(cvm->sp, cvm->var[*val].offset)->flags &
                MOVE_FLAG_TYPE_MASK) ==
               MOVE_FLAG_VAR) {
                *flags = MOVE_FLAG_IMMEDIATE;
                *index = STACK_ARG(cvm->sp, cvm->var[*val].offset)->index;
                *val = cvm->var[STACK_ARG(cvm->sp,
                                          cvm->var[*val].offset)->val].length -
                       *index;
            } else {
                *flags = MOVE_FLAG_IMMEDIATE;
                *val = 1;
            }
        } else {
            *flags = MOVE_FLAG_IMMEDIATE;
            *val = cvm->var[*val].length;
        }
    } else if((*flags & MOVE_FLAG_TYPE_MASK) == MOVE_FLAG_IMMEDIATE) {
        *flags = MOVE_FLAG_IMMEDIATE;
        /* val is already val */
    } else {
        cvm->status = CRUSTY_STATUS_INTERNAL_ERROR;
        return(-1);
    }

    return(0);
}

static int call(CrustyVM *cvm, unsigned int procindex, unsigned int argsindex) {
    unsigned int i;
    unsigned int newsp;
    CrustyProcedure *callee;
    int flags, val, index, ptr;

    if(cvm->csp == cvm->callstacksize) {
        cvm->status = CRUSTY_STATUS_STACK_OVERFLOW;
        return(-1);
    }

    callee = &(cvm->proc[procindex]);
    newsp = cvm->sp + callee->stackneeded;

    if(newsp > cvm->stacksize) {
        cvm->status = CRUSTY_STATUS_STACK_OVERFLOW;
        return(-1);
    }

    /* initialize local variables */
    memcpy(&(cvm->stack[cvm->sp]),
           callee->initializer,
           callee->stackneeded);

    /* set up procedure arguments */
    for(i = 0; i < callee->args; i++) {
        flags = cvm->inst[argsindex + (i * CALL_ARG_SIZE) + CALL_ARG_FLAGS];
        val = cvm->inst[argsindex + (i * CALL_ARG_SIZE) + CALL_ARG_VAL];
        index = cvm->inst[argsindex + (i * CALL_ARG_SIZE) + CALL_ARG_INDEX];
        ptr = cvm->sp;

        if(update_src_ref(cvm, &flags, &val, &index, &ptr) < 0) {
            return(-1);
        }

        STACK_ARG(newsp, i + 1)->flags = flags;
        STACK_ARG(newsp, i + 1)->val = val;
        STACK_ARG(newsp, i + 1)->index = index;
        STACK_ARG(newsp, i + 1)->ptr = ptr;
    }

    /* push return and procedure to be called on to the stack */
    cvm->csp++;
    /* make the return ip start at the next instruction */
    cvm->cstack[cvm->csp - 1].ip =
        argsindex + (cvm->proc[procindex].args * CALL_ARG_SIZE);
    cvm->cstack[cvm->csp - 1].proc = procindex;

    cvm->sp = newsp;
    cvm->ip = callee->instruction;

    return(0);
}

static int update_dest_ref(CrustyVM *cvm,
                           int *flags,
                           int *val,
                           int *index,
                           int *ptr) {
    if((*flags & MOVE_FLAG_TYPE_MASK) == MOVE_FLAG_VAR) {
        if(variable_is_argument(&(cvm->var[*val]))) {
            if((STACK_ARG(cvm->sp, cvm->var[*val].offset)->flags &
                MOVE_FLAG_TYPE_MASK) ==
               MOVE_FLAG_VAR) {
                if((*flags & MOVE_FLAG_INDEX_TYPE_MASK) == MOVE_FLAG_INDEX_VAR) {
                    if(variable_is_argument(&(cvm->var[*index]))) {
                        if((STACK_ARG(cvm->sp, cvm->var[*index].offset)->flags &
                           MOVE_FLAG_TYPE_MASK) == MOVE_FLAG_VAR) {
                            if(cvm->var[STACK_ARG(cvm->sp,
                                                  cvm->var[*index].offset)->val].type ==
                               CRUSTY_TYPE_FLOAT) {
                                cvm->status = CRUSTY_STATUS_FLOAT_INDEX;
                                return(-1);
                            }

                            if(read_var(cvm,
                                        index,
                                        NULL,
                                        STACK_ARG(cvm->sp, cvm->var[*index].offset)->ptr,
                                        &(cvm->var[STACK_ARG(cvm->sp,
                                                             cvm->var[*index].offset)->val]),
                                        STACK_ARG(cvm->sp,
                                                  cvm->var[*index].offset)->index) < 0) {
                                cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                                return(-1);
                            }
                        } else {
                            *index = STACK_ARG(cvm->sp, cvm->var[*index].offset)->val;
                        }
                    } else {
                        if(cvm->var[*index].type == CRUSTY_TYPE_FLOAT) {
                            cvm->status = CRUSTY_STATUS_FLOAT_INDEX;
                            return(-1);
                        }

                        if(read_var(cvm,
                                    index,
                                    NULL,
                                    GET_PTR(*index, *ptr),
                                    &(cvm->var[*index]),
                                    0) < 0) {
                            cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                            return(-1);
                        }
                    }
                } /* else {
                    do nothing
                } */
                if(*index < 0) {
                    cvm->status = CRUSTY_STATUS_OUT_OF_RANGE;
                    return(-1);
                }

                *index += STACK_ARG(cvm->sp, cvm->var[*val].offset)->index;
                if(*index >
                   (int)(cvm->var[STACK_ARG(cvm->sp, 
                                            cvm->var[*val].offset)->val].length -
                   1)) {
                    cvm->status = CRUSTY_STATUS_OUT_OF_RANGE;
                    return(-1);
                }

                *ptr = STACK_ARG(cvm->sp, cvm->var[*val].offset)->ptr;
                *val = STACK_ARG(cvm->sp, cvm->var[*val].offset)->val;
                /* index is already updated */
            } else {
                if((*flags & MOVE_FLAG_INDEX_TYPE_MASK) == MOVE_FLAG_INDEX_VAR) {
                    if(variable_is_argument(&(cvm->var[*index]))) {
                        if((STACK_ARG(cvm->sp, cvm->var[*index].offset)->flags &
                            MOVE_FLAG_TYPE_MASK) ==
                           MOVE_FLAG_VAR) {
                            if(cvm->var[STACK_ARG(cvm->sp,
                                                  cvm->var[*index].offset)->val].type ==
                               CRUSTY_TYPE_FLOAT) {
                                cvm->status = CRUSTY_STATUS_FLOAT_INDEX;
                                return(-1);
                            }

                            if(read_var(cvm,
                                        index,
                                        NULL,
                                        STACK_ARG(cvm->sp, cvm->var[*index].offset)->ptr,
                                        &(cvm->var[STACK_ARG(cvm->sp,
                                                             cvm->var[*index].offset)->val]),
                                        STACK_ARG(cvm->sp,
                                                  cvm->var[*index].offset)->index) < 0) {
                                cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                                return(-1);
                            }
                        } else {
                            *index = STACK_ARG(cvm->sp, cvm->var[*index].offset)->val;
                        }
                    } else {
                        if(cvm->var[*index].type == CRUSTY_TYPE_FLOAT) {
                            cvm->status = CRUSTY_STATUS_FLOAT_INDEX;
                            return(-1);
                        }

                        if(read_var(cvm,
                                    index,
                                    NULL,
                                    GET_PTR(*index, *ptr),
                                    &(cvm->var[*index]),
                                    0) < 0) {
                            cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                            return(-1);
                        }
                    }
                } /* else {
                    do nothing
                } */
                if(*index < 0 || *index > 0) {
                    cvm->status = CRUSTY_STATUS_OUT_OF_RANGE;
                    return(-1);
                }

                /* special case but should be safe since the ptr will point to
                   the immediate value on the stack and index will always be + 0
                   and the write_var function will just see the local var which
                   is not a callback and just treat it as a pointer with 0 index
                   and chrinit will be NULL which will cause write_var to treat
                   it as an integer.
                 */
                /* do some goofy nonsense to get the pointer (in VM memory) in
                   to the stack of the value referenced by val */
                *ptr = cvm->sp -
                       (cvm->var[*val].offset * sizeof(CrustyStackArg)) +
                       offsetof(CrustyStackArg, val);
            }
        } else {
            if((*flags & MOVE_FLAG_INDEX_TYPE_MASK) == MOVE_FLAG_INDEX_VAR) {
                if(variable_is_argument(&(cvm->var[*index]))) {
                    if((STACK_ARG(cvm->sp, cvm->var[*index].offset)->flags &
                       MOVE_FLAG_TYPE_MASK) == MOVE_FLAG_VAR) {
                        if(cvm->var[STACK_ARG(cvm->sp,
                                              cvm->var[*index].offset)->val].type ==
                           CRUSTY_TYPE_FLOAT) {
                            cvm->status = CRUSTY_STATUS_FLOAT_INDEX;
                            return(-1);
                        }

                        if(read_var(cvm,
                                    index,
                                    NULL,
                                    STACK_ARG(cvm->sp, cvm->var[*index].offset)->ptr,
                                    &(cvm->var[STACK_ARG(cvm->sp,
                                                         cvm->var[*index].offset)->val]),
                                    STACK_ARG(cvm->sp,
                                              cvm->var[*index].offset)->index) < 0) {
                            cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                            return(-1);
                        }
                    } else {
                        *index = STACK_ARG(cvm->sp, cvm->var[*index].offset)->val;
                    }
                } else {
                    if(cvm->var[*index].type == CRUSTY_TYPE_FLOAT) {
                        cvm->status = CRUSTY_STATUS_FLOAT_INDEX;
                        return(-1);
                    }

                    if(read_var(cvm,
                                index,
                                NULL,
                                GET_PTR(*index, *ptr),
                                &(cvm->var[*index]),
                                0) < 0) {
                        cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                        return(-1);
                    }
                }
            } /* else {
                do nothing
            } */
            if(*index < 0 || *index > (int)(cvm->var[*val].length - 1)) {
                cvm->status = CRUSTY_STATUS_OUT_OF_RANGE;
                return(-1);
            }

            /* val and index are already correct */
            *ptr = GET_PTR(*val, *ptr);
        }
    } else {
        cvm->status = CRUSTY_STATUS_INTERNAL_ERROR;
        return(-1);
    }

    /* on success, this function will have always set flags to be a VAR */
    *flags = MOVE_FLAG_VAR;
    return(0);
}

static int fetch_val(CrustyVM *cvm,
              int flags,
              int val,
              int index,
              int *intval,
              double *floatval,
              int ptr) {
    if((flags & MOVE_FLAG_TYPE_MASK) == MOVE_FLAG_VAR) {
        if(read_var(cvm,
                    intval,
                    floatval,
                    ptr,
                    &(cvm->var[val]),
                    index) < 0) {
            cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
            return(-1);
        }
    } else {
        *intval = val;
    }

    return(0);
}

/* if flags isn't VAR, calling this is invalid */
static void store_result(CrustyVM *cvm,
                         int val,
                         int index,
                         int ptr) {
    write_var(cvm,
              cvm->intresult,
              cvm->floatresult,
              ptr,
              &(cvm->var[val]),
              index);
}

#define POPULATE_ARGS \
    destflags = cvm->inst[cvm->ip + MOVE_DEST_FLAGS]; \
    destval = cvm->inst[cvm->ip + MOVE_DEST_VAL]; \
    destindex = cvm->inst[cvm->ip + MOVE_DEST_INDEX]; \
    destptr = cvm->sp; \
    srcflags = cvm->inst[cvm->ip + MOVE_SRC_FLAGS]; \
    srcval = cvm->inst[cvm->ip + MOVE_SRC_VAL]; \
    srcindex = cvm->inst[cvm->ip + MOVE_SRC_INDEX]; \
    srcptr = cvm->sp; \
    if(update_dest_ref(cvm, \
                       &destflags, \
                       &destval, \
                       &destindex, \
                       &destptr) < 0) { \
        break; \
    } \
    if(update_src_ref(cvm, \
                      &srcflags, \
                      &srcval, \
                      &srcindex, \
                      &srcptr) < 0) { \
        break; \
    }
 
#define FETCH_VALS \
    if(cvm->var[destval].write != NULL) { \
        cvm->status = CRUSTY_STATUS_INVALID_INSTRUCTION; \
        return(cvm->status); \
    } \
    \
    if(fetch_val(cvm, \
                 srcflags, \
                 srcval, \
                 srcindex, \
                 &intoperand, \
                 &floatoperand, \
                 srcptr) < 0) { \
        break; \
    } \
    if(fetch_val(cvm, \
                 destflags, \
                 destval, \
                 destindex, \
                 &(cvm->intresult), \
                 &(cvm->floatresult), \
                 destptr) < 0) { \
        break; \
    }

#define MATH_INSTRUCTION(OP) \
    POPULATE_ARGS \
    \
    FETCH_VALS \
    \
    if(srcflags == MOVE_FLAG_VAR) { \
        if(cvm->var[srcval].type == CRUSTY_TYPE_FLOAT && \
           cvm->var[destval].type != CRUSTY_TYPE_FLOAT) { \
            cvm->intresult = ((double)(cvm->intresult)) OP floatoperand; \
            cvm->resulttype = CRUSTY_TYPE_INT; \
        } else if(cvm->var[srcval].type != CRUSTY_TYPE_FLOAT && \
                  cvm->var[destval].type == CRUSTY_TYPE_FLOAT) { \
            cvm->floatresult = cvm->floatresult OP ((double)intoperand); \
            cvm->resulttype = CRUSTY_TYPE_FLOAT; \
        } else if(cvm->var[srcval].type == CRUSTY_TYPE_FLOAT && \
                  cvm->var[destval].type == CRUSTY_TYPE_FLOAT) { \
            cvm->floatresult = cvm->floatresult OP floatoperand; \
            cvm->resulttype = CRUSTY_TYPE_FLOAT; \
        } else { /* both not float */ \
            cvm->intresult = cvm->intresult OP intoperand; \
            cvm->resulttype = CRUSTY_TYPE_INT; \
        } \
    } else { \
        /* immediates can only be ints */ \
        if(cvm->var[destval].type == CRUSTY_TYPE_FLOAT) { \
            cvm->floatresult = cvm->floatresult OP ((double)intoperand); \
            cvm->resulttype = CRUSTY_TYPE_FLOAT; \
        } else { \
            cvm->intresult = cvm->intresult OP intoperand; \
            cvm->resulttype = CRUSTY_TYPE_INT; \
        } \
    } \
    \
    store_result(cvm, destval, destindex, destptr); \
    \
    cvm->ip += MOVE_ARGS + 1;

#define LOGIC_INSTRUCTION(OP) \
    POPULATE_ARGS \
    \
    FETCH_VALS \
    \
    if(srcflags == MOVE_FLAG_VAR) { \
        if(cvm->var[srcval].type == CRUSTY_TYPE_FLOAT || \
           cvm->var[destval].type == CRUSTY_TYPE_FLOAT) { \
            cvm->status = CRUSTY_STATUS_INVALID_INSTRUCTION; \
            break; \
        } \
    } \
    \
    cvm->intresult = cvm->intresult OP intoperand; \
    cvm->resulttype = CRUSTY_TYPE_INT; \
    \
    store_result(cvm, destval, destindex, destptr); \
    \
    cvm->ip += MOVE_ARGS + 1;

#define JUMP_INSTRUCTION(CMP) \
    if(cvm->resulttype == CRUSTY_TYPE_INT) { \
        if(cvm->intresult CMP 0) { \
            cvm->ip = (unsigned int)(cvm->inst[cvm->ip + JUMP_LOCATION]); \
        } else { \
            cvm->ip += JUMP_ARGS + 1; \
        } \
    } else { \
        if(cvm->floatresult CMP 0.0) { \
            cvm->ip = (unsigned int)(cvm->inst[cvm->ip + JUMP_LOCATION]); \
        } else { \
            cvm->ip += JUMP_ARGS + 1; \
        } \
    }

CrustyStatus crustyvm_step(CrustyVM *cvm) {
    int destflags, destval, destindex, destptr;
    int srcflags, srcval, srcindex, srcptr;
    double floatoperand;
    int intoperand;
    CrustyVariable *dest, *src;

    if(cvm->status != CRUSTY_STATUS_ACTIVE) {
        return(cvm->status);
    }

#ifdef CRUSTY_TEST
    if(cvm->flags & CRUSTY_FLAG_TRACE) {
        if(check_instruction(cvm, NULL, cvm->ip) < 0) {
            LOG_PRINTF(cvm, "Invalid instruction at %u.\n", cvm->ip);
            cvm->status = CRUSTY_STATUS_INVALID_INSTRUCTION;
            return(cvm->status);
        }
    }
#endif

    switch(cvm->inst[cvm->ip]) {
        case CRUSTY_INSTRUCTION_TYPE_MOVE:
            POPULATE_ARGS

            /* because "write" callbacks accept a pointer now, a
             * result of a math operation can't be passed to a
             * callback, so only move can be used with a callback
             * as a destination.  All operations can still use "read"
             * callbacks. */
            /* destval should be an index to a variable */
            dest = &(cvm->var[destval]);
            if(dest->write != NULL) { /* destination is callback */
                if((srcflags & MOVE_FLAG_TYPE_MASK) == MOVE_FLAG_VAR) {
                    src = &(cvm->var[srcval]);
                    if(src->read != NULL) {
                        if(src->type == CRUSTY_TYPE_CHAR) {
                            /* the function will assume only 1 byte of storage
                             * so make sure it is all clear. */
                            cvm->intresult = 0;
                            if(src->read(src->readpriv,
                                         &(cvm->intresult),
                                         srcindex)) {
                                cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                                return(cvm->status);
                            }
                            cvm->resulttype = CRUSTY_TYPE_INT;
                        } else if(src->type == CRUSTY_TYPE_FLOAT) {
                            if(src->read(src->readpriv,
                                         &(cvm->floatresult),
                                         srcindex)) {
                                cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                                return(cvm->status);
                            }
                            cvm->resulttype = CRUSTY_TYPE_FLOAT;
                        } else { /* INT */
                            if(src->read(src->readpriv,
                                         &(cvm->intresult),
                                         srcindex)) {
                                cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                                return(cvm->status);
                            }
                            cvm->resulttype = CRUSTY_TYPE_INT;
                        }

                        if(dest->write(dest->writepriv,
                                       cvm->resulttype,
                                       1,
                                       cvm->resulttype == CRUSTY_TYPE_INT ?
                                           (void *)&(cvm->intresult) :
                                           (void *)&(cvm->floatresult),
                                       destindex) < 0) {
                            cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                            return(cvm->status);
                        }
                    } else {
                        if(src->type == CRUSTY_TYPE_INT) {
                            srcptr += srcindex * sizeof(int);
                            cvm->intresult = *(int *)(&(cvm->stack[srcptr]));
                            cvm->resulttype = CRUSTY_TYPE_INT;
                        } else if(src->type == CRUSTY_TYPE_FLOAT) {
                            srcptr += srcindex * sizeof(double);
                            cvm->floatresult = *(float *)(&(cvm->stack[srcptr]));
                            cvm->resulttype = CRUSTY_TYPE_FLOAT;
                        } else {
                            srcptr += srcindex;
                            cvm->intresult = cvm->stack[srcptr];
                            cvm->resulttype = CRUSTY_TYPE_INT;
                        }

                        if(dest->write(dest->writepriv,
                                       src->type,
                                       src->length - srcindex,
                                       &(cvm->stack[srcptr]),
                                       destindex) < 0) {
                            cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                            return(cvm->status);
                        }
                    }
                } else {
                    if(dest->write(dest->writepriv,
                                   CRUSTY_TYPE_INT,
                                   1,
                                   &srcval,
                                   destindex) < 0) {
                        cvm->status = CRUSTY_STATUS_CALLBACK_FAILED;
                        return(cvm->status);
                    }
                }
            } else { /* destination is memory */
                if(fetch_val(cvm,
                             srcflags,
                             srcval,
                             srcindex,
                             &(cvm->intresult),
                             &(cvm->floatresult),
                             srcptr) < 0) {
                    break;
                }

                if(srcflags == MOVE_FLAG_VAR) {
                    if(cvm->var[srcval].type == CRUSTY_TYPE_FLOAT &&
                       cvm->var[destval].type != CRUSTY_TYPE_FLOAT) {
                        cvm->intresult = cvm->floatresult;
                        cvm->resulttype = CRUSTY_TYPE_INT;
                    } else if((cvm->var[srcval].type !=
                               CRUSTY_TYPE_FLOAT) &&
                              (cvm->var[destval].type ==
                               CRUSTY_TYPE_FLOAT)) {
                        cvm->floatresult = cvm->intresult;
                        cvm->resulttype = CRUSTY_TYPE_FLOAT;
                    }
                    /* if src and dest are the same type, no
                     * conversion is necessary */
                } else {
                    /* immediates can only be ints */
                    if(cvm->var[destval].type == CRUSTY_TYPE_FLOAT) {
                        cvm->floatresult = cvm->intresult;
                        cvm->resulttype = CRUSTY_TYPE_FLOAT;
                    }
                }

                store_result(cvm, destval, destindex, destptr);
            }

            cvm->ip += MOVE_ARGS + 1;
            break;
        case CRUSTY_INSTRUCTION_TYPE_ADD:
            MATH_INSTRUCTION(+)
            break;
        case CRUSTY_INSTRUCTION_TYPE_SUB:
            MATH_INSTRUCTION(-)
            break;
        case CRUSTY_INSTRUCTION_TYPE_MUL:
            MATH_INSTRUCTION(*)
            break;
        case CRUSTY_INSTRUCTION_TYPE_DIV:
            MATH_INSTRUCTION(/)
            break;
        case CRUSTY_INSTRUCTION_TYPE_MOD:
            POPULATE_ARGS

            FETCH_VALS

            if(srcflags == MOVE_FLAG_VAR) {
                if(cvm->var[srcval].type == CRUSTY_TYPE_FLOAT &&
                   cvm->var[destval].type != CRUSTY_TYPE_FLOAT) {
                    cvm->intresult = fmod((double)cvm->intresult, floatoperand);
                    cvm->resulttype = CRUSTY_TYPE_INT;
                } else if(cvm->var[srcval].type != CRUSTY_TYPE_FLOAT &&
                          cvm->var[destval].type == CRUSTY_TYPE_FLOAT) {
                    cvm->floatresult = fmod(cvm->floatresult, (double)intoperand);
                    cvm->resulttype = CRUSTY_TYPE_FLOAT;
                } else if(cvm->var[srcval].type == CRUSTY_TYPE_FLOAT &&
                          cvm->var[destval].type == CRUSTY_TYPE_FLOAT) {
                    cvm->floatresult = fmod(cvm->floatresult, floatoperand);
                    cvm->resulttype = CRUSTY_TYPE_FLOAT;
                } else { /* both not float */
                    cvm->intresult = cvm->intresult % intoperand;
                    cvm->resulttype = CRUSTY_TYPE_INT;
                }
            } else {
                /* immediates can only be ints */
                if(cvm->var[destval].type == CRUSTY_TYPE_FLOAT) {
                    cvm->floatresult = fmod(cvm->floatresult, (double)intoperand);
                    cvm->resulttype = CRUSTY_TYPE_FLOAT;
                } else {
                    cvm->intresult = cvm->intresult % intoperand;
                    cvm->resulttype = CRUSTY_TYPE_INT;
                }
            }

            store_result(cvm, destval, destindex, destptr);

            cvm->ip += MOVE_ARGS + 1;
            break;
        case CRUSTY_INSTRUCTION_TYPE_AND:
            LOGIC_INSTRUCTION(&)
            break;
        case CRUSTY_INSTRUCTION_TYPE_OR:
            LOGIC_INSTRUCTION(|)
            break;
        case CRUSTY_INSTRUCTION_TYPE_XOR:
            LOGIC_INSTRUCTION(^)
            break;
        case CRUSTY_INSTRUCTION_TYPE_SHR:
            POPULATE_ARGS

            FETCH_VALS

            /* make sure we're shifting by an integer, so just truncate the float
               value to an integer */
            if(srcflags == MOVE_FLAG_VAR &&
               cvm->var[srcval].type == CRUSTY_TYPE_FLOAT) {
                if(cvm->var[destval].type == CRUSTY_TYPE_FLOAT) {
                    cvm->status = CRUSTY_STATUS_INVALID_INSTRUCTION;
                    break;
                } else {
                    cvm->intresult = cvm->intresult >> intoperand;
                    cvm->resulttype = CRUSTY_TYPE_INT;
                }
            } else {
                if(cvm->var[destval].type == CRUSTY_TYPE_FLOAT) {
                    cvm->status = CRUSTY_STATUS_INVALID_INSTRUCTION;
                    break;
                } else {
                    cvm->intresult = cvm->intresult >> intoperand;
                    cvm->resulttype = CRUSTY_TYPE_INT;
                }
            }

            store_result(cvm, destval, destindex, destptr);

            cvm->ip += MOVE_ARGS + 1;
            break;
        case CRUSTY_INSTRUCTION_TYPE_SHL:
            POPULATE_ARGS

            FETCH_VALS

            /* make sure we're shifting by an integer, so just truncate the float
               value to an integer */
            if(srcflags == MOVE_FLAG_VAR &&
               cvm->var[srcval].type == CRUSTY_TYPE_FLOAT) {
                if(cvm->var[destval].type == CRUSTY_TYPE_FLOAT) {
                    cvm->status = CRUSTY_STATUS_INVALID_INSTRUCTION;
                    break;
                } else {
                    cvm->intresult = cvm->intresult << intoperand;
                    cvm->resulttype = CRUSTY_TYPE_INT;
                }
            } else {
                if(cvm->var[destval].type == CRUSTY_TYPE_FLOAT) {
                    cvm->status = CRUSTY_STATUS_INVALID_INSTRUCTION;
                    break;
                } else {
                    cvm->intresult = cvm->intresult << intoperand;
                    cvm->resulttype = CRUSTY_TYPE_INT;
                }
            }

            store_result(cvm, destval, destindex, destptr);

            cvm->ip += MOVE_ARGS + 1;
            break;
        case CRUSTY_INSTRUCTION_TYPE_CMP:
            /* this one is a bit special because destination never needs to be
               written to, so treat both as src references */
            destflags = cvm->inst[cvm->ip + MOVE_DEST_FLAGS]; \
            destval = cvm->inst[cvm->ip + MOVE_DEST_VAL]; \
            destindex = cvm->inst[cvm->ip + MOVE_DEST_INDEX]; \
            destptr = cvm->sp; \
            srcflags = cvm->inst[cvm->ip + MOVE_SRC_FLAGS]; \
            srcval = cvm->inst[cvm->ip + MOVE_SRC_VAL]; \
            srcindex = cvm->inst[cvm->ip + MOVE_SRC_INDEX]; \
            srcptr = cvm->sp; \

            if(update_src_ref(cvm, &destflags, &destval, &destindex, &destptr) < 0) {
                break;
            }
            if(update_src_ref(cvm, &srcflags, &srcval, &srcindex, &srcptr) < 0) {
                break;
            }

            if(fetch_val(cvm,
                         srcflags,
                         srcval,
                         srcindex,
                         &intoperand,
                         &floatoperand,
                         srcptr) < 0) {
                break;
            }
            if(fetch_val(cvm,
                         destflags,
                         destval,
                         destindex,
                         &(cvm->intresult),
                         &(cvm->floatresult),
                         destptr) < 0) {
                break;
            }

            if(srcflags == MOVE_FLAG_VAR) {
                if(destflags == MOVE_FLAG_VAR) {
                    if(cvm->var[srcval].type == CRUSTY_TYPE_FLOAT &&
                       cvm->var[destval].type != CRUSTY_TYPE_FLOAT) {
                        cvm->floatresult = ((double)(cvm->intresult)) - floatoperand;
                        cvm->resulttype = CRUSTY_TYPE_FLOAT;
                    } else if(cvm->var[srcval].type != CRUSTY_TYPE_FLOAT &&
                              cvm->var[destval].type == CRUSTY_TYPE_FLOAT) {
                        cvm->floatresult = cvm->floatresult - ((double)intoperand);
                        cvm->resulttype = CRUSTY_TYPE_FLOAT;
                    } else if(cvm->var[srcval].type == CRUSTY_TYPE_FLOAT &&
                              cvm->var[destval].type == CRUSTY_TYPE_FLOAT) {
                        cvm->floatresult -= floatoperand;
                        cvm->resulttype = CRUSTY_TYPE_FLOAT;
                    } else { /* both not float */
                        cvm->intresult -= intoperand;
                        cvm->resulttype = CRUSTY_TYPE_INT;
                    }
                } else { /* with cmp, destination can be an immediate */
                    if(cvm->var[srcval].type == CRUSTY_TYPE_FLOAT) {
                        cvm->floatresult = ((double)(cvm->intresult)) - floatoperand;
                        cvm->resulttype = CRUSTY_TYPE_FLOAT;
                    } else {
                        cvm->intresult -= intoperand;
                    }
                }
            } else {
                /* immediates can only be ints */
                if(destflags == MOVE_FLAG_VAR) {
                    if(cvm->var[destval].type == CRUSTY_TYPE_FLOAT) {
                        cvm->floatresult -= ((double)intoperand);
                        cvm->resulttype = CRUSTY_TYPE_FLOAT;
                    } else {
                        cvm->intresult -= intoperand;
                        cvm->resulttype = CRUSTY_TYPE_INT;
                    }
                } else { /* I guess we're comparing 2 immediates */
                    cvm->intresult -= intoperand;
                    cvm->resulttype = CRUSTY_TYPE_INT;
                }
            }

            cvm->ip += MOVE_ARGS + 1;
            break;
        case CRUSTY_INSTRUCTION_TYPE_JUMP:
            /* jump to self means nothing more can happen, so end execution. */
            if(cvm->ip == (unsigned int)(cvm->inst[cvm->ip + JUMP_LOCATION])) {
                cvm->status = CRUSTY_STATUS_READY;
                break;
            }
            cvm->ip = (unsigned int)(cvm->inst[cvm->ip + JUMP_LOCATION]);
            break;
        case CRUSTY_INSTRUCTION_TYPE_JUMPN:
            JUMP_INSTRUCTION(!=)
            break;
        case CRUSTY_INSTRUCTION_TYPE_JUMPZ:
            JUMP_INSTRUCTION(==)
            break;
        case CRUSTY_INSTRUCTION_TYPE_JUMPL:
            JUMP_INSTRUCTION(<)
            break;
        case CRUSTY_INSTRUCTION_TYPE_JUMPG:
            JUMP_INSTRUCTION(>)
            break;
        case CRUSTY_INSTRUCTION_TYPE_CALL:
            if(call(cvm,
                    cvm->inst[cvm->ip + CALL_PROCEDURE],
                    cvm->ip + CALL_START_ARGS) < 0) {
                break;
            }

            //crustyvm_debugtrace(cvm, 0);
            /* no need to update ip */
            break;
        case CRUSTY_INSTRUCTION_TYPE_RET:
            /* going to return from initial call */
            if(cvm->csp == 1) {
                cvm->status = CRUSTY_STATUS_READY;
                break;
            }

            cvm->ip = cvm->cstack[cvm->csp - 1].ip;
            cvm->sp -= cvm->proc[cvm->cstack[cvm->csp - 1].proc].stackneeded;

            cvm->csp--;
            break;
        default:
            cvm->status = CRUSTY_STATUS_INVALID_INSTRUCTION;
    }

    return(cvm->status);
}

#undef JUMP_INSTRUCTION
#undef MATH_INSTRUCTION

CrustyStatus crustyvm_get_status(CrustyVM *cvm) {
    return(cvm->status);
}

const char *crustyvm_statusstr(CrustyStatus status) {
    if(status < 0 || status >= CRUSTY_STATUS_INVALID) {
        return(CRUSTY_STATUSES[CRUSTY_STATUS_INVALID]);
    }

    return(CRUSTY_STATUSES[status]);
}

int crustyvm_begin(CrustyVM *cvm, const char *procname) {
    int procnum;

    if(cvm->status != CRUSTY_STATUS_READY) {
        LOG_PRINTF(cvm, "Cannot start running, status is not active.\n");
        return(-1);
    }

    cvm->stage = "runtime init";
#ifdef CRUSTY_TEST
    LOG_PRINTF(cvm, "Start\n");
#endif

    procnum = find_procedure(cvm, procname);
    if(procnum == -1) {
        LOG_PRINTF(cvm, "Couldn't find procedure: %s\n", procname);
        return(-1);
    }
    if(cvm->proc[procnum].args > 0) {
        LOG_PRINTF(cvm, "Can't enter from procedure with arguments.\n");
        return(-1);
    }

    /* just some nonsense value so the call stack has something reasonable on it
       even though this will never be used */
    cvm->ip = 0;

    cvm->sp = cvm->initialstack;
    cvm->csp = 0;
    cvm->intresult = 0;
    cvm->floatresult = 0.0;
    cvm->resulttype = CRUSTY_TYPE_INT;

    if(call(cvm, procnum, 0)) {
        LOG_PRINTF(cvm, "Failed to call procedure %s: %s\n", procname,
                   crustyvm_statusstr(crustyvm_get_status(cvm)));
        return(-1);
    }

    cvm->status = CRUSTY_STATUS_ACTIVE;

    return(0);
}

int crustyvm_run(CrustyVM *cvm, const char *procname) {
    if(crustyvm_begin(cvm, procname) < 0) {
        return(-1);
    }

    cvm->stage = "running";
#ifdef CRUSTY_TEST
    LOG_PRINTF(cvm, "Start\n");
#endif

    while(crustyvm_step(cvm) == CRUSTY_STATUS_ACTIVE);

    if(cvm->status != CRUSTY_STATUS_READY) {
        LOG_PRINTF(cvm, "Execution stopped with error: %s\n",
                   crustyvm_statusstr(crustyvm_get_status(cvm)));
        return(-1);
    }

    return(0);
}

static CrustyLine *inst_to_line(CrustyVM *cvm, unsigned int inst) {
    unsigned int i;

    for(i = 0; i < cvm->lines; i++) {
        if(cvm->line[i].instruction == inst) {
            return(&(cvm->line[i]));
        }
    }

    return(NULL);
}

void crustyvm_debugtrace(CrustyVM *cvm, int full) {
    unsigned int startcsp, csp, sp, ip;
    unsigned int flags, val, index, ptr;
    unsigned int i, j;
    CrustyProcedure *proc;
    const char *temp;
    CrustyLine *line;

    temp = cvm->stage;
    cvm->stage = "debug trace";
#ifdef CRUSTY_TEST
    LOG_PRINTF(cvm, "Start\n");
#endif

    csp = cvm->csp;
    startcsp = csp;
    sp = cvm->sp;
    ip = cvm->ip;

    while(csp > 0) {
        proc = &(cvm->proc[cvm->cstack[csp - 1].proc]);
        LOG_PRINTF(cvm, "%u: %s@", csp, proc->name);
        line = inst_to_line(cvm, ip);
        if(line == NULL) {
            LOG_PRINTF_BARE(cvm, "invalid");
        } else {
            /* IP at the top of the stack is the current line, but IP further
             * up the stack points to the next instruction. */
            LOG_PRINTF_BARE(cvm, "%s:%u",
                                 TOKENVAL(line->moduleOffset),
                                 csp == startcsp ?
                                 line->line :
                                 line->line - 1);
        }
        for(i = 0; i < proc->args; i++) {
            LOG_PRINTF_BARE(cvm, " %s", proc->var[i]->name);
        }
        LOG_PRINTF_BARE(cvm, "\n");

        for(i = 0; i < proc->args; i++) {
            flags = STACK_ARG(sp, i + 1)->flags;
            val = STACK_ARG(sp, i + 1)->val;
            index = STACK_ARG(sp, i + 1)->index;
            ptr = STACK_ARG(sp, i + 1)->ptr;
            switch(flags & MOVE_FLAG_TYPE_MASK) {
                case MOVE_FLAG_VAR:
                    LOG_PRINTF(cvm, " %u: %s -> %s.%s@%u[%u]:%u\n",
                               i,
                               proc->var[i]->name,
                               cvm->var[val].proc == NULL ? "Global" : cvm->var[val].proc->name,
                               cvm->var[val].name,
                               ptr,
                               cvm->var[val].length,
                               index);
                    break;
                case MOVE_FLAG_IMMEDIATE:
                    LOG_PRINTF(cvm, " %u: %s = %d\n",
                               i,
                               proc->var[i]->name,
                               val);
                    break;
                default:
                    LOG_PRINTF(cvm, " %u: Invalid flags %X\n", i, flags);
                    break;
            }
        }
        for(i = proc->args; i < proc->vars; i++) {
            LOG_PRINTF(cvm, " %u: %s@%u[%u]",
                       i,
                       proc->var[i]->name,
                       sp - proc->var[i]->offset,
                       proc->var[i]->length);
            if(full) {
                if(proc->var[i]->type == CRUSTY_TYPE_INT) {
                    for(j = 0;
                        j < proc->var[i]->length && j < DEBUG_MAX_PRINT;
                        j++) {
                        LOG_PRINTF_BARE(cvm, " %d",
                            *((int *)(&(cvm->stack[sp -
                                                   proc->var[i]->offset +
                                                   (j * sizeof(int))]))));
                    }
                } else if(proc->var[i]->type == CRUSTY_TYPE_FLOAT) {
                    for(j = 0;
                        j < proc->var[i]->length && j < DEBUG_MAX_PRINT;
                        j++) {
                        LOG_PRINTF_BARE(cvm, " %g",
                            *((int *)(&(cvm->stack[sp -
                                                   proc->var[i]->offset +
                                                   (j * sizeof(double))]))));
                    }
                } else { /* chrinit */
                    LOG_PRINTF_BARE(cvm, " \"");
                    for(j = 0;
                        j < proc->var[i]->length && j < DEBUG_MAX_PRINT;
                        j++) {
                        LOG_PRINTF_BARE(cvm, "%c",
                            *((int *)(&(cvm->stack[sp - proc->var[i]->offset + j]))));
                    }
                    LOG_PRINTF_BARE(cvm, "\"");
                }
            }
            LOG_PRINTF_BARE(cvm, "\n");
        }

        sp -= proc->stackneeded;
        ip = cvm->cstack[csp - 1].ip;
        csp--;
    }
    LOG_PRINTF(cvm, "Global:\n");
    for(i = 0; i < cvm->vars; i++) {
        if(variable_is_global(&(cvm->var[i]))) {
            if(variable_is_callback(&(cvm->var[i]))) {
                LOG_PRINTF(cvm, " %u: %s[%u] CB\n",
                           i,
                           cvm->var[i].name,
                           cvm->var[i].length);
            } else {
                LOG_PRINTF(cvm, " %u: %s@%u[%u]",
                           i,
                           cvm->var[i].name,
                           cvm->var[i].offset,
                           cvm->var[i].length);
                if(full) {
                    if(cvm->var[i].type == CRUSTY_TYPE_INT) {
                        for(j = 0;
                            j < cvm->var[i].length && j < DEBUG_MAX_PRINT;
                            j++) {
                            LOG_PRINTF_BARE(cvm, " %d",
                                *((int *)(&(cvm->stack[cvm->var[i].offset +
                                                       (j * sizeof(int))]))));
                        }
                    } else if(cvm->var[i].type == CRUSTY_TYPE_FLOAT) {
                        for(j = 0;
                            j < cvm->var[i].length && j < DEBUG_MAX_PRINT;
                            j++) {
                            LOG_PRINTF_BARE(cvm, " %g",
                                *((int *)(&(cvm->stack[cvm->var[i].offset +
                                                       (j * sizeof(double))]))));
                        }
                    } else { /* CHAR */
                        LOG_PRINTF_BARE(cvm, " \"");
                        for(j = 0;
                            j < cvm->var[i].length && j < DEBUG_MAX_PRINT;
                            j++) {
                            LOG_PRINTF_BARE(cvm, "%c",
                                *((int *)(&(cvm->stack[cvm->var[i].offset + j]))));
                        }
                        LOG_PRINTF_BARE(cvm, "\"");
                    }
                }
                LOG_PRINTF_BARE(cvm, "\n");
            }
        }
    }

    cvm->stage = temp;
}

int crustyvm_has_entrypoint(CrustyVM *cvm, const char *name) {
    int procnum;

    procnum = find_procedure(cvm, name);
    if(procnum == -1) {
        return(0);
    }

    if(cvm->proc[procnum].args > 0) {
        return(0);
    }

    return(1);
}

unsigned int crustyvm_get_tokenmem(CrustyVM *cvm) {
    return(cvm->tokenmemlen);
}

unsigned int crustyvm_get_stackmem(CrustyVM *cvm) {
    return(cvm->stacksize);
}

#ifdef CRUSTY_TEST
void vprintf_cb(void *priv, const char *fmt, ...) {
    va_list ap;
    FILE *out = priv;

    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
}

int write_to(void *priv,
             CrustyType type,
             unsigned int size,
             void *ptr,
             unsigned int index) {
    switch(type) {
        case CRUSTY_TYPE_CHAR:
            fprintf((FILE *)priv, "%c", (char *)ptr);
            break;
        case CRUSTY_TYPE_INT:
            fprintf((FILE *)priv, "%d", (int *)ptr);
            break;
        case CRUSTY_TYPE_FLOAT:
            fprintf((FILE *)priv, "%g", (float *)ptr);
            break;
        default:
            return(-1);
    }

    return(0);
}

int write_string_to(void *priv,
                    CrustyType type,
                    unsigned int size,
                    void *ptr,
                    unsigned int index) {
    if(type != CRUSTY_TYPE_CHAR) {
        return(-1);
    }

    if(fwrite((char *)ptr,
              1,
              size,
              (FILE *)priv) < size) {
        return(-1);
    }

    return(0);
}

#define CLEAN_ARGS \
    if(vars > 0) { \
        for(i = 0; i < vars; i++) { \
            free(var[i]); \
            free(value[i]); \
        } \
        free(var); \
        free(value); \
    } \
    vars = 0;

int main(int argc, char **argv) {
    const char *filename = NULL;
    char *fullpath;
    unsigned int i;
    unsigned int arglen;
    char *equals;
    char *temp;
    char **tempa;
    char **var = NULL;
    char **value = NULL;
    unsigned int vars = 0;

    FILE *in = NULL;
    CrustyVM *cvm = NULL;
    char *program = NULL;
    long len;
    int result;
    CrustyCallback cb[] = {
        {
            .name = "out",
            .length = 1,
            .read = NULL,
            .readpriv = NULL,
            .write = write_to,
            .writepriv = stdout
        },
        {
            .name = "err",
            .length = 1,
            .read = NULL,
            .readpriv = NULL,
            .write = write_to,
            .writepriv = stderr
        },
        {
            .name = "string_out",
            .length = 1,
            .read = NULL,
            .readpriv = NULL,
            .write = write_string_to,
            .writepriv = stdout
        },
        {
            .name = "string_err",
            .length = 1,
            .read = NULL,
            .readpriv = NULL,
            .write = write_string_to,
            .writepriv = stderr
        }
    };

    for(i = 1; i < (unsigned int)argc; i++) {
        arglen = strlen(argv[i]);
        if(arglen > 0 && argv[i][0] == '-') {
            if(arglen > 1) {
                if(argv[i][1] == '-') {
                    if(filename != NULL) {
                        filename = NULL;
                        break;
                    }
                    if(i + 1 < (unsigned int)argc) {
                        filename = argv[i + 1];
                    }
                    break;
                } else if(argv[i][1] == 'D') {
                    if(argv[i][2] == '=') {
                        filename = NULL;
                        break;
                    }
                    equals = strchr(&(argv[i][2]), '=');
                    if(equals == NULL) {
                        filename = NULL;
                        break;
                    }

                    tempa = realloc(var, sizeof(char *) * (vars + 1));
                    if(tempa == NULL) {
                        fprintf(stderr, "Failed to allocate memory "
                                        "for vars list.\n");
                        goto error;
                    }
                    var = tempa;
                    tempa = realloc(value, sizeof(char *) * (vars + 1));
                    if(tempa == NULL) {
                        fprintf(stderr, "Failed to allocate memory "
                                        "for values list.\n");
                        goto error;
                    }
                    value = tempa;
                    /* difference from start, take away "-D", add
                     * space for '\0' */
                    temp = malloc(equals - argv[i] - 2 + 1);
                    if(temp == NULL) {
                        fprintf(stderr, "Failed to allocate memory "
                                        "for var.\n");
                        goto error;
                    }
                    memcpy(temp, &(argv[i][2]), equals - argv[i] - 2);
                    temp[equals - argv[i] - 2] = '\0';
                    var[vars] = temp;
                    /* total length, take away the length of the first
                     * part, take away the '=', add the '\0' */
                    temp = malloc(arglen -
                                  (equals - argv[i] - 2) -
                                  1 +
                                  1);
                    if(temp == NULL) {
                        fprintf(stderr, "Failed to allocate memory "
                                        "for value.\n");
                        goto error;
                    }
                    memcpy(temp,
                           &(equals[1]),
                           arglen - (equals - argv[i] - 2) - 1);
                    temp[arglen - (equals - argv[i] - 2) - 1] = '\0';
                    value[vars] = temp;
                    vars++;
                } else {
                    filename = NULL;
                    break;
                }
            }
        } else {
            if(filename != NULL) {
                filename = NULL;
                break;
            }
            filename = argv[i];
        }
    }

    if(filename == NULL) {
        fprintf(stderr, "USAGE: %s [(<filename>|-D<var>=<value>) ...]"
                        " [-- <filename>]\n", argv[0]);
        goto error;
    }

    fullpath = NULL;
    in = crustyvm_open_file(filename, &fullpath, vprintf_cb, stderr);
    if(in == NULL) {
        fprintf(stderr, "Failed to open file %s.\n", filename);
        goto error;
    }

    if(fseek(in, 0, SEEK_END) < 0) {
       fprintf(stderr, "Failed to seek to end of file.\n");
       goto error;
    }

    len = ftell(in);
    if(len < 0) {
        fprintf(stderr, "Failed to get file length.\n");
        goto error;
    }
    rewind(in);

    program = malloc(len);
    if(program == NULL) {
        fprintf(stderr, "Failed to allocate memory for program.\n");
        goto error;
    }

    if(fread(program, 1, len, in) < (unsigned long)len) {
        fprintf(stderr, "Failed to read file.\n");
        goto error;
    }

    fclose(in);
    in = NULL;

    cvm = crustyvm_new(filename, fullpath,
                       program, len,
                       CRUSTY_FLAG_OUTPUT_PASSES
                       /* | CRUSTY_FLAG_TRACE */,
                       0,
                       cb, sizeof(cb) / sizeof(CrustyCallback),
                       (const char **)var, (const char **)value, vars,
                       vprintf_cb, stderr);
    if(cvm == NULL) {
        fprintf(stderr, "Failed to load program.\n");
        goto error;
    }
    free(program);
    program = NULL;
    CLEAN_ARGS
    fprintf(stderr, "Program loaded.\n");

    fprintf(stderr, "Token memory size: %u\n", cvm->tokenmemlen);
    fprintf(stderr, "Stack size: %u\n", cvm->stacksize);

    result = crustyvm_run(cvm, "init");
    fprintf(stderr, "\n");
    if(result < 0) {
        fprintf(stderr, "Program reached an exception while running: "
                        "%s\n",
                crustyvm_statusstr(crustyvm_get_status(cvm)));
        crustyvm_debugtrace(cvm, 1);
        goto error;
    }

    fprintf(stderr, "Program completed successfully.\n");
    crustyvm_free(cvm);

    exit(EXIT_SUCCESS);

error:
    if(cvm != NULL) {
        free(cvm);
    }

    if(program != NULL) {
        free(program);
    }

    if(in != NULL) {
        fclose(in);
    }

    if(fullpath != NULL) {
        free(fullpath);
    }

    CLEAN_ARGS

    exit(EXIT_FAILURE);
}
#endif
