/******************************************************************************
 * @file            process.c
 *****************************************************************************/
#include    <ctype.h>
#include    <stddef.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "as.h"
#include    "frag.h"
#include    "hashtab.h"
#include    "intel.h"
#include    "lex.h"
#include    "lib.h"
#include    "listing.h"
#include    "load_line.h"
#include    "macro.h"
#include    "pseudo_ops.h"
#include    "process.h"
#include    "report.h"
#include    "section.h"
#include    "symbol.h"
#include    "vector.h"

static const char *filename = 0;
static unsigned long line_number = 0;

char lex_table[256] = {

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0,                             /* SPACE!"#$%&'()*+,-./ */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1,                             /* 0123456789:;<=>?     */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,                             /* @ABCDEFGHIJKLMNO     */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 3,                             /* #PQRSTUVWXYZ[\]^_    */
    0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,                             /* 'abcdefghijklmno     */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0                              /* pqrstuvwxyz{|}~      */

};

/**
 * Index: a character
 * 
 * Output:
 *     1 - the character ends a line
 *     2 - the character is a line separator
 *
 * Line separators allowing having multiple logical lines on a single physical line.
 */
char is_end_of_line[256] = {
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 /* '\0' and '\n' */
};

static char *find_end_of_line (char *line) {

    while (!is_end_of_line[(int) *line]) {
        ++(line);
    }
    
    return line;

}

char get_symbol_name_end (char **pp) {

    char c = **pp;
    
    if (is_name_beginner ((int) (*pp)[0])) {
    
        while (is_name_part ((int) (*pp)[0])) {
            (*pp)++;
        }
        
        c = **pp;
        **pp = '\0';
    
    }
    
    return c;

}

int read_and_append_char_in_ascii (char **pp, int double_quotes, int size) {

    char ch;
    int i;
    
    unsigned long number;
    
    switch (ch = *((*pp)++)) {
    
        case '"':
        
            if (double_quotes) {
                return 1;
            }
            
            for (i = 0; i < size; i++) {
                frag_append_1_char ((ch >> (8 * i)) & 0xff);
            }
            
            break;
        
        case '\'':
        
            if (!double_quotes) {
                return 1;
            }
            
            for (i = 0; i < size; i++) {
                frag_append_1_char ((ch >> (8 * i)) & 0xff);
            }
            
            break;
        
        case '\0':
        
            --*pp;                      /* Might be the end of line buffer. */
            
            report (REPORT_WARNING, "null character in string; '\"' inserted");
            return 1;
        
        case '\n':
        
            report (REPORT_WARNING, "unterminated string; newline inserted");
            ++line_number;
            
            for (i = 0; i < size; i++) {
                frag_append_1_char ((ch >> (8 * i)) & 0xff);
            }
            
            break;
        
        case '\\':
        
            ch = *((*pp)++);
            
            switch (ch) {
            
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                
                    for (i = 0, number = 0; isdigit (ch) && (i < 3); (ch = *((*pp)++)), i++) {
                        number = number * 8 + ch - '0';
                    }
                    
                    for (i = 0; i < size; i++) {
                        frag_append_1_char ((number >> (8 * i)) & 0xff);
                    }
                    
                    (*pp)--;
                    break;
                
                case 'r':
                
                    number = 13;
                    
                    for (i = 0; i < size; i++) {
                        frag_append_1_char ((number >> (8 * i)) & 0xff);
                    }
                    
                    break;
                
                case 'n':
                
                    number = 10;
                    
                    for (i = 0; i < size; i++) {
                        frag_append_1_char ((number >> (8 * i)) & 0xff);
                    }
                    
                    break;
                
                case '\\':
                case '"':
                case '\'':
                
                    for (i = 0; i < size; i++) {
                        frag_append_1_char ((ch >> (8 * i)) & 0xff);
                    }
                    
                    break;
                
                default:
                
                    report_at (__FILE__, __LINE__, REPORT_INTERNAL_ERROR, "+++UNIMPLEMENTED escape sequence read_and_append_char_in_ascii");
                    exit (EXIT_FAILURE);
            
            }
            
            break;
        
        default:
        
            for (i = 0; i < size; i++) {
                frag_append_1_char ((ch >> (8 * i)) & 0xff);
            }
            
            break;
    
    }
    
    return 0;

}

void get_filename_and_line_number (const char **filename_p, unsigned long *line_number_p) {

    *filename_p = filename;
    *line_number_p = line_number;
    
}

void ignore_rest_of_line (char **pp) {

    *pp = find_end_of_line (*pp);
    ++*pp;

}

void demand_empty_rest_of_line (char **pp) {

    *pp = skip_whitespace (*pp);
    
    if (is_end_of_line[(int) **pp]) {
        ++(*pp);
    } else {
    
        if (isprint ((int) **pp)) {
            report (REPORT_ERROR, "junk at the end of line, first unrecognized character is '%c'", **pp);
        } else {
            report (REPORT_ERROR, "junk at the end of line, first unrecognized character valued 0x%x", **pp);
        }
        
        ignore_rest_of_line (pp);
    
    }

}


static struct vector cond_stack = { 0 };
static int enable = 1, satisfy = 0;

#define     CF_ENABLE                   (1 << 0)
#define     CF_SATISFY_SHIFT            (1 << 0)

#define     CF_SATISFY_MASK             (3 << CF_SATISFY_SHIFT)

static long cond_value (int enable, int satisfy) {
  return (enable ? CF_ENABLE : 0) | (satisfy << CF_SATISFY_SHIFT);
}

static int handler_if (char **pp) {

    struct expr expr;
    expression_read_into (pp, &expr);
    
    vec_push (&cond_stack, (void *) cond_value (enable, satisfy));
    
    satisfy = (expr.type == EXPR_TYPE_CONSTANT && expr.add_number != 0);
    enable = enable && satisfy == 1;
    
    return enable;

}

static int handler_ifdef (char **pp) {

    char saved_ch, *temp = *pp;
    
    while (!is_end_of_line[(int) *temp]) {
        ++(temp);
    }
    
    saved_ch = *temp;
    *temp = '\0';
    
    vec_push (&cond_stack, (void *) cond_value (enable, satisfy));
    
    satisfy = has_macro (*pp);
    enable = enable && satisfy == 1;
    
    *temp = saved_ch;
    *pp = find_end_of_line (*pp);
    
    return enable;

}

static int handler_ifndef (char **pp) {

    char saved_ch, *temp = *pp;
    
    while (!is_end_of_line[(int) *temp]) {
        ++(temp);
    }
    
    saved_ch = *temp;
    *temp = '\0';
    
    vec_push (&cond_stack, (void *) cond_value (enable, satisfy));
    
    satisfy = !has_macro (*pp);
    enable = enable && satisfy == 1;
    
    *temp = saved_ch;
    *pp = find_end_of_line (*pp);
    
    return enable;

}

static int handler_elif (char **pp) {

    int32_t last = cond_stack.length - 1;
    int cond = 0;
    
    long flag;
    
    if (last < 0) {
    
        report (REPORT_ERROR, "'elif': no matching if");
        
        ignore_rest_of_line (pp);
        return enable;
    
    }
    
    flag = (long) cond_stack.data[last];
    
    if (satisfy == 2) {
    
        report (REPORT_ERROR, "elif after else");
        
        ignore_rest_of_line (pp);
        return enable;
    
    }
    
    if (satisfy == 0) {
    
        struct expr expr;
        expression_read_into (pp, &expr);
        
        cond = (expr.type == EXPR_TYPE_CONSTANT && expr.add_number != 0);
        
        if (cond) {
            satisfy = 1;
        }
    
    } else {
        ignore_rest_of_line (pp);
    }
    
    enable = !enable && cond && ((flag & CF_ENABLE) != 0);
    return enable;

}

static int handler_elifdef (char **pp) {

    int32_t last = cond_stack.length - 1;
    int cond = 0;
    
    long flag;
    
    if (last < 0) {
    
        report (REPORT_ERROR, "'elifdef': no matching if");
        
        ignore_rest_of_line (pp);
        return enable;
    
    }
    
    flag = (long) cond_stack.data[last];
    
    if (satisfy == 2) {
    
        report (REPORT_ERROR, "elifdef after else");
        
        ignore_rest_of_line (pp);
        return enable;
    
    }
    
    if (satisfy == 0) {
    
        char saved_ch, *temp = *pp;
        
        while (!is_end_of_line[(int) *temp]) {
            ++(temp);
        }
        
        saved_ch = *temp;
        *temp = '\0';
        
        cond = has_macro (*pp);
        
        *temp = saved_ch;
        *pp = find_end_of_line (*pp);
        
        if (cond) {
            satisfy = 1;
        }
    
    } else {
        ignore_rest_of_line (pp);
    }
    
    enable = !enable && cond && ((flag & CF_ENABLE) != 0);
    return enable;

}

static int handler_elifndef (char **pp) {

    int32_t last = cond_stack.length - 1;
    int cond = 0;
    
    long flag;
    
    if (last < 0) {
    
        report (REPORT_ERROR, "'elifndef': no matching if");
        
        ignore_rest_of_line (pp);
        return enable;
    
    }
    
    flag = (long) cond_stack.data[last];
    
    if (satisfy == 2) {
    
        report (REPORT_ERROR, "elifndef after else");
        
        ignore_rest_of_line (pp);
        return enable;
    
    }
    
    if (satisfy == 0) {
    
        char saved_ch, *temp = *pp;
        
        while (!is_end_of_line[(int) *temp]) {
            ++(temp);
        }
        
        saved_ch = *temp;
        *temp = '\0';
        
        cond = !has_macro (*pp);
        
        *temp = saved_ch;
        *pp = find_end_of_line (*pp);
        
        if (cond) {
            satisfy = 1;
        }
    
    } else {
        ignore_rest_of_line (pp);
    }
    
    enable = !enable && cond && ((flag & CF_ENABLE) != 0);
    return enable;

}

static int handler_else (char **pp) {

    int32_t last = cond_stack.length - 1;
    long flag;
    
    if (last < 0) {
    
        report (REPORT_ERROR, "'else': no matching if");
        
        ignore_rest_of_line (pp);
        return enable;
    
    }
    
    flag = (long) cond_stack.data[last];
    
    if (satisfy == 2) {
    
        report (REPORT_ERROR, "else after else");
        
        ignore_rest_of_line (pp);
        return enable;
    
    }
    
    enable = !enable && satisfy == 0 && ((flag & CF_ENABLE) != 0);
    satisfy = 2;
    
    return enable;

}

static int handler_endif (char **pp) {

    int32_t len = cond_stack.length;
    long flag;
    
    if (len <= 0) {
    
        report (REPORT_ERROR, "endif used without if");
        
        ignore_rest_of_line (pp);
        return enable;
    
    }
    
    flag = (long) cond_stack.data[--len];
    
    enable = (flag & CF_ENABLE) != 0;
    satisfy = (flag & CF_SATISFY_MASK) >> CF_SATISFY_SHIFT;
    
    cond_stack.length = len;
    return enable;

}


void handler_include (char **pp) {

    char *orig_ilp;
    
    const char *orig_filename = filename;
    unsigned long orig_ln = line_number;
    
    char *p2 = NULL, *tmp;
    unsigned long i, len = 0;
    
    *pp = skip_whitespace (*pp);
    tmp = *pp;
    
    if (**pp == '"' || **pp == '\'') {
    
        char ch;
        int double_quotes = (**pp == '"');
        
        while ((ch = *++*pp)) {
        
            if (ch == '"' && double_quotes) {
            
                ++(*pp);
                break;
            
            } else if (ch == '\'') {
            
                ++(*pp);
                break;
            
            }
            
            len++;
            continue;
        
        }
        
        if (double_quotes && ch != '"') {
        
            report (REPORT_ERROR, "unterminated string");
            
            ignore_rest_of_line (pp);
            return;
        
        } else if (ch != '\'') {
        
            report (REPORT_ERROR, "unterminated string");
            
            ignore_rest_of_line (pp);
            return;
        
        }
        
    } else {
        
        report (REPORT_ERROR, "missing string");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    p2 = xmalloc (len + 1);
    tmp++;
    
    for (i = 0; i < len; i++) {
    
        if (*tmp == '"') {
            break;
        }
        
        p2[i] = *tmp++;
    
    }
    
    p2[len] = '\0';
    
    demand_empty_rest_of_line (pp);
    orig_ilp = *pp;
    
    if (!process_file (p2)) {
    
        *pp = orig_ilp;
        goto done;
    
    }
    
    for (i = 0; i < state->nb_inc_paths; i++) {
    
        tmp = xmalloc (strlen (state->inc_paths[i]) + len);
        
        strcpy (tmp, state->inc_paths[i]);
        strcat (tmp, p2);
        
        if (!process_file (tmp)) {
        
            free (tmp);
            
            *pp = orig_ilp;
            goto done;
        
        }
        
        free (tmp);
    
    }
    
    report_at (orig_filename, orig_ln, REPORT_ERROR, "can't open '%s' for reading", p2);

done:

    free (p2);
    
    filename = orig_filename;
    line_number = orig_ln;

}

int process_file (const char *fname) {

    unsigned long new_line_number = 1;
    void *load_line_internal_data = NULL;
    
    unsigned long newlines;
    char *line, *line_end;
    
    unsigned long real_line_len;
    char *real_line;
    
    int enabled = 1, i;
    FILE *ifp;
    
    if ((ifp = fopen (fname, "r")) == NULL) {
        return 1;
    }
    
    filename = fname;
    line_number = 0;
    
    if ((load_line_internal_data = load_line_create_internal_data (&new_line_number)) == NULL) {
        return 1;
    }
    
    while (!load_line (&line, &line_end, &real_line, &real_line_len, &newlines, ifp, &load_line_internal_data)) {
    
        line_number = new_line_number;
        new_line_number += newlines + 1;
        
        if (state->listing) {
        
            update_listing_line (current_frag);
            add_listing_line (real_line, real_line_len, filename, line_number);
        
        }
        
        while (line < line_end) {
        
            char saved_ch, *start_p;
            
            line = skip_whitespace (line);
            start_p = line;
            
            if (*line == '%') {
                ++(line);
            }
            
            saved_ch = get_symbol_name_end (&line);
            
            if (xstrcasecmp (start_p, "%if") == 0 || xstrcasecmp (start_p, ".if") == 0 || xstrcasecmp (start_p, "if") == 0) {
            
                *line = saved_ch;
                
                line = skip_whitespace (line);
                enabled = handler_if (&line);
                
                continue;
            
            } else if (xstrcasecmp (start_p, "%ifdef") == 0 || xstrcasecmp (start_p, ".ifdef") == 0 || xstrcasecmp (start_p, "ifdef") == 0) {
            
                *line = saved_ch;
                
                line = skip_whitespace (line);
                enabled = handler_ifdef (&line);
                
                continue;
            
            } else if (xstrcasecmp (start_p, "%ifndef") == 0 || xstrcasecmp (start_p, ".ifndef") == 0 || xstrcasecmp (start_p, "ifndef") == 0) {
            
                *line = saved_ch;
                
                line = skip_whitespace (line);
                enabled = handler_ifndef (&line);
                
                continue;
            
            } else if (xstrcasecmp (start_p, "%elif") == 0 || xstrcasecmp (start_p, ".elif") == 0 || xstrcasecmp (start_p, "elif") == 0) {
            
                *line = saved_ch;
                
                line = skip_whitespace (line);
                enabled = handler_elif (&line);
                
                continue;
            
            } else if (xstrcasecmp (start_p, "%elifdef") == 0 || xstrcasecmp (start_p, ".elifdef") == 0 || xstrcasecmp (start_p, "elifdef") == 0) {
            
                *line = saved_ch;
                
                line = skip_whitespace (line);
                enabled = handler_elifdef (&line);
                
                continue;
            
            } else if (xstrcasecmp (start_p, "%elifndef") == 0 || xstrcasecmp (start_p, ".elifndef") == 0 || xstrcasecmp (start_p, "elifndef") == 0) {
            
                *line = saved_ch;
                
                line = skip_whitespace (line);
                enabled = handler_elifndef (&line);
                
                continue;
            
            } else if (xstrcasecmp (start_p, "%else") == 0 || xstrcasecmp (start_p, "else") == 0 || xstrcasecmp (start_p, "else") == 0) {
            
                *line = saved_ch;
                
                line = skip_whitespace (line);
                enabled = handler_else (&line);
                
                continue;
            
            } else if (xstrcasecmp (start_p, "%endif") == 0 || xstrcasecmp (start_p, ".endif") == 0 || xstrcasecmp (start_p, "endif") == 0) {
            
                *line = saved_ch;
                
                line = skip_whitespace (line);
                enabled = handler_endif (&line);
                
                continue;
            
            } else {
            
                *line = saved_ch;
                line = start_p;
            
            }
            
            if (!enabled) {
            
                ignore_rest_of_line (&line);
                continue;
            
            }
            
            if (*line == '%') {
                ++(line);
            }
            
            saved_ch = get_symbol_name_end (&line);
            
            if (xstrcasecmp (start_p, "extern") == 0 || xstrcasecmp (start_p, "extrn") == 0) {
            
                struct hashtab_name *key;
                line = skip_whitespace (line + 1);
                
                start_p = line;
                saved_ch = get_symbol_name_end (&line);
                
                if ((key = hashtab_alloc_name (xstrdup (start_p))) == NULL) {
                
                    report_at (program_name, 0, REPORT_ERROR, "memory full (malloc)");
                    goto next;
                
                }
                
                if (hashtab_get (&state->hashtab_externs, key) != NULL) {
                
                    free (key);
                    
                    report_at (program_name, 0, REPORT_ERROR, "hashtab collision");
                    goto next;
                
                }
                
                hashtab_put (&state->hashtab_externs, key, xstrdup (start_p));
            
            next:
            
                *line = saved_ch;
                
                ignore_rest_of_line (&line);
                continue;
            
            } else if (xstrcasecmp (start_p, "assume") == 0 || xstrcasecmp (start_p, "dgroup") == 0 || xstrcasecmp (start_p, ".stack") == 0) {
            
                report (REPORT_WARNING, "%s unimplemented; ignored", start_p);
                *line = saved_ch;
                
                ignore_rest_of_line (&line);
                continue;
            
            } else if (xstrcasecmp (start_p, "proc") == 0 || xstrcasecmp (start_p, "endp") == 0) {
            
                report (REPORT_ERROR, "procedure must have a name");
                *line = saved_ch;
                
                ignore_rest_of_line (&line);
                continue;
            
            } else {
            
                *line = saved_ch;
                line = start_p;
            
            }
            
            if (is_name_beginner ((int) *line)) {
            
                struct pseudo_op *poe;
                
                if (*line == '%') {
                
                    line++;
                    
                    if (*line == ' ') {
                        continue;
                    }
                
                }
                
                saved_ch = get_symbol_name_end (&line);
                
                if (xstrcasecmp (start_p, "equ") == 0) {
                
                    report (REPORT_ERROR, "missing label for equ");
                    
                    *line = saved_ch;
                    ignore_rest_of_line (&line);
                    
                    continue;
                
                }
                
                if ((poe = find_pseudo_op (start_p)) != NULL) {
                
                    *line = saved_ch;
                    
                    line = skip_whitespace (line);
                    (poe->handler) (&line);
                    
                    continue;
                
                }
                
                if (machine_dependent_is_register (start_p)) {
                
                    report (REPORT_ERROR, "label or instruction expected at start of line");
                    
                    *line = saved_ch;
                    ignore_rest_of_line (&line);
                    
                    continue;
                
                }
                
                if (saved_ch && (saved_ch == ':' || (*skip_whitespace (line + 1)) == ':')) {
                
                    char temp_ch, *temp_line = line, *temp_start_p;
                    
                    if (saved_ch != ':') {
                        temp_line = skip_whitespace (line + 1);
                    }
                    
                    temp_line = skip_whitespace (temp_line + 1);
                    temp_start_p = temp_line;
                    
                    temp_ch = get_symbol_name_end (&temp_line);
                    
                    if (xstrcasecmp (temp_start_p, "equ") == 0) {
                    
                        line = skip_whitespace (temp_line + 1);
                        
                        handler_equ (&line, start_p);
                        continue;
                    
                    }
                    
                    *temp_line = temp_ch;
                    
                    symbol_label (start_p);
                    *line = saved_ch;
                    
                    if (*line != ':') {
                        line = skip_whitespace (line + 1) + 1;
                    }
                    
                    line = skip_whitespace (line + 1);
                    continue;
                
                }
                
                if (saved_ch && saved_ch == ' ') {
                
                    char temp_ch, *temp_line, *temp_start_p;
                    
                    temp_line = skip_whitespace (line + 1);
                    temp_start_p = temp_line;
                    
                    temp_ch = get_symbol_name_end (&temp_line);
                    
                    if (is_data_pseudo_op (temp_start_p)) {
                    
                        symbol_label (start_p);
                        
                        *temp_line = temp_ch;
                        line = temp_start_p;
                        
                        continue;
                    
                    }
                    
                    if ((temp_ch && temp_ch == '=') || xstrcasecmp (temp_start_p, "equ") == 0) {
                    
                        line = skip_whitespace (temp_line + 1);
                        
                        handler_equ (&line, start_p);
                        continue;
                    
                    }
                    
                    if (xstrcasecmp (temp_start_p, "label") == 0) {
                    
                        char *temp = xmalloc (13);
                        symbol_label (start_p);
                        
                        *temp_line = temp_ch;
                        line = temp_line;
                        
                        start_p = (line = skip_whitespace (line));
                        saved_ch = get_symbol_name_end (&line);
                        
                        if (xstrcasecmp (start_p, "byte") == 0) {
                        
                            strcpy (temp, "1 dup (?)");
                            
                            if ((poe = find_pseudo_op ("db")) != NULL) {
                                poe->handler (&temp);
                            }
                        
                        } else if (xstrcasecmp (start_p, "word") == 0) {
                        
                            strcpy (temp, "1 dup (?)");
                            
                            if ((poe = find_pseudo_op ("dw")) != NULL) {
                                poe->handler (&temp);
                            }
                        
                        }
                        
                        /*ignore_rest_of_line (&line);*/
                        *line = saved_ch;
                        continue;
                    
                    }
                    
                    if (xstrcasecmp (temp_start_p, "proc") == 0) {
                    
                        struct proc *proc = xmalloc (sizeof (*proc));
                        
                        char *temp, *name;
                        int offset = 0, i;
                        
                        proc->name = xstrdup (start_p);
                        
                        symbol_label (proc->name);
                        *temp_line = temp_ch;
                        
                        line = temp_line;
                        line = skip_whitespace (line);
                        
                        start_p = line;
                        saved_ch = get_symbol_name_end (&line);
                        
                        if (xstrcasecmp (start_p, "uses") == 0) {
                        
                            char *reg;
                            *line = saved_ch;
                            
                            while (!is_end_of_line[(int) *line] && *line == ' ') {
                            
                                line = skip_whitespace (line);
                                
                                if (!isalpha ((int) *line)) {
                                    break;
                                }
                                
                                start_p = line;
                                saved_ch = get_symbol_name_end (&line);
                                
                                reg = xstrdup (start_p);
                                vec_push (&proc->regs, reg);
                                
                                *line = saved_ch;
                            
                            }
                        
                        } else {
                        
                            *line = saved_ch;
                            line = start_p;
                        
                        }
                        
                        if (state->model < 4) {
                            offset = 4;
                        } else {
                            offset = 6;
                        }
                        
                        line = skip_whitespace (line);
                        
                        do {
                        
                            if (*line == ',') { line++; }
                            line = skip_whitespace (line);
                            
                            name = line;
                            saved_ch = get_symbol_name_end (&line);
                            
                            if (!isalnum ((int) *name)) {
                            
                                *line = saved_ch;
                                break;
                            
                            }
                            
                            if (saved_ch != ':') {
                            
                                *line = saved_ch;
                                break;
                            
                            }
                            
                            line = skip_whitespace (line + 1);
                            
                            start_p = line;
                            saved_ch = get_symbol_name_end (&line);
                            
                            temp = xmalloc (strlen (name) + 1 + 5 + 5 + 1 + 5 + 8 + 2);
                            
                            if (xstrcasecmp (start_p, "byte") == 0 || xstrcasecmp (start_p, "word") == 0) {
                            
                                sprintf (temp, "%s %s ptr [bp + %d]", name, start_p, offset);
                                offset += 2;
                            
                            } else if (xstrcasecmp (start_p, "dword") == 0) { 
                            
                                if (state->sym_start) {
                                    sprintf (temp, "%s [bp + %d]", name, offset);
                                } else {
                                    sprintf (temp, "%s %s ptr [bp + %d]", name, start_p, offset);
                                }
                                
                                offset += 4;
                            
                            } else if (xstrcasecmp (start_p, "ptr") == 0) { 
                            
                                if (machine_dependent_get_bits () == 32 || state->model >= 5) {
                                
                                    sprintf (temp, "%s [bp + %d]", name, offset);
                                    offset += 4;
                                
                                } else {
                                
                                    sprintf (temp, "%s [bp + %d]", name, offset);
                                    offset += 2;
                                
                                }
                            
                            } else {
                            
                                report (REPORT_ERROR, "unsupported argument type");
                                
                                *line = saved_ch;
                                break;
                            
                            }
                            
                            vec_push (&proc->args, xstrdup (start_p));
                            
                            handler_define (&temp);
                            /*free (temp);*/
                            
                            *line = saved_ch;
                            line = skip_whitespace (line);
                        
                        } while (!is_end_of_line[(int) *line] && *line == ',');
                        
                        if (proc->args.length > 0) {
                        
                            char *temp = xmalloc (8);
                            sprintf (temp, "push bp");
                            
                            machine_dependent_assemble_line (temp);
                            
                            temp = xmalloc (10);
                            sprintf (temp, "mov bp, sp");
                            
                            machine_dependent_assemble_line (temp);
                        
                        }
                        
                        for (i = 0; i < proc->regs.length; ++i) {
                        
                            char *temp, *reg = proc->regs.data[i];
                            
                            temp = xmalloc (4 + 1 + strlen (reg) + 1); 
                            sprintf (temp, "push %s", reg);
                            
                            machine_dependent_assemble_line (temp);
                        
                        }
                        
                        vec_push (&state->procs, (void *) proc);
                        
                        demand_empty_rest_of_line (&line);
                        continue;
                    
                    }
                    
                    if (xstrcasecmp (temp_start_p, "endp") == 0) {
                    
                        if (state->procs.length == 0) {
                            report (REPORT_ERROR, "block nesting error");
                        } else {
                        
                            int32_t last = state->procs.length - 1;
                            struct proc *proc = state->procs.data[last];
                            
                            if (strcmp (start_p, proc->name)) {
                                report (REPORT_ERROR, "procedure name does not match");
                            } else {
                                state->procs.length = last;
                            }
                            
                            if (proc->regs.length > 0) {
                            
                                unsigned char last_inst = current_frag->buf[current_frag->fixed_size - 1];
                                
                                char *temp, *reg;
                                int32_t i;
                                
                                current_frag->fixed_size--;
                                
                                for (i = proc->regs.length - 1; i >= 0; --i) {
                                
                                    reg = proc->regs.data[i];
                                    
                                    temp = xmalloc (3 + 1 + strlen (reg) + 1);
                                    sprintf (temp, "pop %s", reg);
                                    
                                    machine_dependent_assemble_line (temp);
                                
                                }
                                
                                frag_append_1_char (last_inst);
                                proc->regs.length = 0;
                            
                            }
                            
                            if (proc->args.length > 0) {
                            
                                if (current_frag->buf[current_frag->fixed_size - 1] == 0xCB) {
                                
                                    current_frag->buf[current_frag->fixed_size - 1] = 0x5D;
                                    frag_append_1_char (0xCB);
                                
                                } else if (current_frag->buf[current_frag->fixed_size - 1] == 0xC3) {
                                
                                    current_frag->buf[current_frag->fixed_size - 1] = 0x5D;
                                    frag_append_1_char (0xC3);
                                
                                }
                                
                                proc->args.length = 0;
                            
                            }
                        
                        }
                        
                        *temp_line = temp_ch;
                        line = temp_line;
                        
                        demand_empty_rest_of_line (&line);
                        continue;
                    
                    }
                    
                    if (xstrcasecmp (temp_start_p, "segment") == 0) {
                    
                        struct seg *seg = xmalloc (sizeof (*seg));
                        seg->name = xstrdup (start_p);
                        
                        vec_push (&state->segs, seg);
                        line = skip_whitespace (temp_line + 1);
                        
                        if ((poe = find_pseudo_op ("masm_segment")) != NULL) {
                            (poe->handler) (&line);
                        }
                        
                        continue;
                    
                    }
                    
                    if (xstrcasecmp (temp_start_p, "ends") == 0) {
                    
                        if (state->segs.length == 0) {
                            report (REPORT_ERROR, "block nesting error");
                        } else {
                        
                            int32_t last = state->segs.length - 1;
                            struct seg *seg = state->segs.data[last];
                            
                            if (strcmp (start_p, seg->name)) {
                                report (REPORT_ERROR, "segment name does not match");
                            } else {
                            
                                machine_dependent_set_bits (seg->bits);
                                state->segs.length = last;
                            
                            }
                        
                        }
                        
                        *temp_line = temp_ch;
                        line = temp_line;
                        
                        demand_empty_rest_of_line (&line);
                        continue;
                    
                    }
                    
                    *temp_line = temp_ch;
                
                }
                
                *line = saved_ch;
                
                if (current_section == bss_section) {
                
                    report (REPORT_WARNING, "attempt to initialize memory in a nobits section: ignored");
                    
                    ignore_rest_of_line (&line);
                    continue;
                
                }
                
                saved_ch = *(line = find_end_of_line (line));
                *line = '\0';
                
                line = machine_dependent_assemble_line (skip_whitespace (start_p));
                *(line)++ = saved_ch;
                
                continue;
            
            }
            
            if (is_end_of_line[(int) *line]) {
            
                line++;
                continue;
            
            }
            
            if (*line == '#' || *line == '/') {
                break;
            }
            
            demand_empty_rest_of_line (&line);
        
        }
    
    }
    
    if (state->listing) {
        update_listing_line (current_frag);
    }
    
    load_line_destory_internal_data (load_line_internal_data);
    fclose (ifp);
    
    if (cond_stack.length > 0) {
    
        int32_t count = cond_stack.length;
        report (REPORT_ERROR, "%d if statement%s not closed", count, (count == 1 ? "" : "s"));
    
    }
    
    for (i = 0; i < state->procs.length; ++i) {
    
        struct proc *proc = (struct proc *) state->procs.data[i];
        report (REPORT_ERROR, "procedure %s is not closed", proc->name);
    
    }
    
    for (i = 0; i < state->segs.length; ++i) {
    
        struct seg *seg = (struct seg *) state->segs.data[i];
        report (REPORT_ERROR, "segment %s is not closed", seg->name);
    
    }
    
    return 0;

}
