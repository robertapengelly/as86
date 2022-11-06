/******************************************************************************
 * @file            macro.c
 *****************************************************************************/
#include    <stdlib.h>

#include    "hashtab.h"
#include    "lib.h"
#include    "macro.h"

struct hashtab hashtab_macros = { 0 };

extern char is_end_of_line[];
extern void ignore_rest_of_line (char **pp);

int has_macro (char *p) {

    struct hashtab_name *key;
    int exists = 0;
    
    if ((key = hashtab_alloc_name (p)) == NULL) {
        return exists;
    }
    
    exists = (hashtab_get (&hashtab_macros, key) != NULL);
    free (key);
    
    return exists;

}

void handler_define (char **pp) {

    char *name;
    char *value;
    
    struct hashtab_name *key;
    char saved_ch;
    
    *pp = skip_whitespace (*pp);
    name = *pp;
    
    while (**pp && **pp != ' ' && !is_end_of_line[(int) **pp]) {
        (*pp)++;
    }
    
    saved_ch = **pp;
    **pp = '\0';
    
    if ((key = hashtab_alloc_name (xstrdup (name))) == NULL) {
    
        **pp = saved_ch;
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    *pp = skip_whitespace (*pp + 1);
    value = *pp;
    
    while (**pp && !is_end_of_line[(int) **pp]) {
        (*pp)++;
    }
    
    saved_ch = **pp;
    **pp = '\0';
    
    if (!*value) { value = "1"; }
    hashtab_put (&hashtab_macros, key, xstrdup (value));
    
    **pp = saved_ch;

}
