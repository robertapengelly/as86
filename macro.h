/******************************************************************************
 * @file            macro.h
 *****************************************************************************/
#ifndef     _MACRO_H
#define     _MACRO_H

#include    "hashtab.h"


#define handler_define handdef
#define hashtab_macros htmacros


extern struct hashtab hashtab_macros;

int has_macro (char *p);
void handler_define (char **pp);

#endif
